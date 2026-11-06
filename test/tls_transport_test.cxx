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
#include <StormByte/network/socket/tls.hxx>
#include <StormByte/test_handlers.h>

#include <algorithm>
#include <array>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <thread>
#include <vector>

#ifdef UNIX
#include <arpa/inet.h>
#include <fcntl.h>
#include <unistd.h>
#else
#include <winsock2.h>
#include <ws2tcpip.h>
#endif

using namespace StormByte;
using namespace StormByte::Network;
using namespace std::chrono_literals;
namespace CryptoTLS = StormByte::Crypto::TLS;
namespace X509 = StormByte::Crypto::X509;

/**
 * @brief Throw a test diagnostic instead of hiding peer-thread errors.
 * @param success Expected successful condition.
 * @param message Failure diagnostic.
 */
void Require(bool success, const char* message) {
	if (!success)
		throw std::runtime_error(message);
}

bool HandshakeSucceeded(Socket::Client& socket, CryptoTLS::Session& session,
	const Socket::OperationControl& control) {
	const auto result = Socket::TLS::Handshake(socket, session, control);
	if (result.state != Socket::OperationState::Complete)
		std::cerr << "TLS handshake failed: state=" << static_cast<int>(result.state)
			<< ", native_error=" << result.native_error
			<< ", tls_failure=" << result.tls_failure
			<< ", tls_error=" << static_cast<int>(result.error) << std::endl;
	return result.state == Socket::OperationState::Complete;
}

/**
 * @brief Load offline, deliberately disposable fixture bytes.
 * @param name Fixture file name.
 * @return PEM bytes; no production key or trust material is used.
 */
std::vector<std::byte> LoadFixture(const char* name) {
	std::ifstream file(std::string(TLS_FIXTURE_DIRECTORY) + "/" + name, std::ios::binary);
	Require(file.is_open(), "TLS fixture missing");
	const std::string text{std::istreambuf_iterator<char>(file), {}};
	Require(!file.bad(), "TLS fixture read failure");
	const auto bytes = std::as_bytes(std::span(text));
	return {bytes.begin(), bytes.end()};
}

/**
 * @brief Dispose a test-owned native handle.
 * @param handle Native test handle.
 */
void Close(Connection::HandlerType handle) {
#ifdef UNIX
	::close(handle);
#else
	::closesocket(handle);
#endif
}

/**
 * @brief Native loopback peer with a fragmented Crypto TLS server, without OpenSSL linkage.
 */
struct STORMBYTE_NETWORK_PRIVATE Fixture {
	std::vector<std::byte> ca_pem = LoadFixture("ca.pem");
	std::vector<std::byte> server_pem = LoadFixture("server.pem");
	std::vector<std::byte> key_pem = LoadFixture("server-test-key.pem");
	X509::Certificate ca = X509::Load(ca_pem).value();
	X509::Certificate leaf = X509::Load(server_pem).value();
	const std::array<std::byte, 12> alpn{
		std::byte{2}, std::byte{'h'}, std::byte{'2'}, std::byte{8}, std::byte{'h'},
		std::byte{'t'}, std::byte{'t'}, std::byte{'p'}, std::byte{'/'},
		std::byte{'1'}, std::byte{'.'}, std::byte{'1'}};
	std::span<const std::byte> server_protocols = alpn;
	std::string_view expected_protocol = "h2";
	Safe::Shared<Logger::Log> logger = Safe::Shared<Logger::Log>::MakePointer<Logger::ThreadedLog>(
		std::cerr, Logger::Level::Error);
	Socket::Client socket{Connection::Protocol::IPv4, logger};
	Connection::HandlerType peer = static_cast<Connection::HandlerType>(-1);
	std::exception_ptr peer_error;
	std::jthread worker;

