/*
 * Copyright (C) 2024-2026 David C. Manuelda (StormBytePP)
 *
 * This file is part of StormByte-Network.
 *
 * StormByte-Network original source is dual-licensed:
 *
 * 1. GNU Lesser General Public License v3.0 (or later)
 *    You may redistribute and/or modify this file under the terms of the
 *    GNU Lesser General Public License as published by the Free Software
 *    Foundation, either version 3 of the License, or (at your option)
 *    any later version.
 *
 * 2. Commercial license
 *    Alternatively, this file may be used under the terms of a commercial
 *    license agreement with the copyright holder
 *    (David C. Manuelda <StormByte@gmail.com>).
 *
 * Both licenses apply only to original StormByte-Network source in this
 * repository. They do not cover other StormByte modules or any third-party
 * material shipped with this repository (including everything under
 * thirdparty/, and in particular the bundled StormByte Buffer tree), which
 * remains under its own license.
 *
 * Neither license grants any patent rights. Any patent licenses required
 * to use this software or third-party components must be obtained separately
 * from the patent holders.
 *
 * StormByte-Network is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * version 3 along with StormByte-Network. If not, see
 * <https://www.gnu.org/licenses/lgpl-3.0.html>.
 *
 * SPDX-License-Identifier: LGPL-3.0-or-later OR LicenseRef-StormByte-Commercial
 */

#include <StormByte/logger/threaded_log.hxx>
#include <StormByte/network/client.hxx>
#include <StormByte/serializable.hxx>
#include <StormByte/test_handlers.h>

#include "tls_fixture.hxx"

#include <array>
#include <atomic>
#include <chrono>
#include <iostream>
#include <stdexcept>
#include <thread>

#ifdef UNIX
#include <arpa/inet.h>
#include <unistd.h>
#else
#include <winsock2.h>
#include <ws2tcpip.h>
#endif

using namespace StormByte;
using namespace StormByte::Network;
using namespace std::chrono_literals;

/**
 * @brief Test-provider-owned opaque packet.
 */
class TestPacket final: public Transport::Packet {
	public:
		explicit TestPacket(ByteSize size = ByteSize{0}): Packet(42) {
			m_bytes.resize(size);
		}

		TestPacket(OpcodeType opcode, Safe::Binary bytes): Packet(opcode), m_bytes(std::move(bytes)) {}

		Safe::Binary DoSerialize() const noexcept override {
			return m_bytes;
		}

	private:
		Safe::Binary m_bytes;
};

STORMBYTE_DECLARE_MAYBE_SAFE(TestPacket);

/**
 * @brief Public-only application client with a test-provider packet factory.
 */
class TestClient final: public Network::Client {
	public:
		explicit TestClient(Network::Client** on_decode_disconnect = nullptr,
			std::chrono::milliseconds decode_delay = 0ms): Client([on_decode_disconnect, decode_delay](Transport::Packet::OpcodeType opcode, Buffer::Consumer payload,
			Safe::Shared<Logger::Log>) -> PacketPointer {
			Safe::Binary bytes;
			payload.ExtractUntilEoF(bytes);
			std::this_thread::sleep_for(decode_delay);
			if (on_decode_disconnect)
				(*on_decode_disconnect)->Disconnect();
			return Safe::MakeShared<TestPacket>(opcode, std::move(bytes));
		}, Safe::Shared<Logger::Log>::MakePointer<Logger::ThreadedLog>(std::cerr, Logger::Level::Error)) {}

		TestClient(TestClient&&) noexcept = default;

		TestClient& operator=(TestClient&&) noexcept = default;

		using Client::Send;
};

/**
 * @brief Ephemeral raw loopback listener for silent, dribbling and congested peers.
 */
struct Peer {
#ifdef WINDOWS
	using Handle = SOCKET;
#else
	using Handle = int;
#endif
	Handle listener = ::socket(AF_INET, SOCK_STREAM, 0);
	Handle peer = static_cast<Handle>(-1);
	unsigned short port = 0;
	Crypto::TLS::Session session;

