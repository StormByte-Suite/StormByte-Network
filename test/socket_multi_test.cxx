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

// Combined multi-client server/client test
#include <StormByte/buffer/fifo.hxx>
#include <StormByte/logger/threaded_log.hxx>
#include <StormByte/network/connection/handler.hxx>
#include <StormByte/network/socket/server.hxx>
#include <StormByte/network/socket/client.hxx>
#include <StormByte/network/packet.hxx>
#include <StormByte/test_handlers.h>
#include <atomic>
#include <csignal>
#include <cstdlib>
#include <iostream>
#include <thread>
#include <vector>
#include <memory>
using namespace StormByte;
using namespace StormByte::Network;
// Configuration
constexpr const char* HOST = "127.0.0.1";
constexpr const unsigned short PORT = 7070;
// Number of concurrent clients (global constant)
constexpr const std::size_t N = 40;
// Chunk size for each send (bytes)
constexpr const std::size_t CHUNK_SIZE = 4 * 1024 * 1024; // 4 MiB
// Total bytes each client will send (bytes)
constexpr const std::size_t TOTAL_BYTES_PER_CLIENT = 6 * 1024 + 53; // 6.053 KiB
auto logger = Logger::ThreadedLog(std::cout, Logger::Level::Error, "[%L] [T%i] %T:");
int TestSocketMulti() {
    const std::string fn_name = "TestSocketMulti";
	const std::string send_data(TOTAL_BYTES_PER_CLIENT, 'H');
    std::atomic<bool> stop{false};
    std::atomic<std::size_t> clients_finished{0};
    std::atomic<int> failures{0};
    // Containers to capture thread return codes (0 = ok, non-zero = failure)
    std::vector<int> handler_results;
    std::vector<int> client_results;
    // Server thread
    std::thread server_thread([&]() -> int {
        Socket::Server server(Protocol::IPv4, logger);
        auto listen_res = server.Listen(HOST, PORT);
        ASSERT_TRUE(fn_name, listen_res.has_value());
        std::vector<std::thread> client_handlers;
        while (!stop.load()) {
            auto expected_client = server.Accept();
            if (!expected_client) {
                // Accept may time out; check stop flag and continue
                continue;
            }

            Socket::Client client_socket = std::move(expected_client.value());
            // Handler thread per client
            handler_results.emplace_back(0);
            const std::size_t handler_idx = handler_results.size() - 1;
            auto worker = [client = std::move(client_socket), &clients_finished]() mutable -> int {
                const std::string fn_name_inner = "TestSocketMulti::Handler";
                // Ensure clients_finished is incremented on any exit path from this handler
                struct FinishedGuard {
                    std::atomic<std::size_t> &counter;
                    FinishedGuard(std::atomic<std::size_t> &c) noexcept : counter(c) {}

                    ~FinishedGuard() { counter.fetch_add(1); }
                } guard(clients_finished);
                // Accumulate until we've received the full payload from this client
                StormByte::BinaryData accumulated;
                std::size_t total_received = 0;
                while (total_received < TOTAL_BYTES_PER_CLIENT) {
                    auto recv_res = client.Receive(TOTAL_BYTES_PER_CLIENT - total_received);
                    ASSERT_TRUE(fn_name_inner, recv_res.has_value());
                    Buffer::FIFO buffer = std::move(recv_res.value());
                    auto expected_data = buffer.Extract();
                    ASSERT_TRUE(fn_name_inner, expected_data.has_value());
                    auto chunk = expected_data.value();
                    ASSERT_FALSE(fn_name_inner, chunk.empty());
                    accumulated.insert(accumulated.end(), chunk.begin(), chunk.end());
                    total_received = accumulated.size();
                }

                // Echo back the accumulated payload
                if (!accumulated.empty()) {
                    auto send_res = client.Send(std::span<const std::byte>(accumulated.data(), accumulated.size()));
                    ASSERT_TRUE(fn_name_inner, send_res.has_value());
                }

                client.Disconnect();
                return 0;
            };
            client_handlers.emplace_back([worker = std::move(worker), &handler_results, handler_idx]() mutable {
                handler_results[handler_idx] = worker();
            });
        }

        // Join handler threads
        for (auto &t : client_handlers) if (t.joinable()) t.join();
        if (Connection::IsConnected(server.Status())) {
            server.Disconnect();
        }

        return 0;
    });
    // Small delay to ensure server is listening
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    // Client threads
    std::vector<std::thread> clients;
    client_results.reserve(N);
    for (std::size_t i = 0; i < N; ++i) {
        client_results.emplace_back(0);
        const std::size_t client_idx = client_results.size() - 1;
        auto worker = [i, &clients_finished, &send_data]() -> int {
            const std::string fn_name_inner = "TestSocketMulti::Client";
            Socket::Client client(Protocol::IPv4, logger);
            auto connect_res = client.Connect(HOST, PORT);
            ASSERT_TRUE(fn_name_inner, connect_res.has_value());
            // Send `send_data` in CHUNK_SIZE slices
            std::size_t sent = 0;
            while (sent < TOTAL_BYTES_PER_CLIENT) {
                std::size_t to_send = std::min(CHUNK_SIZE, TOTAL_BYTES_PER_CLIENT - sent);
                auto span = std::span<const std::byte>(reinterpret_cast<const std::byte*>(send_data.data() + sent), to_send);
                auto res = client.Send(span);
                ASSERT_TRUE(fn_name_inner, res.has_value());
                sent += to_send;
            }

            // Receive echoed data and accumulate into a std::string
            std::string received;
            received.reserve(TOTAL_BYTES_PER_CLIENT);
            while (received.size() < TOTAL_BYTES_PER_CLIENT) {
                auto recv_buf_res = client.Receive(TOTAL_BYTES_PER_CLIENT - received.size());
                ASSERT_TRUE(fn_name_inner, recv_buf_res.has_value());
                Buffer::FIFO buffer = std::move(recv_buf_res.value());
                auto expected_data = buffer.Extract();
                ASSERT_TRUE(fn_name_inner, expected_data.has_value());
                auto vec = expected_data.value();
                received.append(reinterpret_cast<const char*>(vec.data()), vec.size());
            }

            // Simple equality check
            ASSERT_TRUE(fn_name_inner, received == send_data);
            client.Disconnect();
            return 0;
        };
        clients.emplace_back([worker = std::move(worker), &client_results, client_idx]() mutable {
            client_results[client_idx] = worker();
        });
    }

    // Wait for all clients to finish
    for (auto &t : clients) if (t.joinable()) t.join();
    // Aggregate client results into failures
    for (auto v : client_results) if (v != 0) failures.fetch_add(1);
    // Signal server to stop accepting (it will finish handling existing clients)
    stop.store(true);
    // Wait until server observed all clients finished
    while (clients_finished.load() < N) {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }

    if (server_thread.joinable()) server_thread.join();
    // Aggregate handler results into failures
    for (auto v : handler_results) if (v != 0) failures.fetch_add(1);
    // Return 1 if any thread reported failures, 0 otherwise
    RETURN_TEST(fn_name, failures.load() ? 1 : 0);
}