	/**
	 * @brief Connect to an ephemeral listener and constrain native send capacity.
	 */
	Fixture() {
		const auto listener = ::socket(AF_INET, SOCK_STREAM, 0);
		Require(listener != static_cast<Connection::HandlerType>(-1), "listener creation");
		sockaddr_in address{};
		address.sin_family = AF_INET;
		address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
		const auto bound = ::bind(listener, reinterpret_cast<sockaddr*>(&address), sizeof(address));
		const auto listening = ::listen(listener, 1);
#ifdef UNIX
		socklen_t length = sizeof(address);
#else
		int length = sizeof(address);
#endif
		const auto named = ::getsockname(listener, reinterpret_cast<sockaddr*>(&address), &length);
		if (bound != 0 || listening != 0 || named != 0) {
			Close(listener);
			throw std::runtime_error("listener initialization");
		}
		Connection::Resolver resolver;
		const auto connected = socket.Connect("127.0.0.1", ntohs(address.sin_port), resolver,
			{std::chrono::steady_clock::now() + 2s});
		if (connected.state == Socket::OperationState::Complete)
			peer = ::accept(listener, nullptr, nullptr);
		Close(listener);
		Require(peer != static_cast<Connection::HandlerType>(-1), "loopback connection");
#ifdef UNIX
		Require(::fcntl(peer, F_SETFL, O_NONBLOCK) == 0, "nonblocking peer");
#else
		u_long enabled = 1;
		Require(::ioctlsocket(peer, FIONBIO, &enabled) == 0, "nonblocking peer");
#endif
#ifdef MACOS
		const int suppress_sigpipe = 1;
		Require(::setsockopt(peer, SOL_SOCKET, SO_NOSIGPIPE,
			reinterpret_cast<const char*>(&suppress_sigpipe), sizeof(suppress_sigpipe)) == 0,
			"peer SIGPIPE suppression");
#endif
		const int capacity = 4096;
		Require(::setsockopt(socket.Handle(), SOL_SOCKET, SO_SNDBUF,
			reinterpret_cast<const char*>(&capacity), sizeof(capacity)) == 0, "constrained send buffer");
	}

	/**
	 * @brief Stop I/O, join the bounded peer and release native resources.
	 */
	~Fixture() {
		socket.Disconnect();
		if (worker.joinable())
			worker.join();
		if (peer != static_cast<Connection::HandlerType>(-1))
			Close(peer);
	}

	Fixture(const Fixture&) = delete;
	Fixture(Fixture&&) = delete;
	Fixture& operator=(const Fixture&) = delete;
	Fixture& operator=(Fixture&&) = delete;

	/**
	 * @brief Configure authentication at a fixed instant inside fixture validity.
	 * @param hostname Required server DNS SAN.
	 * @param bundled Use production roots to test rejection of the disposable CA.
	 * @return Configured Crypto client.
	 */
	CryptoTLS::Session Client(std::string_view hostname = "localhost", bool bundled = false) {
		CryptoTLS::ClientConfiguration configuration;
		configuration.identity = X509::SubjectAltName(X509::SubjectAltNameType::DNS,
			Safe::Binary(std::as_bytes(std::span(hostname))));
		configuration.trust_source = bundled ? CryptoTLS::TrustSource::Bundled : CryptoTLS::TrustSource::Custom;
		if (!bundled)
			configuration.trust_anchors = std::span(&ca, 1);
		configuration.validation_time = leaf.NotBefore() + 60s;
		configuration.alpn_wire = alpn;
		auto result = CryptoTLS::Session::CreateClient(configuration);
		Require(result.has_value(), "client configuration");
		return std::move(*result);
	}

	/**
	 * @brief Start a bounded native peer worker.
	 * @param echo_size Plaintext bytes to receive and echo; zero means no echo.
	 * @param truncate Close TCP without close-notify after authentication.
	 * @param tolerate_abort Expect client-side rejection or abandonment.
	 * @param stall Stop reading after authentication to exercise backpressure.
	 */
	void Start(std::size_t echo_size, bool truncate = false, bool tolerate_abort = false, bool stall = false) {
		worker = std::jthread([this, echo_size, truncate, tolerate_abort, stall] {
			try {
				Run(echo_size, truncate, tolerate_abort, stall);
			}
			catch (...) {
				peer_error = std::current_exception();
			}
		});
	}