	/**
	 * @brief Listen on an ephemeral loopback port.
	 * @param receive_buffer Accepted-socket receive buffer, or zero for the OS default.
	 */
	explicit Peer(int receive_buffer = 0) {
		// Set before listen so accepted sockets inherit it and cannot auto-tune past it.
		if (receive_buffer > 0 && ::setsockopt(listener, SOL_SOCKET, SO_RCVBUF,
			reinterpret_cast<const char*>(&receive_buffer), sizeof(receive_buffer)) != 0)
			throw std::runtime_error("listener receive buffer");
		sockaddr_in address{};
		address.sin_family = AF_INET;
		address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
		if (::bind(listener, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0
			|| ::listen(listener, 4) != 0)
			throw std::runtime_error("loopback listener");
#ifdef UNIX
		socklen_t length = sizeof(address);
#else
		int length = sizeof(address);
#endif
		if (::getsockname(listener, reinterpret_cast<sockaddr*>(&address), &length) != 0)
			throw std::runtime_error("listener address");
		port = ntohs(address.sin_port);
	}

	~Peer() {
		Close(peer);
		Close(listener);
	}

	static void Close(Handle handle) {
		if (handle == static_cast<Handle>(-1))
			return;
#ifdef UNIX
		::close(handle);
#else
		::closesocket(handle);
#endif
	}

	void Accept() {
		peer = ::accept(listener, nullptr, nullptr);
		if (peer == static_cast<Handle>(-1))
			throw std::runtime_error("accept");
#ifdef SO_NOSIGPIPE
		const int enabled = 1;
		::setsockopt(peer, SOL_SOCKET, SO_NOSIGPIPE, &enabled, sizeof(enabled));
#endif
	}

	void Connect(TestClient& client) {
		if (!client.Connect(Connection::Protocol::IPv4, "127.0.0.1", port, 1s))
			throw std::runtime_error("connect");
		Accept();
	}

	bool Write(std::span<const std::byte> bytes) {
#ifdef LINUX
		const int flags = MSG_NOSIGNAL;
#else
		const int flags = 0;
#endif
		while (!bytes.empty()) {
			const int written = static_cast<int>(::send(peer, reinterpret_cast<const char*>(bytes.data()),
				static_cast<int>(bytes.size()), flags));
			if (written <= 0)
				return false;
			bytes = bytes.subspan(static_cast<std::size_t>(written));
		}
		return true;
	}

	bool ReadRequest() {
		std::array<char, sizeof(Transport::Packet::OpcodeType) + sizeof(std::size_t)> bytes{};
		std::size_t read = 0;
		while (read < bytes.size()) {
			const int count = static_cast<int>(::recv(peer, bytes.data() + read,
				static_cast<int>(bytes.size() - read), 0));
			if (count <= 0)
				return false;
			read += static_cast<std::size_t>(count);
		}
		return true;
	}

	void Authenticate() {
		const auto chain = TLSFixture::Load("server.pem");
		const auto key = TLSFixture::Load("server-test-key.pem");
		Crypto::TLS::ServerConfiguration configuration;
		configuration.certificate_chain_pem = chain.span();
		configuration.private_key_pem = key.span();
		auto created = Crypto::TLS::Session::CreateServer(configuration);
		if (!created)
			throw std::runtime_error("server TLS configuration");
		session = std::move(*created);
		std::array<std::byte, 4096> bytes{};
		for (;;) {
			for (;;) {
				auto count = session.DrainEncrypted(bytes);
				if (!count)
					throw std::runtime_error("server TLS drain");
				if (*count == 0)
					break;
				if (!Write(std::span{bytes}.first(*count)))
					throw std::runtime_error("server TLS write");
			}
			if (session.HandshakeComplete())
				return;
			const int read = static_cast<int>(::recv(peer, reinterpret_cast<char*>(bytes.data()),
				static_cast<int>(bytes.size()), 0));
			if (read <= 0 || !session.FeedEncrypted(std::span{bytes}.first(static_cast<std::size_t>(read))))
				throw std::runtime_error("server TLS read");
		}
	}

	void ConnectTLS(TestClient& client) {
		if (!TLSFixture::Configure(client))
			throw std::runtime_error("client TLS configuration");
		std::exception_ptr error;
		std::jthread server([&] {
			try {
				Accept();
				Authenticate();
			} catch (...) {
				error = std::current_exception();
			}
		});
		const auto connected = client.Connect(Connection::Protocol::IPv4, "127.0.0.1", port, 1s);
		server.join();
		if (error)
			std::rethrow_exception(error);
		if (!connected)
			throw std::runtime_error("client TLS connect");
	}

	Safe::Binary Encrypt(std::span<const std::byte> plaintext) {
		const auto written = session.WritePlaintext(plaintext);
		if (!written || *written != plaintext.size())
			throw std::runtime_error("server TLS encryption");
		Safe::Binary result;
		std::array<std::byte, 4096> bytes{};
		for (;;) {
			const auto drained = session.DrainEncrypted(bytes);
			if (!drained)
				throw std::runtime_error("server TLS output");
			if (*drained == 0)
				return result;
			result.append(std::span{bytes}.first(*drained));
		}
	}

	static Safe::Binary Frame(std::size_t payload = 0) {
		auto bytes = Serializable<Transport::Packet::OpcodeType>(42).Serialize();
		bytes.append(Serializable<std::size_t>(payload).Serialize().span());
		bytes.resize(bytes.size() + ByteSize{payload});
		return bytes;
	}
};

// -------------------
// Cancellation
// -------------------

int test_cancel_connect_handshake() {
	TestClient client;
	ASSERT_TRUE(TLSFixture::Configure(client));
	Peer fixture;
	Expected<void, Client::Error> result;
	const auto start = std::chrono::steady_clock::now();
	std::jthread connect([&] {
		result = client.Connect(Connection::Protocol::IPv4, "127.0.0.1", fixture.port, 5s);
	});
	fixture.Accept();
	std::array<char, 1> hello{};
	const auto received = ::recv(fixture.peer, hello.data(), 1, 0);
	client.Cancel();
	connect.join();
	ASSERT_EQUAL(1, received);
	ASSERT_FALSE(result);
	ASSERT_TRUE(*result.error() == Client::Error::Cancelled);
	ASSERT_TRUE(std::chrono::steady_clock::now() - start < 1s);
	ASSERT_TRUE(client.Status() == Connection::Status::Disconnected);
	ASSERT_TRUE(client.DisableTLS());
	Peer retry;
	retry.Connect(client);
	return 0;
}

#ifdef LINUX
int test_cancel_pending_native_connect() {
	Peer fixture;
	ASSERT_EQUAL(0, ::listen(fixture.listener, 0));
	TestClient queued;
	ASSERT_TRUE(queued.Connect(Connection::Protocol::IPv4, "127.0.0.1", fixture.port, 1s));
	TestClient client;
	Expected<void, Client::Error> result;
	std::jthread connect([&] {
		result = client.Connect(Connection::Protocol::IPv4, "127.0.0.1", fixture.port, 5s);
	});
	const auto admitted = std::chrono::steady_clock::now() + 1s;
	while (client.Telemetry()->ConnectionAttempts() == Size{0} && std::chrono::steady_clock::now() < admitted)
		std::this_thread::sleep_for(1ms);
	const auto start = std::chrono::steady_clock::now();
	client.Cancel();
	connect.join();
	ASSERT_FALSE(result);
	ASSERT_TRUE(*result.error() == Client::Error::Cancelled);
	ASSERT_TRUE(std::chrono::steady_clock::now() - start < 500ms);
	return 0;
}
#endif

int test_cancel_read_and_reconnect() {
	TestClient client;
	Peer fixture;
	fixture.Connect(client);
	Expected<PacketPointer, Client::Error> result;
	std::jthread send([&] { result = client.Send(TestPacket{}, 5s); });
	const bool read = fixture.ReadRequest();
	const auto start = std::chrono::steady_clock::now();
	client.Cancel();
	send.join();
	ASSERT_TRUE(read);
	ASSERT_FALSE(result);
	ASSERT_TRUE(*result.error() == Client::Error::Cancelled);
	ASSERT_TRUE(std::chrono::steady_clock::now() - start < 500ms);
	Peer retry;
	retry.Connect(client);
	ASSERT_TRUE(client.Status() == Connection::Status::Connected);
	return 0;
}

int test_cancel_reconnect_with_concurrent_status() {
	TestClient client;
	std::jthread observer([&](std::stop_token stop) {
		while (!stop.stop_requested()) {
			(void)client.Status();
			(void)client.Telemetry();
		}
	});
	for (int index = 0; index < 32; ++index) {
		Peer fixture;
		fixture.Connect(client);
		Expected<PacketPointer, Client::Error> result;
		std::jthread send([&] { result = client.Send(TestPacket{}, 2s); });
		const bool read = fixture.ReadRequest();
		client.Cancel();
		send.join();
		ASSERT_TRUE(read);
		ASSERT_FALSE(result);
		ASSERT_TRUE(*result.error() == Client::Error::Cancelled);
	}
	return 0;
}

int test_cancel_send() {
	TestClient client;
	Peer fixture;
	fixture.Connect(client);
	Expected<PacketPointer, Client::Error> result;
	TestPacket large(ByteSize{16 * 1024 * 1024});
	std::jthread send([&] { result = client.Send(large, 5s); });
	const bool read = fixture.ReadRequest();
	const auto start = std::chrono::steady_clock::now();
	client.Cancel();
	send.join();
	ASSERT_TRUE(read);
	ASSERT_FALSE(result);
	ASSERT_TRUE(*result.error() == Client::Error::Cancelled);
	ASSERT_TRUE(std::chrono::steady_clock::now() - start < 500ms);
	return 0;
}

int test_cancel_tls_send() {
	TestClient client;
	Peer fixture;
	fixture.ConnectTLS(client);
	Expected<PacketPointer, Client::Error> result;
	TestPacket large(ByteSize{16 * 1024 * 1024});
	std::jthread send([&] { result = client.Send(large, 5s); });
	std::array<char, 1> bytes{};
	const auto read = ::recv(fixture.peer, bytes.data(), 1, 0);
	const auto start = std::chrono::steady_clock::now();
	client.Cancel();
	send.join();
	ASSERT_EQUAL(1, read);
	ASSERT_FALSE(result);
	ASSERT_TRUE(*result.error() == Client::Error::Cancelled);
	ASSERT_TRUE(std::chrono::steady_clock::now() - start < 500ms);
	return 0;
}

int test_disconnect_from_packet_callback() {
	Network::Client* owner = nullptr;
	TestClient client(&owner);
	owner = &client;
	Peer fixture;
	fixture.Connect(client);
	std::jthread respond([&] {
		if (fixture.ReadRequest())
			(void)fixture.Write(Peer::Frame().span());
	});
	const auto result = client.Send(TestPacket{}, 1s);
	ASSERT_FALSE(result);
	ASSERT_TRUE(*result.error() == Client::Error::Cancelled);
	ASSERT_TRUE(client.Status() == Connection::Status::Disconnected);
	return 0;
}

int test_disconnect_unblocks_legacy_read() {
	TestClient client;
	Peer fixture;
	fixture.Connect(client);
	PacketPointer result;
	std::jthread send([&] { result = client.Send(TestPacket{}); });
	const bool read = fixture.ReadRequest();
	client.Disconnect();
	send.join();
	ASSERT_TRUE(read);
	ASSERT_FALSE(result);
	ASSERT_TRUE(client.Status() == Connection::Status::Disconnected);
	return 0;
}

// -------------------
// Deadline
// -------------------

int test_callback_time_counts_toward_deadline() {
	TestClient client(nullptr, 100ms);
	Peer fixture;
	fixture.Connect(client);
	std::jthread respond([&] {
		if (fixture.ReadRequest())
			(void)fixture.Write(Peer::Frame().span());
	});
	const auto result = client.Send(TestPacket{}, 50ms);
	ASSERT_FALSE(result);
	ASSERT_TRUE(*result.error() == Client::Error::TimedOut);
	ASSERT_TRUE(client.Status() == Connection::Status::Disconnected);
	return 0;
}

int test_connect_handshake_total_deadline() {
	TestClient client;
	ASSERT_TRUE(TLSFixture::Configure(client));
	Peer fixture;
	const auto start = std::chrono::steady_clock::now();
	const auto result = client.Connect(Connection::Protocol::IPv4, "127.0.0.1", fixture.port, 80ms);
	ASSERT_FALSE(result);
	ASSERT_TRUE(*result.error() == Client::Error::TimedOut);
	ASSERT_TRUE(std::chrono::steady_clock::now() - start >= 60ms);
	ASSERT_TRUE(std::chrono::steady_clock::now() - start < 500ms);
	ASSERT_TRUE(client.Status() == Connection::Status::Disconnected);
	return 0;
}

#ifdef LINUX
int test_connect_native_total_deadline() {
	Peer fixture;
	ASSERT_EQUAL(0, ::listen(fixture.listener, 0));
	TestClient queued;
	ASSERT_TRUE(queued.Connect(Connection::Protocol::IPv4, "127.0.0.1", fixture.port, 1s));
	TestClient client;
	const auto start = std::chrono::steady_clock::now();
	const auto result = client.Connect(Connection::Protocol::IPv4, "127.0.0.1", fixture.port, 70ms);
	ASSERT_FALSE(result);
	ASSERT_TRUE(*result.error() == Client::Error::TimedOut);
	ASSERT_TRUE(std::chrono::steady_clock::now() - start < 400ms);
	return 0;
}
#endif

int test_dribble_header_total_deadline() {
	TestClient client;
	Peer fixture;
	fixture.Connect(client);
	std::jthread dribble([&] {
		if (!fixture.ReadRequest())
			return;
		const auto frame = Peer::Frame();
		for (const auto& byte: frame.span()) {
			if (!fixture.Write(std::span{&byte, 1}))
				break;
			std::this_thread::sleep_for(35ms);
		}
	});
	const auto start = std::chrono::steady_clock::now();
	const auto result = client.Send(TestPacket{}, 120ms);
	const auto elapsed = std::chrono::steady_clock::now() - start;
	ASSERT_FALSE(result);
	ASSERT_TRUE(*result.error() == Client::Error::TimedOut);
	ASSERT_TRUE(elapsed >= 90ms && elapsed < 400ms);
	ASSERT_TRUE(client.Status() == Connection::Status::Disconnected);
	return 0;
}

int test_header_payload_share_deadline() {
	TestClient client;
	Peer fixture;
	fixture.Connect(client);
	std::jthread dribble([&] {
		if (!fixture.ReadRequest())
			return;
		const auto frame = Peer::Frame(1);
		std::this_thread::sleep_for(75ms);
		if (!fixture.Write(frame.span().first(static_cast<std::size_t>(frame.size()) - 1)))
			return;
		std::this_thread::sleep_for(75ms);
		(void)fixture.Write(frame.span().last(1));
	});
	const auto start = std::chrono::steady_clock::now();
	const auto result = client.Send(TestPacket{}, 120ms);
	const auto elapsed = std::chrono::steady_clock::now() - start;
	ASSERT_FALSE(result);
	ASSERT_TRUE(*result.error() == Client::Error::TimedOut);
	ASSERT_TRUE(elapsed >= 90ms && elapsed < 400ms);
	return 0;
}

int test_queued_request_deadline_does_not_wait_for_active_request() {
	TestClient client;
	Peer fixture;
	fixture.Connect(client);
	Expected<PacketPointer, Client::Error> active;
	std::jthread send([&] { active = client.Send(TestPacket{}, 2s); });
	const bool read = fixture.ReadRequest();
	const auto start = std::chrono::steady_clock::now();
	const auto queued = client.Send(TestPacket{}, 40ms);
	const auto elapsed = std::chrono::steady_clock::now() - start;
	client.Cancel();
	send.join();
	ASSERT_TRUE(read);
	ASSERT_FALSE(queued);
	ASSERT_TRUE(*queued.error() == Client::Error::TimedOut);
	ASSERT_TRUE(elapsed >= 30ms && elapsed < 300ms);
	ASSERT_FALSE(active);
	ASSERT_TRUE(*active.error() == Client::Error::Cancelled);
	return 0;
}

int test_send_backpressure_deadline() {
	for (const bool tls: {false, true}) {
		TestClient client;
		// A non-reading peer with a fixed small window guarantees native send backpressure.
		Peer fixture(16 * 1024);
		if (tls)
			fixture.ConnectTLS(client);
		else
			fixture.Connect(client);
		const TestPacket large(ByteSize{16 * 1024 * 1024});
		// Framing 16 MiB is synchronous pipeline work (about 0.1 s on Linux, slower on
		// Windows CI) that runs before socket I/O; the budget must outlast it so the
		// deadline expires while waiting for send capacity.
		const auto start = std::chrono::steady_clock::now();
		const auto result = client.Send(large, 2s);
		const auto elapsed = std::chrono::steady_clock::now() - start;
		ASSERT_FALSE(result);
		ASSERT_TRUE(*result.error() == Client::Error::TimedOut);
		ASSERT_TRUE(elapsed >= 2s && elapsed < 2400ms);
		ASSERT_TRUE(client.Status() == Connection::Status::Disconnected);
	}
	return 0;
}

int test_silent_server_deadline() {
	TestClient client;
	Peer fixture;
	fixture.Connect(client);
	const auto start = std::chrono::steady_clock::now();
	const auto result = client.Send(TestPacket{}, 70ms);
	ASSERT_FALSE(result);
	ASSERT_TRUE(*result.error() == Client::Error::TimedOut);
	ASSERT_TRUE(std::chrono::steady_clock::now() - start >= 50ms);
	ASSERT_TRUE(std::chrono::steady_clock::now() - start < 400ms);
	ASSERT_EQUAL(Size{1}, client.Telemetry()->RequestsWithoutResponse());
	return 0;
}

int test_silent_tls_server_deadline() {
	TestClient client;
	Peer fixture;
	fixture.ConnectTLS(client);
	const auto start = std::chrono::steady_clock::now();
	const auto result = client.Send(TestPacket{}, 70ms);
	ASSERT_FALSE(result);
	ASSERT_TRUE(*result.error() == Client::Error::TimedOut);
	ASSERT_TRUE(std::chrono::steady_clock::now() - start >= 50ms);
	ASSERT_TRUE(std::chrono::steady_clock::now() - start < 400ms);
	return 0;
}

int test_tls_dribble_total_deadline() {
	TestClient client;
	Peer fixture;
	fixture.ConnectTLS(client);
	const auto encrypted = fixture.Encrypt(Peer::Frame().span());
	std::jthread dribble([&] {
		for (const auto& byte: encrypted.span()) {
			if (!fixture.Write(std::span{&byte, 1}))
				break;
			std::this_thread::sleep_for(20ms);
		}
	});
	const auto start = std::chrono::steady_clock::now();
	const auto result = client.Send(TestPacket{}, 100ms);
	const auto elapsed = std::chrono::steady_clock::now() - start;
	ASSERT_FALSE(result);
	ASSERT_TRUE(*result.error() == Client::Error::TimedOut);
	ASSERT_TRUE(elapsed >= 80ms && elapsed < 400ms);
	return 0;
}

int test_tls_header_payload_share_deadline() {
	TestClient client;
	Peer fixture;
	fixture.ConnectTLS(client);
	const auto frame = Peer::Frame(1);
	const auto header = fixture.Encrypt(frame.span().first(static_cast<std::size_t>(frame.size()) - 1));
	const auto payload = fixture.Encrypt(frame.span().last(1));
	std::jthread dribble([&] {
		std::this_thread::sleep_for(75ms);
		if (!fixture.Write(header.span()))
			return;
		std::this_thread::sleep_for(75ms);
		(void)fixture.Write(payload.span());
	});
	const auto start = std::chrono::steady_clock::now();
	const auto result = client.Send(TestPacket{}, 120ms);
	const auto elapsed = std::chrono::steady_clock::now() - start;
	ASSERT_FALSE(result);
	ASSERT_TRUE(*result.error() == Client::Error::TimedOut);
	ASSERT_TRUE(elapsed >= 90ms && elapsed < 400ms);
	return 0;
}

// -------------------
// Lifecycle
// -------------------

int test_cancel_idle_move_and_reuse() {
	TestClient source;
	Peer fixture;
	fixture.Connect(source);
	TestClient destination(std::move(source));
	ASSERT_TRUE(destination.Status() == Connection::Status::Connected);
	ASSERT_TRUE(source.Status() == Connection::Status::Disconnected);
	source.Cancel();
	ASSERT_TRUE(destination.Status() == Connection::Status::Connected);
	destination.Cancel();
	Peer retry;
	retry.Connect(destination);
	TestClient assigned;
	assigned = std::move(destination);
	ASSERT_TRUE(assigned.Status() == Connection::Status::Connected);
	ASSERT_TRUE(destination.Status() == Connection::Status::Disconnected);
	Peer moved_retry;
	moved_retry.Connect(destination);
	return 0;
}

int test_invalid_timeout_and_zero_budget() {
	TestClient client;
	const auto invalid = client.Connect(Connection::Protocol::IPv4, "127.0.0.1", 1, -1ms);
	ASSERT_FALSE(invalid);
	ASSERT_TRUE(*invalid.error() == Client::Error::InvalidTimeout);
	const auto zero = client.Connect(Connection::Protocol::IPv4, "127.0.0.1", 1, 0ms);
	ASSERT_FALSE(zero);
	ASSERT_TRUE(*zero.error() == Client::Error::TimedOut);
	const auto offline = client.Send(TestPacket{}, 1s);
	ASSERT_FALSE(offline);
	ASSERT_TRUE(*offline.error() == Client::Error::NotConnected);
	Peer fixture;
	fixture.Connect(client);
	const auto expired = client.Send(TestPacket{}, 0ms);
	ASSERT_FALSE(expired);
	ASSERT_TRUE(*expired.error() == Client::Error::TimedOut);
	ASSERT_TRUE(client.Status() == Connection::Status::Connected);
	return 0;
}

int test_partial_header_is_transport_error() {
	TestClient client;
	Peer fixture;
	fixture.Connect(client);
	const auto frame = Peer::Frame();
	ASSERT_TRUE(fixture.Write(frame.span().first(1)));
	Peer::Close(fixture.peer);
	fixture.peer = static_cast<Peer::Handle>(-1);
	const auto result = client.Send(TestPacket{}, 1s);
	ASSERT_FALSE(result);
	ASSERT_TRUE(*result.error() == Client::Error::Transport);
	return 0;
}

int test_serialized_requests_keep_responses_isolated() {
	TestClient client;
	Peer fixture;
	fixture.Connect(client);
	std::jthread responder([&] {
		for (int index = 0; index < 2; ++index)
			if (fixture.ReadRequest())
				(void)fixture.Write(Peer::Frame().span());
	});
	Expected<PacketPointer, Client::Error> first;
	Expected<PacketPointer, Client::Error> second;
	std::jthread first_sender([&] { first = client.Send(TestPacket{}, 1s); });
	std::jthread second_sender([&] { second = client.Send(TestPacket{}, 1s); });
	first_sender.join();
	second_sender.join();
	ASSERT_TRUE(first);
	ASSERT_TRUE(second);
	ASSERT_EQUAL(Size{2}, client.Telemetry()->Responses());
	return 0;
}

int test_tls_failure_diagnostics() {
	TestClient client;
	ASSERT_TRUE(TLSFixture::Configure(client, "wrong.invalid"));
	Safe::String detail{"stale detail"};
	Crypto::TLS::ClientConfiguration invalid;
	ASSERT_FALSE(client.ConfigureTLS(invalid, detail));
	ASSERT_TRUE(std::string_view(detail) ==
		Crypto::TLS::DescribeSessionError(Crypto::TLS::SessionError::InvalidConfiguration));
	Peer fixture;
	std::jthread server([&] {
		fixture.Accept();
		try {
			fixture.Authenticate();
		} catch (const std::runtime_error&) {
			// A rejected certificate can abort the peer handshake.
		}
	});
	const auto result = client.Connect(Connection::Protocol::IPv4, "127.0.0.1", fixture.port, 2s, detail);
	server.join();
	ASSERT_FALSE(result);
	ASSERT_TRUE(*result.error() == Client::Error::TLS);
	ASSERT_FALSE(detail.empty());
	ASSERT_TRUE(std::string_view(detail) != Crypto::TLS::DescribeSessionError(Crypto::TLS::SessionError::PeerRejected));
	ASSERT_TRUE(client.Status() == Connection::Status::Disconnected);
	ASSERT_FALSE(client.Connect(Connection::Protocol::IPv4, "127.0.0.1", fixture.port, -1ms, detail));
	ASSERT_TRUE(detail.empty());
	return 0;
}

int main() {
#ifdef WINDOWS
	WSADATA data{};
	if (::WSAStartup(MAKEWORD(2, 2), &data) != 0)
		return 1;
#endif
	int failures = 0;
	// -------------------
	// Cancellation
	// -------------------
	failures += test_cancel_connect_handshake();
#ifdef LINUX
	failures += test_cancel_pending_native_connect();
#endif
	failures += test_cancel_read_and_reconnect();
	failures += test_cancel_reconnect_with_concurrent_status();
	failures += test_cancel_send();
	failures += test_cancel_tls_send();
	failures += test_disconnect_from_packet_callback();
	failures += test_disconnect_unblocks_legacy_read();
	// -------------------
	// Deadline
	// -------------------
	failures += test_callback_time_counts_toward_deadline();
	failures += test_connect_handshake_total_deadline();
#ifdef LINUX
	failures += test_connect_native_total_deadline();
#endif
	failures += test_dribble_header_total_deadline();
	failures += test_header_payload_share_deadline();
	failures += test_queued_request_deadline_does_not_wait_for_active_request();
	failures += test_send_backpressure_deadline();
	failures += test_silent_server_deadline();
	failures += test_silent_tls_server_deadline();
	failures += test_tls_dribble_total_deadline();
	failures += test_tls_header_payload_share_deadline();
	// -------------------
	// Lifecycle
	// -------------------
	failures += test_cancel_idle_move_and_reuse();
	failures += test_invalid_timeout_and_zero_budget();
	failures += test_partial_header_is_transport_error();
	failures += test_serialized_requests_keep_responses_isolated();
	failures += test_tls_failure_diagnostics();
#ifdef WINDOWS
	::WSACleanup();
#endif
	if (failures == 0)
#ifdef LINUX
		std::cout << "23 focused client control cases passed\n";
#else
		std::cout << "21 focused client control cases passed (Linux backlog tests not applicable)\n";
#endif
	return failures;
}
