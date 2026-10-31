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
struct PeerFixture {
	Safe::Shared<Logger::Log> logger = Safe::Shared<Logger::Log>::MakePointer<Logger::ThreadedLog>(
		std::cerr, Logger::Level::Error);
	Socket::Client client{Connection::Protocol::IPv4, logger};
	Connection::HandlerType listener = ::socket(AF_INET, SOCK_STREAM, 0);
	Connection::HandlerType peer = static_cast<Connection::HandlerType>(-1);
	unsigned short port = 0;

	PeerFixture() {
		sockaddr_in address{};
		address.sin_family = AF_INET;
		address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
		if (::bind(listener, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0 ||
			::listen(listener, 4) != 0)
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

	~PeerFixture() {
		if (peer != static_cast<Connection::HandlerType>(-1))
			Close(peer);
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

// -------------------
// Partial I/O
// -------------------

int test_partial_read_states() {
	PeerFixture fixture;
	std::array<std::byte, 8> buffer{};
	ASSERT_TRUE(fixture.client.ReadSome(buffer).state == Socket::OperationState::Error);
	fixture.Connect();
	ASSERT_TRUE(fixture.client.ReadSome(buffer).state == Socket::OperationState::WouldBlock);
	ASSERT_TRUE(::send(fixture.peer, "abc", 3, 0) == 3);
	auto result = fixture.client.ReadSome(buffer);
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

int main() {
	int failures = 0;
	// -------------------
	// Partial I/O
	// -------------------
	failures += test_partial_read_states();
	failures += test_send_cancelled_with_progress();
	failures += test_send_deadline_with_progress();
	failures += test_send_success_and_preflight();
	return failures;
}