	/**
	 * @brief Join and surface any unexpected peer-thread failure.
	 */
	void Join() {
		worker.join();
		if (peer_error)
			std::rethrow_exception(peer_error);
	}

	/**
	 * @brief Drive a fragmented TLS peer using only Crypto and native socket I/O.
	 * @param echo_size Expected application payload size.
	 * @param truncate Simulate truncated TCP EOF.
	 * @param tolerate_abort Accept the intentionally tested client's abandonment.
	 * @param stall Hold a 200ms receive pause for deterministic 50ms client deadlines.
	 */
	void Run(std::size_t echo_size, bool truncate, bool tolerate_abort, bool stall) {
		CryptoTLS::ServerConfiguration configuration;
		configuration.certificate_chain_pem = server_pem;
		configuration.private_key_pem = key_pem;
		configuration.alpn_wire = server_protocols;
		auto created = CryptoTLS::Session::CreateServer(configuration);
		Require(created.has_value(), "server configuration");
		auto session = std::move(*created);
		const Socket::OperationControl control{std::chrono::steady_clock::now() + 5s};
		std::vector<std::byte> plaintext;
		bool response_sent = false;
		std::array<std::byte, 307> buffer{};
		for (;;) {
			std::array<std::byte, 4096> encrypted{};
			for (;;) {
				auto drained = session.DrainEncrypted(encrypted);
				if (!drained) {
					Require(tolerate_abort, "server TLS drain failure");
					return;
				}
				if (*drained == 0)
					break;
				auto pending = std::span(encrypted).first(*drained);
				while (!pending.empty()) {
					Require(Socket::WaitWritable(peer, control).state == Socket::OperationState::Complete,
						"peer write deadline");
#ifdef LINUX
					const int flags = MSG_NOSIGNAL;
#else
					const int flags = 0;
#endif
					const auto sent = ::send(peer, reinterpret_cast<const char*>(pending.data()),
						static_cast<int>(pending.size()), flags);
					if (sent <= 0) {
						Require(tolerate_abort, "peer write failure");
						return;
					}
					pending = pending.subspan(static_cast<std::size_t>(sent));
				}
			}
			if (session.HandshakeComplete()) {
				Require(session.NegotiatedALPN() == expected_protocol, "peer ALPN");
				if (stall) {
					std::this_thread::sleep_for(200ms);
					return;
				}
				if (truncate) {
					Close(peer);
					peer = static_cast<Connection::HandlerType>(-1);
					return;
				}
				for (;;) {
					auto read = session.ReadPlaintext(buffer);
					if (!read) {
						Require(tolerate_abort, "peer plaintext failure");
						return;
					}
					if (*read == 0)
						break;
					plaintext.insert(plaintext.end(), buffer.begin(), buffer.begin() + *read);
				}
				if (!response_sent && echo_size != 0 && plaintext.size() == echo_size) {
					auto remaining = std::span<const std::byte>(plaintext);
					while (!remaining.empty()) {
						auto written = session.WritePlaintext(remaining);
						Require(written.has_value() && *written != 0, "peer echo encryption");
						remaining = remaining.subspan(*written);
					}
					response_sent = true;
					Require(session.Shutdown().has_value(), "peer shutdown");
					continue;
				}
			}
			if (session.Status() == CryptoTLS::State::Closed)
				return;
			Require(Socket::WaitReadable(peer, control).state == Socket::OperationState::Complete,
				"peer receive deadline");
			const auto received = ::recv(peer, reinterpret_cast<char*>(buffer.data()),
				static_cast<int>(buffer.size()), 0);
			if (received <= 0) {
				Require(tolerate_abort, "unexpected TCP EOF");
				return;
			}
			auto fed = session.FeedEncrypted(std::span(buffer).first(static_cast<std::size_t>(received)));
			if (!fed) {
				Require(tolerate_abort, "peer ciphertext failure");
				return;
			}
			Require(*fed == static_cast<std::size_t>(received), "peer partial feed");
		}
	}
};

