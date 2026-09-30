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

#include <StormByte/network/socket/server.hxx>
#include <StormByte/network/socket/client.hxx>
#ifdef UNIX
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <poll.h>
#else
#include <winsock2.h>
#endif
#include <StormByte/network/connection/handler.hxx>
#include <algorithm>
#include <memory>
using namespace StormByte::Network;
Socket::Server::Server(const Connection::Protocol& protocol, StormByte::Shared<Logger::Log> logger) noexcept:
Socket(protocol, logger) {
	m_logger << Logger::Level::LowLevel << "Created server socket with UUID: " << std::string_view{m_UUID} << std::endl;
}

Socket::Server::~Server() noexcept = default;

ExpectedVoid Socket::Server::Listen(std::string_view hostname, const unsigned short& port) noexcept {
	if (Connection::IsConnected(m_status.load(std::memory_order_acquire)))
		return Unexpected<ConnectionError>("Server is already connected");
	m_status.store(Connection::Status::Connecting, std::memory_order_release);
	auto expected_socket = CreateSocket();
	if (!expected_socket)
		return Unexpected(expected_socket.error());
	m_handle = expected_socket.value();
	int opt = 1;
#ifdef WINDOWS
	{
		BOOL exclusive = TRUE;
		if (setsockopt(m_handle, SOL_SOCKET, SO_EXCLUSIVEADDRUSE,
				reinterpret_cast<const char*>(&exclusive), sizeof(exclusive)) == SOCKET_ERROR) {
			m_status.store(Connection::Status::Disconnected, std::memory_order_release);
			m_handle = INVALID_SOCKET;
			return Unexpected<ConnectionError>("Failed to set SO_EXCLUSIVEADDRUSE: {} (error code: {})",
				Connection::Handler::Instance().LastError(),
				Connection::Handler::Instance().LastErrorCode());
		}
	}
#else
	if (setsockopt(m_handle, SOL_SOCKET, SO_REUSEADDR,
			reinterpret_cast<const char*>(&opt), sizeof(opt)) < 0) {
		m_status.store(Connection::Status::Disconnected, std::memory_order_release);
		m_handle = -1;
		return Unexpected<ConnectionError>("Failed to set socket options: {} (error code: {})",
			Connection::Handler::Instance().LastError(),
			Connection::Handler::Instance().LastErrorCode());
	}
#endif
	auto expected_connection_info = Connection::Info::FromHost(hostname, port, m_protocol);
	if (!expected_connection_info)
		return Unexpected<ConnectionError>(expected_connection_info.error()->what());
	m_conn_info = std::make_unique<Connection::Info>(std::move(expected_connection_info.value()));
	auto bind_result = ::bind(m_handle, m_conn_info->SockAddr().get(), sizeof(*m_conn_info->SockAddr()));
	if (bind_result == -1) {
		m_status.store(Connection::Status::Disconnected, std::memory_order_release);
#ifdef WINDOWS
		m_handle = INVALID_SOCKET;
#else
		m_handle = -1;
#endif
		return Unexpected<ConnectionError>("Failed to bind socket: {} (error code: {})",
			Connection::Handler::Instance().LastError(),
			Connection::Handler::Instance().LastErrorCode());
	}

	auto listen_result = ::listen(m_handle, SOMAXCONN);
	if (listen_result == -1) {
		m_status.store(Connection::Status::Disconnected, std::memory_order_release);
#ifdef WINDOWS
		m_handle = INVALID_SOCKET;
#else
		m_handle = -1;
#endif
		return Unexpected<ConnectionError>("Failed to listen on socket: {} (error code: {})",
			Connection::Handler::Instance().LastError(),
			Connection::Handler::Instance().LastErrorCode());
	}

	struct sockaddr_storage bound_address{};
#ifdef WINDOWS
	int bound_address_size = sizeof(bound_address);
#else
	socklen_t bound_address_size = sizeof(bound_address);
#endif
	if (::getsockname(m_handle, reinterpret_cast<struct sockaddr*>(&bound_address), &bound_address_size) == -1) {
		m_status.store(Connection::Status::Disconnected, std::memory_order_release);
		Disconnect();
		return Unexpected<ConnectionError>("Failed to query bound socket port: {} (error code: {})",
			Connection::Handler::Instance().LastError(),
			Connection::Handler::Instance().LastErrorCode());
	}

	if (bound_address.ss_family == AF_INET) {
		m_port = ntohs(reinterpret_cast<const struct sockaddr_in*>(&bound_address)->sin_port);
	} else if (bound_address.ss_family == AF_INET6) {
		m_port = ntohs(reinterpret_cast<const struct sockaddr_in6*>(&bound_address)->sin6_port);
	} else {
		m_status.store(Connection::Status::Disconnected, std::memory_order_release);
		Disconnect();
		return Unexpected<ConnectionError>("Listener bound to an unsupported address family");
	}

	InitializeAfterConnect();
	m_logger << Logger::Level::LowLevel << "Server listening on " << hostname << ":" << m_port << std::endl;
	return {};
}

unsigned short Socket::Server::Port() const noexcept {
	return m_port;
}

ExpectedClient Socket::Server::Accept() noexcept {
	if (!Connection::IsConnected(m_status.load(std::memory_order_acquire)))
		return Unexpected<ConnectionError>("Socket is not connected");
#ifdef UNIX
	struct pollfd pfd;
	pfd.fd = m_handle;
	pfd.events = POLLIN;
	int pr = poll(&pfd, 1, 200);
	if (pr == 0) {
		return Unexpected<ConnectionError>("Timeout occurred while waiting to accept connection.");
	} else if (pr < 0) {
		return Unexpected<ConnectionError>("Error during poll.");
	}
#else
	fd_set read_fds;
	FD_ZERO(&read_fds);
	FD_SET(m_handle, &read_fds);
	struct timeval timeout = {0, 200000};
	int select_result = select(0, &read_fds, nullptr, nullptr, &timeout);
	if (select_result == 0) {
		return Unexpected<ConnectionError>("Timeout occurred while waiting to accept connection.");
	} else if (select_result < 0) {
		return Unexpected<ConnectionError>("Error during select.");
	}
#endif
	Connection::HandlerType client_handle = ::accept(m_handle, nullptr, nullptr);
#ifdef WINDOWS
	if (client_handle == INVALID_SOCKET) {
#else
	if (client_handle == -1) {
#endif
		return Unexpected<ConnectionError>("Failed to accept client connection.");
	}

	Client client_socket(m_protocol, m_logger);
	client_socket.m_handle = client_handle;
	client_socket.InitializeAfterConnect();
	m_active_clients.push_back(std::make_shared<Client>(std::move(client_socket)));
	return m_active_clients.back();
}

void Socket::Server::Disconnect() noexcept {
	for (auto& client : m_active_clients) {
		if (!client) continue;
		client->Disconnect();
	}

	m_active_clients.clear();
	Socket::Disconnect();
}

void Socket::Server::DisconnectClient(std::string_view client_uuid) noexcept {
	auto it = std::find_if(m_active_clients.begin(), m_active_clients.end(),
		[&client_uuid](const std::shared_ptr<Client>& client) {
			return client && client->UUID() == client_uuid;
		});
	if (it != m_active_clients.end()) {
		(*it)->Disconnect();
		m_active_clients.erase(it);
	}
}
