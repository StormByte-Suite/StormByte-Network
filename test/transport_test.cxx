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
#include <StormByte/network/socket/client.hxx>
#include <StormByte/test_handlers.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <iostream>
#include <stdexcept>
#include <thread>
#include <vector>

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
 * @brief Release a test-owned native handle in this module.
 * @param handle Handle to close.
 */
void Close(Connection::HandlerType handle) {
#ifdef UNIX
	::close(handle);
#else
	::closesocket(handle);
#endif
}

/**
 * @brief Ephemeral loopback listener and native peer, entirely test-owned.
 */
struct STORMBYTE_NETWORK_PRIVATE PeerFixture {
	Safe::Shared<Logger::Log> logger = Safe::Shared<Logger::Log>::MakePointer<Logger::ThreadedLog>(
		std::cerr, Logger::Level::Error);
	Socket::Client client{Connection::Protocol::IPv4, logger};
	Connection::HandlerType listener = ::socket(AF_INET, SOCK_STREAM, 0);
	Connection::HandlerType peer = static_cast<Connection::HandlerType>(-1);
	unsigned short port = 0;

	explicit PeerFixture(bool listening = true) {
		sockaddr_in address{};
		address.sin_family = AF_INET;
		address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
		if (::bind(listener, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0 ||
			(listening && ::listen(listener, 4) != 0))
			throw std::runtime_error("loopback listener");
#ifdef UNIX
		socklen_t length = sizeof(address);
#else
		int length = sizeof(address);
#endif
		if (::getsockname(listener, reinterpret_cast<sockaddr*>(&address), &length) != 0)
			throw std::runtime_error("listener address");
		port = ntohs(address.sin_port);
		if (!listening) {
			Close(listener);
			listener = static_cast<Connection::HandlerType>(-1);
		}
	}

	~PeerFixture() {
		if (peer != static_cast<Connection::HandlerType>(-1))
			Close(peer);
		if (listener != static_cast<Connection::HandlerType>(-1))
			Close(listener);
	}

	void Connect() {
		if (!client.Connect("127.0.0.1", port))
			throw std::runtime_error("client connect");
		peer = ::accept(listener, nullptr, nullptr);
		if (peer == static_cast<Connection::HandlerType>(-1))
			throw std::runtime_error("peer accept");
	}

	void ConstrainSendBuffer() {
		const int capacity = 4096;
		if (::setsockopt(client.Handle(), SOL_SOCKET, SO_SNDBUF,
			reinterpret_cast<const char*>(&capacity), sizeof(capacity)) != 0)
			throw std::runtime_error("send capacity");
	}
};

Safe::Atomic<int> lookup_calls{0};
Safe::Atomic<int> lookup_completions{0};
Safe::Atomic<bool> release_lookup{false};

Expected<Connection::Info::Addresses, Network::Exception> BlockedLookup(
	std::string_view hostname, unsigned short port, Connection::AddressPolicy policy) noexcept {
	lookup_calls.fetch_add(1, Safe::MemoryOrder::AcqRel);
	while (!release_lookup.load(Safe::MemoryOrder::Acquire))
		std::this_thread::sleep_for(1ms);
	auto result = Connection::Info::ResolveAll(hostname, port, policy);
	lookup_completions.fetch_add(1, Safe::MemoryOrder::AcqRel);
	return result;
}

/**
 * @brief Deterministic slow native-provider stand-in; no external DNS is required.
 * @param hostname Borrowed request name.
 * @param port Destination port.
 * @param policy Requested address policy.
 * @return Native numeric alternatives after a controlled delay.
 */
Expected<Connection::Info::Addresses, Network::Exception> SlowLookup(
	std::string_view hostname, unsigned short port, Connection::AddressPolicy policy) noexcept {
	lookup_calls.fetch_add(1, Safe::MemoryOrder::AcqRel);
	std::this_thread::sleep_for(150ms);
	auto result = Connection::Info::ResolveAll(hostname, port, policy);
	lookup_completions.fetch_add(1, Safe::MemoryOrder::AcqRel);
	return result;
}

// -------------------
// Connection
// -------------------

int test_connect_deadline_cancel_and_retry() {
	PeerFixture fixture;
	auto addresses = Connection::Info::ResolveAll("127.0.0.1", fixture.port, Connection::AddressPolicy::IPv4Only);
	ASSERT_TRUE(addresses.has_value());
	Safe::Atomic<bool> cancelled{true};
	auto result = fixture.client.ConnectResolved(addresses.value(), {{}, &cancelled});
	ASSERT_TRUE(result.state == Socket::OperationState::Cancelled);
	ASSERT_TRUE(fixture.client.Status() == Connection::Status::Disconnected);
	result = fixture.client.ConnectResolved(addresses.value(), {std::chrono::steady_clock::now() - 1ms});
	ASSERT_TRUE(result.state == Socket::OperationState::TimedOut);
	ASSERT_TRUE(fixture.client.Status() == Connection::Status::Disconnected);
	Connection::Info::Addresses invalid;
	invalid.push_back({});
	result = fixture.client.ConnectResolved(invalid, {});
	ASSERT_TRUE(result.state == Socket::OperationState::Error);
	result = fixture.client.ConnectResolved(addresses.value(), {std::chrono::steady_clock::now() + 1s});
	ASSERT_TRUE(result.state == Socket::OperationState::Complete);
	result = fixture.client.ConnectResolved(addresses.value(), {});
	ASSERT_TRUE(result.state == Socket::OperationState::Error);
	ASSERT_TRUE(fixture.client.Status() == Connection::Status::Connected);
	return 0;
}

int test_connect_explicit_policy_preserves_family() {
	PeerFixture fixture;
	Socket::Client ipv6{Connection::Protocol::IPv6, fixture.logger};
	Connection::Resolver resolver;
	auto result = ipv6.Connect("127.0.0.1", fixture.port, resolver,
		{std::chrono::steady_clock::now() + 1s});
	ASSERT_TRUE(result.state == Socket::OperationState::Error);
	result = ipv6.Connect("127.0.0.1", fixture.port, resolver,
		{std::chrono::steady_clock::now() + 1s}, Connection::AddressPolicy::SystemOrder);
	ASSERT_TRUE(result.state == Socket::OperationState::Complete);
	ipv6.Disconnect();
	ASSERT_TRUE(!ipv6.Connect("127.0.0.1", fixture.port));
	ASSERT_TRUE(ipv6.Status() == Connection::Status::Disconnected);
	return 0;
}

int test_connect_failed_alternatives_then_legacy_retry() {
	PeerFixture fixture;
	PeerFixture refused(false);
	auto addresses = Connection::Info::ResolveAll("127.0.0.1", refused.port, Connection::AddressPolicy::IPv4Only);
	ASSERT_TRUE(addresses.has_value());
	const auto result = fixture.client.ConnectResolved(addresses.value(), {std::chrono::steady_clock::now() + 5s});
	ASSERT_TRUE(result.state == Socket::OperationState::Error && result.native_error != 0);
	ASSERT_TRUE(fixture.client.Status() == Connection::Status::Disconnected);
	ASSERT_TRUE(fixture.client.Handle() == static_cast<Connection::HandlerType>(-1));
	fixture.Connect();
	ASSERT_TRUE(fixture.client.Status() == Connection::Status::Connected);
	return 0;
}

int test_connect_fallback_and_io() {
	PeerFixture fixture;
	PeerFixture refused(false);
	auto addresses = Connection::Info::ResolveAll("127.0.0.1", refused.port, Connection::AddressPolicy::IPv4Only);
	auto reachable = Connection::Info::ResolveAll("127.0.0.1", fixture.port, Connection::AddressPolicy::IPv4Only);
	ASSERT_TRUE(addresses.has_value() && reachable.has_value());
	addresses->push_back((*reachable)[0]);
	const auto result = fixture.client.ConnectResolved(addresses.value(), {std::chrono::steady_clock::now() + 5s});
	ASSERT_TRUE(result.state == Socket::OperationState::Complete);
	fixture.peer = ::accept(fixture.listener, nullptr, nullptr);
	ASSERT_TRUE(fixture.peer != static_cast<Connection::HandlerType>(-1));
	const std::array<std::byte, 1> byte{std::byte{'x'}};
	ASSERT_TRUE(fixture.client.Send(byte, {}).bytes == ByteSize{1});
	char received = 0;
	ASSERT_TRUE(::recv(fixture.peer, &received, 1, 0) == 1 && received == 'x');
	return 0;
}

int test_connect_managed_resolution() {
	PeerFixture fixture;
	Connection::Resolver resolver;
	auto result = fixture.client.Connect("127.0.0.1", fixture.port, resolver,
		{std::chrono::steady_clock::now() + 1s});
	ASSERT_TRUE(result.state == Socket::OperationState::Complete);
	ASSERT_TRUE(fixture.client.Status() == Connection::Status::Connected);
	return 0;
}

#ifdef LINUX
int test_connect_pending_deadline_and_cancellation() {
	PeerFixture fixture;
	ASSERT_TRUE(::listen(fixture.listener, 0) == 0);
	Socket::Client queued{Connection::Protocol::IPv4, fixture.logger};
	ASSERT_TRUE(queued.Connect("127.0.0.1", fixture.port).has_value());
	auto addresses = Connection::Info::ResolveAll("127.0.0.1", fixture.port, Connection::AddressPolicy::IPv4Only);
	ASSERT_TRUE(addresses.has_value());
	auto start = std::chrono::steady_clock::now();
	auto result = fixture.client.ConnectResolved(addresses.value(), {start + 30ms});
	ASSERT_TRUE(result.state == Socket::OperationState::TimedOut);
	ASSERT_TRUE(std::chrono::steady_clock::now() - start < 1s);
	ASSERT_TRUE(fixture.client.Status() == Connection::Status::Disconnected);
	ASSERT_TRUE(fixture.client.Handle() == static_cast<Connection::HandlerType>(-1));
	Safe::Atomic<bool> cancelled{false};
	std::thread cancel([&] {
		std::this_thread::sleep_for(20ms);
		cancelled.store(true, Safe::MemoryOrder::Release);
	});
	start = std::chrono::steady_clock::now();
	result = fixture.client.ConnectResolved(addresses.value(), {start + 1s, &cancelled});
	cancel.join();
	ASSERT_TRUE(result.state == Socket::OperationState::Cancelled);
	ASSERT_TRUE(std::chrono::steady_clock::now() - start < 1s);
	ASSERT_TRUE(fixture.client.Status() == Connection::Status::Disconnected);
	fixture.peer = ::accept(fixture.listener, nullptr, nullptr);
	ASSERT_TRUE(fixture.peer != static_cast<Connection::HandlerType>(-1));
	result = fixture.client.ConnectResolved(addresses.value(), {std::chrono::steady_clock::now() + 1s});
	ASSERT_TRUE(result.state == Socket::OperationState::Complete);
	return 0;
}
#endif

// -------------------
// Partial I/O
// -------------------

int test_disconnect_without_fixed_delay() {
	std::chrono::steady_clock::duration elapsed{};
	for (int index = 0; index < 3; ++index) {
		PeerFixture fixture;
		fixture.Connect();
		const auto start = std::chrono::steady_clock::now();
		fixture.client.Disconnect();
		elapsed += std::chrono::steady_clock::now() - start;
	}
	ASSERT_TRUE(elapsed < 200ms);
	return 0;
}

int test_interrupt_and_close_are_serialized() {
	for (int index = 0; index < 32; ++index) {
		PeerFixture fixture;
		fixture.Connect();
		Safe::Atomic<bool> done{false};
		std::jthread interrupt([&] {
			while (!done.load(Safe::MemoryOrder::Acquire))
				fixture.client.Interrupt();
		});
		fixture.client.Disconnect();
		done.store(true, Safe::MemoryOrder::Release);
		interrupt.join();
		ASSERT_TRUE(fixture.client.Status() == Connection::Status::Disconnected);
		ASSERT_TRUE(fixture.client.Handle() == static_cast<Connection::HandlerType>(-1));
	}
	return 0;
}

int test_interrupt_wakes_read_without_releasing_handle() {
	PeerFixture fixture;
	fixture.Connect();
	const auto handle = fixture.client.Handle();
	Safe::Atomic<bool> cancelled{false};
	Socket::OperationResult result;
	const auto start = std::chrono::steady_clock::now();
	std::jthread receive([&] {
		result = Socket::WaitReadable(handle, {start + 2s, &cancelled});
	});
	std::this_thread::sleep_for(100ms);
	// Same order as Client::Cancel(): publish cancellation, then shut down natively.
	const auto cancel = std::chrono::steady_clock::now();
	cancelled.store(true, Safe::MemoryOrder::Release);
	fixture.client.Interrupt();
	receive.join();
	ASSERT_TRUE(result.state == Socket::OperationState::Cancelled);
	ASSERT_TRUE(std::chrono::steady_clock::now() - cancel < 500ms);
	ASSERT_TRUE(fixture.client.Handle() == handle);
	std::array<std::byte, 1> bytes{};
	const auto read = fixture.client.ReadSome(bytes);
#ifdef WINDOWS
	// Winsock rejects receives after SD_BOTH instead of reporting EOF.
	ASSERT_TRUE(read.state == Socket::OperationState::Error && read.native_error == WSAESHUTDOWN);
#else
	ASSERT_TRUE(read.state == Socket::OperationState::PeerClosed);
#endif
	return 0;
}

#ifdef UNIX
/**
 * @brief POSIX poll reports a local shutdown; Windows select does not, so it is not portable.
 */
int test_posix_shutdown_wakes_uncancelled_wait() {
	PeerFixture fixture;
	fixture.Connect();
	const auto handle = fixture.client.Handle();
	Socket::OperationResult result;
	const auto start = std::chrono::steady_clock::now();
	std::jthread receive([&] {
		result = Socket::WaitReadable(handle, {start + 2s});
	});
	fixture.client.Interrupt();
	receive.join();
	ASSERT_TRUE(result.state == Socket::OperationState::Complete);
	ASSERT_TRUE(std::chrono::steady_clock::now() - start < 500ms);
	ASSERT_TRUE(fixture.client.Handle() == handle);
	return 0;
}
#endif

int test_partial_read_states() {
	PeerFixture fixture;
	std::array<std::byte, 8> buffer{};
	ASSERT_TRUE(fixture.client.ReadSome(buffer).state == Socket::OperationState::Error);
	fixture.Connect();
	ASSERT_TRUE(fixture.client.ReadSome({}).state == Socket::OperationState::Error);
	ASSERT_TRUE(fixture.client.ReadSome(buffer).state == Socket::OperationState::WouldBlock);
	ASSERT_TRUE(::send(fixture.peer, "abc", 3, 0) == 3);
	Socket::OperationResult result;
	for (int attempt = 0; attempt < 100; ++attempt) {
		result = fixture.client.ReadSome(buffer);
		if (result.state != Socket::OperationState::WouldBlock)
			break;
		std::this_thread::sleep_for(1ms);
	}
	ASSERT_TRUE(result.state == Socket::OperationState::Complete);
	ASSERT_TRUE(result.bytes == ByteSize{3});
	ASSERT_TRUE(buffer[0] == std::byte{'a'} && buffer[2] == std::byte{'c'});
#ifdef UNIX
	::shutdown(fixture.peer, SHUT_WR);
#else
	::shutdown(fixture.peer, SD_SEND);
#endif
	for (int attempt = 0; attempt < 100; ++attempt) {
		result = fixture.client.ReadSome(buffer);
		if (result.state != Socket::OperationState::WouldBlock)
			break;
		std::this_thread::sleep_for(1ms);
	}
	ASSERT_TRUE(result.state == Socket::OperationState::PeerClosed);
	ASSERT_TRUE(result.bytes == ByteSize{0});
	return 0;
}

int test_partial_read_transport_error() {
	PeerFixture fixture;
	fixture.Connect();
	const linger reset{1, 0};
	ASSERT_TRUE(::setsockopt(fixture.peer, SOL_SOCKET, SO_LINGER,
		reinterpret_cast<const char*>(&reset), sizeof(reset)) == 0);
	Close(fixture.peer);
	fixture.peer = static_cast<Connection::HandlerType>(-1);
	std::array<std::byte, 8> buffer{};
	Socket::OperationResult result;
	for (int attempt = 0; attempt < 100; ++attempt) {
		result = fixture.client.ReadSome(buffer);
		if (result.state != Socket::OperationState::WouldBlock)
			break;
		std::this_thread::sleep_for(1ms);
	}
	ASSERT_TRUE(result.state == Socket::OperationState::Error && result.native_error != 0);
	ASSERT_TRUE(result.bytes == ByteSize{0});
	result = fixture.client.Send(buffer, {std::chrono::steady_clock::now() + 1s});
	ASSERT_TRUE(result.state == Socket::OperationState::Error && result.native_error != 0);
	ASSERT_TRUE(result.bytes == ByteSize{0});
	return 0;
}

int test_send_cancelled_with_progress() {
	PeerFixture fixture;
	fixture.Connect();
	fixture.ConstrainSendBuffer();
	std::vector<std::byte> data(32 * 1024 * 1024, std::byte{42});
	Safe::Atomic<bool> cancelled{false};
	std::thread cancel([&] {
		std::this_thread::sleep_for(40ms);
		cancelled.store(true, Safe::MemoryOrder::Release);
	});
	const auto start = std::chrono::steady_clock::now();
	const auto result = fixture.client.Send(data, {start + 2s, &cancelled});
	cancel.join();
	if (result.state != Socket::OperationState::Cancelled)
		std::cerr << "test_send_cancelled_with_progress: state " << static_cast<int>(result.state)
			<< ", error " << result.native_error << ", bytes " << static_cast<std::size_t>(result.bytes) << '\n';
	ASSERT_TRUE(result.state == Socket::OperationState::Cancelled);
	ASSERT_TRUE(result.bytes > ByteSize{0} && result.bytes < ByteSize{data.size()});
	ASSERT_TRUE(std::chrono::steady_clock::now() - start < 1s);
	return 0;
}

int test_send_deadline_with_progress() {
	PeerFixture fixture;
	fixture.Connect();
	fixture.ConstrainSendBuffer();
	std::vector<std::byte> data(32 * 1024 * 1024, std::byte{42});
	const auto start = std::chrono::steady_clock::now();
	const auto result = fixture.client.Send(data, {start + 30ms});
	if (result.state != Socket::OperationState::TimedOut)
		std::cerr << "test_send_deadline_with_progress: state " << static_cast<int>(result.state)
			<< ", error " << result.native_error << ", bytes " << static_cast<std::size_t>(result.bytes) << '\n';
	ASSERT_TRUE(result.state == Socket::OperationState::TimedOut);
	ASSERT_TRUE(result.bytes > ByteSize{0} && result.bytes < ByteSize{data.size()});
	ASSERT_TRUE(std::chrono::steady_clock::now() - start < 1s);
	ASSERT_TRUE(fixture.client.Status() == Connection::Status::Connected);
	std::array<std::byte, 4096> received{};
	std::size_t remaining = static_cast<std::size_t>(result.bytes);
	while (remaining > 0) {
		const int count = static_cast<int>(::recv(fixture.peer, reinterpret_cast<char*>(received.data()),
			static_cast<int>(std::min(remaining, received.size())), 0));
		ASSERT_TRUE(count > 0);
		for (int index = 0; index < count; ++index)
			ASSERT_TRUE(received[static_cast<std::size_t>(index)] == std::byte{42});
		remaining -= static_cast<std::size_t>(count);
	}
	return 0;
}

int test_send_success_and_preflight() {
	PeerFixture fixture;
	fixture.Connect();
	const std::array<std::byte, 3> data{std::byte{'a'}, std::byte{'b'}, std::byte{'c'}};
	Safe::Atomic<bool> cancelled{true};
	auto result = fixture.client.Send(data, {std::chrono::steady_clock::now() - 1s, &cancelled});
	ASSERT_TRUE(result.state == Socket::OperationState::Cancelled && result.bytes == ByteSize{0});
	result = fixture.client.Send(data, {std::chrono::steady_clock::now() - 1s});
	ASSERT_TRUE(result.state == Socket::OperationState::TimedOut && result.bytes == ByteSize{0});
	result = fixture.client.Send(data, {std::chrono::steady_clock::now() + 1s});
	ASSERT_TRUE(result.state == Socket::OperationState::Complete && result.bytes == ByteSize{3});
	std::array<char, 3> received{};
	ASSERT_TRUE(::recv(fixture.peer, received.data(), 3, 0) == 3);
	ASSERT_TRUE(received[0] == 'a' && received[2] == 'c');
	return 0;
}

// -------------------
// Resolution
// -------------------

int test_resolution_cancelled_connect_and_cleanup() {
	lookup_calls.store(0, Safe::MemoryOrder::Release);
	lookup_completions.store(0, Safe::MemoryOrder::Release);
	release_lookup.store(false, Safe::MemoryOrder::Release);
	PeerFixture fixture;
	Safe::Atomic<bool> cancelled{false};
	Socket::OperationResult result;
	const auto start = std::chrono::steady_clock::now();
	{
		Connection::Resolver resolver(BlockedLookup);
		std::thread cancel([&] {
			while (lookup_calls.load(Safe::MemoryOrder::Acquire) == 0 &&
				std::chrono::steady_clock::now() < start + 1s)
				std::this_thread::sleep_for(1ms);
			std::this_thread::sleep_for(20ms);
			cancelled.store(true, Safe::MemoryOrder::Release);
		});
		result = fixture.client.Connect("127.0.0.1", fixture.port, resolver, {start + 1s, &cancelled});
		const auto completed_before_release = lookup_completions.load(Safe::MemoryOrder::Acquire);
		release_lookup.store(true, Safe::MemoryOrder::Release);
		cancel.join();
		ASSERT_TRUE(result.state == Socket::OperationState::Cancelled);
		ASSERT_TRUE(completed_before_release == 0);
		ASSERT_TRUE(fixture.client.Status() == Connection::Status::Disconnected);
	}
	ASSERT_TRUE(lookup_calls.load(Safe::MemoryOrder::Acquire) == 1);
	ASSERT_TRUE(lookup_completions.load(Safe::MemoryOrder::Acquire) == 1);
	return 0;
}

int test_resolution_deadline_busy_and_reuse() {
	lookup_calls.store(0, Safe::MemoryOrder::Release);
	lookup_completions.store(0, Safe::MemoryOrder::Release);
	Connection::Resolver resolver(SlowLookup);
	Socket::OperationState state;
	const auto start = std::chrono::steady_clock::now();
	auto result = resolver.Resolve("127.0.0.1", 1234, Connection::AddressPolicy::IPv4Only,
		{start + 10ms}, state);
	ASSERT_TRUE(!result && state == Socket::OperationState::TimedOut);
	ASSERT_TRUE(std::chrono::steady_clock::now() - start < 100ms);
	for (int attempt = 0; attempt < 20; ++attempt) {
		result = resolver.Resolve("127.0.0.1", 1234, Connection::AddressPolicy::IPv4Only, {}, state);
		ASSERT_TRUE(!result && state == Socket::OperationState::Busy);
	}
	ASSERT_TRUE(lookup_calls.load(Safe::MemoryOrder::Acquire) == 1);
	const auto reuse_deadline = std::chrono::steady_clock::now() + 1s;
	do {
		result = resolver.Resolve("127.0.0.1", 1234, Connection::AddressPolicy::IPv4Only,
			{reuse_deadline}, state);
		if (result || state != Socket::OperationState::Busy)
			break;
		std::this_thread::sleep_for(10ms);
	} while (std::chrono::steady_clock::now() < reuse_deadline);
	ASSERT_TRUE(result.has_value() && state == Socket::OperationState::Complete);
	ASSERT_TRUE(lookup_calls.load(Safe::MemoryOrder::Acquire) == 2);
	ASSERT_TRUE(lookup_completions.load(Safe::MemoryOrder::Acquire) == 2);
	return 0;
}

int test_resolution_family_policy_and_addresses() {
	using Policy = Connection::AddressPolicy;
	auto ipv4 = Connection::Info::ResolveAll("127.0.0.1", 1234, Policy::IPv4Only);
	auto ipv6 = Connection::Info::ResolveAll("::1", 1234, Policy::IPv6Only);
	ASSERT_TRUE(ipv4.has_value() && ipv6.has_value());
	ASSERT_TRUE((*ipv4)[0]->sa_family == AF_INET && (*ipv6)[0]->sa_family == AF_INET6);
	auto info = Connection::Info::FromSockAddr((*ipv6)[0]);
	ASSERT_TRUE(info.has_value() && info->Port() == 1234);
	ASSERT_TRUE(!Connection::Info::ResolveAll("::1", 1234, Policy::IPv4Only));
	ASSERT_TRUE(!Connection::Info::ResolveAll(std::string_view{"bad\0host", 8}, 1234, Policy::SystemOrder));
	ASSERT_TRUE(!Connection::Info::ResolveAll("localhost", 1234, static_cast<Policy>(-1)));
	for (auto policy: {Policy::SystemOrder, Policy::PreferIPv4, Policy::PreferIPv6}) {
		auto addresses = Connection::Info::ResolveAll("localhost", 1234, policy);
		ASSERT_TRUE(addresses.has_value() && !addresses->empty());
		bool deferred = false;
		const int preferred = policy == Policy::PreferIPv4 ? AF_INET : AF_INET6;
		for (const auto& address: addresses.value()) {
			auto peer = Connection::Info::FromSockAddr(address);
			ASSERT_TRUE(peer.has_value() && peer->Port() == 1234);
			if (policy != Policy::SystemOrder) {
				if (address->sa_family != preferred)
					deferred = true;
				else
					ASSERT_TRUE(!deferred);
			}
		}
	}
	return 0;
}

int test_resolution_preflight_and_error() {
	Connection::Resolver resolver;
	Socket::OperationState state;
	Safe::Atomic<bool> cancelled{true};
	auto result = resolver.Resolve("127.0.0.1", 1234, Connection::AddressPolicy::IPv4Only,
		{{}, &cancelled}, state);
	ASSERT_TRUE(!result && state == Socket::OperationState::Cancelled);
	result = resolver.Resolve("127.0.0.1", 1234, Connection::AddressPolicy::IPv4Only,
		{std::chrono::steady_clock::now() - 1ms}, state);
	ASSERT_TRUE(!result && state == Socket::OperationState::TimedOut);
	result = resolver.Resolve("::1", 1234, Connection::AddressPolicy::IPv4Only,
		{std::chrono::steady_clock::now() + 1s}, state);
	ASSERT_TRUE(!result && state == Socket::OperationState::Error);
	return 0;
}

int main() {
	int failures = 0;
	// -------------------
	// Connection
	// -------------------
	failures += test_connect_deadline_cancel_and_retry();
	failures += test_connect_explicit_policy_preserves_family();
	failures += test_connect_failed_alternatives_then_legacy_retry();
	failures += test_connect_fallback_and_io();
	failures += test_connect_managed_resolution();
#ifdef LINUX
	failures += test_connect_pending_deadline_and_cancellation();
#endif
	// -------------------
	// Partial I/O
	// -------------------
	failures += test_disconnect_without_fixed_delay();
	failures += test_interrupt_and_close_are_serialized();
	failures += test_interrupt_wakes_read_without_releasing_handle();
	failures += test_partial_read_states();
	failures += test_partial_read_transport_error();
#ifdef UNIX
	failures += test_posix_shutdown_wakes_uncancelled_wait();
#endif
	failures += test_send_cancelled_with_progress();
	failures += test_send_deadline_with_progress();
	failures += test_send_success_and_preflight();
	// -------------------
	// Resolution
	// -------------------
	failures += test_resolution_cancelled_connect_and_cleanup();
	failures += test_resolution_deadline_busy_and_reuse();
	failures += test_resolution_family_policy_and_addresses();
	failures += test_resolution_preflight_and_error();
	if (failures == 0)
#ifdef LINUX
		std::cout << "19 focused transport cases passed\n";
#elif defined(UNIX)
		std::cout << "18 focused transport cases passed (Linux backlog test not applicable)\n";
#else
		std::cout << "17 focused transport cases passed (Linux backlog and POSIX shutdown-wake tests not applicable)\n";
#endif
	return failures;
}