// -------------------
// Handshake
// -------------------

int test_handshake_alpn_http1_and_no_selection() {
	for (const bool absent : {false, true}) {
		Fixture fixture;
		fixture.server_protocols = absent ? std::span<const std::byte>{} : std::span(fixture.alpn).subspan(3);
		fixture.expected_protocol = absent ? "" : "http/1.1";
		auto session = fixture.Client();
		fixture.Start(0, false, true, true);
		ASSERT_TRUE(HandshakeSucceeded(fixture.socket, session,
			{std::chrono::steady_clock::now() + 2s}));
		ASSERT_TRUE(session.NegotiatedALPN() == fixture.expected_protocol);
		fixture.socket.Disconnect();
		fixture.Join();
	}
	return 0;
}

int test_handshake_cancellation_and_expiry() {
	for (const bool cancelled : {false, true}) {
		Fixture fixture;
		auto session = fixture.Client();
		Safe::Atomic<bool> cancellation{false};
		std::jthread cancel;
		if (cancelled)
			cancel = std::jthread([&] { std::this_thread::sleep_for(20ms); cancellation.store(true); });
		const auto start = std::chrono::steady_clock::now();
		const auto result = Socket::TLS::Handshake(fixture.socket, session,
			{start + (cancelled ? 2s : 100ms), &cancellation});
		if (result.state != (cancelled ? Socket::OperationState::Cancelled : Socket::OperationState::TimedOut))
			std::cerr << "Handshake cancellation/expiry: cancelled=" << cancelled
				<< ", state=" << static_cast<int>(result.state)
				<< ", native_error=" << result.native_error << std::endl;
		ASSERT_TRUE(result.state == (cancelled ? Socket::OperationState::Cancelled : Socket::OperationState::TimedOut));
		ASSERT_TRUE(std::chrono::steady_clock::now() - start < 500ms);
		ASSERT_TRUE(fixture.socket.Status() == Connection::Status::Disconnected);
		ASSERT_TRUE(session.Status() == CryptoTLS::State::Failed);
	}
	return 0;
}

int test_handshake_rejects_identity_and_untrusted_root() {
	for (const bool bundled : {false, true}) {
		Fixture fixture;
		auto session = fixture.Client(bundled ? "localhost" : "wrong.example", bundled);
		fixture.Start(0, false, true);
		const auto result = Socket::TLS::Handshake(fixture.socket, session,
			{std::chrono::steady_clock::now() + 2s});
		ASSERT_TRUE(result.state == Socket::OperationState::Error && result.tls_failure);
		ASSERT_TRUE(result.error == CryptoTLS::SessionError::PeerRejected);
		ASSERT_FALSE(result.detail.empty());
		ASSERT_TRUE(std::string_view(result.detail) != CryptoTLS::DescribeSessionError(result.error));
		ASSERT_TRUE(std::string_view(result.detail) != session.FailureDetail());
		ASSERT_TRUE(fixture.socket.Status() == Connection::Status::Disconnected);
		fixture.Join();
	}
	return 0;
}

int test_handshake_rejects_invalid_records() {
	Fixture fixture;
	auto session = fixture.Client();
	constexpr std::string_view invalid = "Not a TLS record";
	ASSERT_TRUE(::send(fixture.peer, invalid.data(), static_cast<int>(invalid.size()), 0)
		== static_cast<int>(invalid.size()));
	const auto result = Socket::TLS::Handshake(fixture.socket, session,
		{std::chrono::steady_clock::now() + 1s});
	ASSERT_TRUE(result.state == Socket::OperationState::Error && result.tls_failure);
	ASSERT_FALSE(result.detail.empty());
	ASSERT_TRUE(fixture.socket.Status() == Connection::Status::Disconnected);
	return 0;
}