int TestReceiveTimeout() {
    const std::string fn_name = "TestReceiveTimeout";
    const std::string send_data(1024, 'T'); // 1 KiB payload
    Socket::Server server(Protocol::IPv4, logger);
    auto listen_res = server.Listen(HOST, PORT);
    ASSERT_TRUE(fn_name, listen_res.has_value());
    // Server accept + handler thread
    std::thread server_thread([&]() -> int {
        auto expected_client = server.Accept();
        ASSERT_TRUE(fn_name, expected_client.has_value());
        Socket::Client client_socket = std::move(expected_client.value());
        // Read exact payload
        auto recv_res = client_socket.Receive(send_data.size());
        ASSERT_TRUE(fn_name, recv_res.has_value());
        Buffer::FIFO buffer = std::move(recv_res.value());
        auto expected_data = buffer.Extract();
        ASSERT_TRUE(fn_name, expected_data.has_value());
        // Echo back
        auto &vec = expected_data.value();
        auto span = std::span<const std::byte>(reinterpret_cast<const std::byte*>(vec.data()), vec.size());
        auto send_res = client_socket.Send(span);
        ASSERT_TRUE(fn_name, send_res.has_value());
        // Attempt to receive more bytes with 1 second timeout -> expect timeout (no data)
        auto extra = client_socket.Receive(10, 1);
        ASSERT_FALSE(fn_name, extra.has_value());
        client_socket.Disconnect();
        return 0;
    });
    // Small delay to ensure server is listening
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    // Client side
    Socket::Client client(Protocol::IPv4, logger);
    auto connect_res = client.Connect(HOST, PORT);
    ASSERT_TRUE(fn_name, connect_res.has_value());
    // Send payload
    auto span = std::span<const std::byte>(reinterpret_cast<const std::byte*>(send_data.data()), send_data.size());
    auto send_res = client.Send(span);
    ASSERT_TRUE(fn_name, send_res.has_value());
    // Receive echoed payload
    std::string received;
    received.reserve(send_data.size());
    while (received.size() < send_data.size()) {
        auto recv_buf_res = client.Receive(send_data.size() - received.size());
        ASSERT_TRUE(fn_name, recv_buf_res.has_value());
        Buffer::FIFO buffer = std::move(recv_buf_res.value());
        auto expected_data = buffer.Extract();
        ASSERT_TRUE(fn_name, expected_data.has_value());
        auto vec = expected_data.value();
        received.append(reinterpret_cast<const char*>(vec.data()), vec.size());
    }

    ASSERT_TRUE(fn_name, received == send_data);
    // Now attempt an extra receive with 1 second timeout - expect timeout (no more data)
    auto extra_recv = client.Receive(10, 1);
    ASSERT_FALSE(fn_name, extra_recv.has_value());
    client.Disconnect();
    if (server_thread.joinable()) server_thread.join();
    RETURN_TEST(fn_name, 0);
}

int main() {
    int result = 0;
    result += TestSocketMulti();
    result += TestReceiveTimeout();
    if (result == 0) {
        std::cout << "All tests passed!" << std::endl;
    } else {
        std::cout << result << " tests failed." << std::endl;
    }

    return result;
}