// -------------------
// Transfer
// -------------------

int test_transfer_cancelled_operations_abandon_stream() {
	for (const int operation : {0, 1, 2}) {
		Fixture fixture;
		auto session = fixture.Client();
		fixture.Start(0, false, true, true);
		ASSERT_TRUE(HandshakeSucceeded(fixture.socket, session,
			{std::chrono::steady_clock::now() + 2s}));
		Safe::Atomic<bool> cancelled{true};
		const Socket::OperationControl control{std::chrono::steady_clock::now() - 1ms, &cancelled};
		std::array<std::byte, 1> buffer{};
		Socket::TLS::Result result;
		if (operation == 0)
			result = Socket::TLS::Read(fixture.socket, session, buffer, control);
		else if (operation == 1)
			result = Socket::TLS::Write(fixture.socket, session, buffer, control);
		else
			result = Socket::TLS::Shutdown(fixture.socket, session, control);
		ASSERT_TRUE(result.state == Socket::OperationState::Cancelled);
		ASSERT_TRUE(fixture.socket.Status() == Connection::Status::Disconnected);
		ASSERT_TRUE(session.Status() == CryptoTLS::State::Failed);
		fixture.Join();
	}
	return 0;
}

int test_transfer_fragmented_echo_alpn_and_shutdown() {
	Fixture fixture;
	auto session = fixture.Client();
	std::vector<std::byte> payload(512 * 1024);
	for (std::size_t index = 0; index < payload.size(); ++index)
		payload[index] = static_cast<std::byte>(index % 251);
	fixture.Start(payload.size());
	const Socket::OperationControl control{std::chrono::steady_clock::now() + 5s};
	ASSERT_TRUE(HandshakeSucceeded(fixture.socket, session, control));
	ASSERT_TRUE(session.NegotiatedALPN() == "h2");
	const auto sent = Socket::TLS::Write(fixture.socket, session, payload, control);
	ASSERT_TRUE(sent.state == Socket::OperationState::Complete && sent.bytes == ByteSize{payload.size()});
	std::vector<std::byte> response(payload.size());
	std::size_t offset = 0;
	while (offset < response.size()) {
		auto read = Socket::TLS::Read(fixture.socket, session, std::span(response).subspan(offset), control);
		ASSERT_TRUE(read.state == Socket::OperationState::Complete && read.bytes != ByteSize{0});
		offset += static_cast<std::size_t>(read.bytes);
	}
	ASSERT_TRUE(response == payload);
	std::array<std::byte, 1> extra{};
	ASSERT_TRUE(Socket::TLS::Read(fixture.socket, session, extra, control).state == Socket::OperationState::PeerClosed);
	ASSERT_TRUE(Socket::TLS::Shutdown(fixture.socket, session, control).state == Socket::OperationState::Complete);
	ASSERT_TRUE(fixture.socket.Status() == Connection::Status::Disconnected);
	fixture.Join();
	return 0;
}

int test_transfer_invalid_state_and_empty_destination() {
	for (const int operation : {0, 1, 2, 3, 4}) {
		Fixture fixture;
		auto session = fixture.Client();
		std::array<std::byte, 1> bytes{};
		if (operation == 0) {
			fixture.socket.Disconnect();
			const auto result = Socket::TLS::Handshake(fixture.socket, session, {});
			ASSERT_TRUE(result.state == Socket::OperationState::Error);
		}
		else if (operation == 1) {
			const auto result = Socket::TLS::Write(fixture.socket, session, bytes, {});
			ASSERT_TRUE(result.state == Socket::OperationState::Error && result.tls_failure);
		}
		else if (operation == 2) {
			const auto result = Socket::TLS::Read(fixture.socket, session, bytes, {});
			ASSERT_TRUE(result.state == Socket::OperationState::Error && result.tls_failure);
		}
		else if (operation == 3) {
			const auto result = Socket::TLS::Shutdown(fixture.socket, session, {});
			ASSERT_TRUE(result.state == Socket::OperationState::Error && result.tls_failure);
		}
		else {
			fixture.Start(0, false, true, true);
			ASSERT_TRUE(HandshakeSucceeded(fixture.socket, session,
				{std::chrono::steady_clock::now() + 2s}));
			const auto result = Socket::TLS::Read(fixture.socket, session, {}, {});
			ASSERT_TRUE(result.state == Socket::OperationState::Error && result.tls_failure);
			fixture.Join();
		}
		ASSERT_TRUE(fixture.socket.Status() == Connection::Status::Disconnected);
		ASSERT_TRUE(session.Status() == CryptoTLS::State::Failed);
	}
	return 0;
}

int test_transfer_read_write_and_shutdown_deadlines() {
	for (const int operation : {0, 1, 2}) {
		Fixture fixture;
		auto session = fixture.Client();
		fixture.Start(0, false, true, true);
		ASSERT_TRUE(HandshakeSucceeded(fixture.socket, session,
			{std::chrono::steady_clock::now() + 2s}));
		// The deadline measures I/O, not allocation of the large write buffer.
		std::vector<std::byte> bytes(operation == 1 ? 8 * 1024 * 1024 : 1, std::byte{42});
		const auto start = std::chrono::steady_clock::now();
		const Socket::OperationControl control{start + 50ms};
		Socket::TLS::Result result;
		if (operation == 0)
			result = Socket::TLS::Read(fixture.socket, session, bytes, control);
		else if (operation == 1)
			result = Socket::TLS::Write(fixture.socket, session, bytes, control);
		else
			result = Socket::TLS::Shutdown(fixture.socket, session, control);
		ASSERT_TRUE(result.state == Socket::OperationState::TimedOut);
		ASSERT_TRUE(std::chrono::steady_clock::now() - start < 500ms);
		if (operation == 1) {
			ASSERT_TRUE(result.bytes > ByteSize{0});
			ASSERT_TRUE(result.bytes < ByteSize{bytes.size()});
		}
		ASSERT_TRUE(fixture.socket.Status() == Connection::Status::Disconnected);
		ASSERT_TRUE(session.Status() == CryptoTLS::State::Failed);
		fixture.Join();
	}
	return 0;
}

int test_transfer_rejects_truncated_tcp_eof() {
	Fixture fixture;
	auto session = fixture.Client();
	fixture.Start(0, true);
	const Socket::OperationControl control{std::chrono::steady_clock::now() + 2s};
	ASSERT_TRUE(HandshakeSucceeded(fixture.socket, session, control));
	std::array<std::byte, 1> output{};
	const auto result = Socket::TLS::Read(fixture.socket, session, output, control);
	ASSERT_TRUE(result.state == Socket::OperationState::Error && result.truncated);
	ASSERT_TRUE(std::string_view(result.detail).find("close-notify") != std::string_view::npos);
	ASSERT_TRUE(!result.tls_failure);
	ASSERT_TRUE(fixture.socket.Status() == Connection::Status::Disconnected);
	fixture.Join();
	return 0;
}

int main() {
	int result = 0;

	// -------------------
	// Handshake
	// -------------------

	result += test_handshake_alpn_http1_and_no_selection();
	result += test_handshake_cancellation_and_expiry();
	result += test_handshake_rejects_identity_and_untrusted_root();
	result += test_handshake_rejects_invalid_records();

	// -------------------
	// Transfer
	// -------------------

	result += test_transfer_cancelled_operations_abandon_stream();
	result += test_transfer_fragmented_echo_alpn_and_shutdown();
	result += test_transfer_invalid_state_and_empty_destination();
	result += test_transfer_read_write_and_shutdown_deadlines();
	result += test_transfer_rejects_truncated_tcp_eof();
	if (result == 0)
		std::cout << "9 focused TLS transport cases passed\n";
	return result;
}
