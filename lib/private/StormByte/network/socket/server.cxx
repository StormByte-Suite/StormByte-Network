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

#include <StormByte/network/connection/handler.hxx>
#include <StormByte/network/socket/client.hxx>
#include <StormByte/network/socket/server.hxx>

#include <algorithm>
#include <utility>

#ifdef UNIX
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <poll.h>
#else
#include <winsock2.h>
#include <ws2tcpip.h>
#endif
using namespace StormByte;
using namespace StormByte::Network;

Socket::Server::Server(const Connection::Protocol& protocol, StormByte::Safe::Shared<Logger::Log> logger):
	Socket(protocol, std::move(logger)) {
	m_logger << Logger::Level::LowLevel << "Created server socket with UUID: " << std::string_view{m_UUID} << std::endl;
}

Socket::Server::~Server() noexcept {
	Disconnect();
}

Socket::Server::Server(Server&& other) noexcept:
	Socket(std::move(other)),
	m_active_clients(std::move(other.m_active_clients)),
	m_port(std::exchange(other.m_port, 0)) {}

Socket::Server& Socket::Server::operator=(Server&& other) noexcept {
	if (this != &other) {
		Disconnect();
		Socket::operator=(std::move(other));
		m_active_clients = std::move(other.m_active_clients);
		m_port = std::exchange(other.m_port, 0);
	}
	return *this;
}

ExpectedVoid Socket::Server::Listen(std::string_view hostname, const unsigned short& port) noexcept {
	if (Connection::IsConnected(m_status.load(Safe::MemoryOrder::Acquire)))
		return Unexpected<ConnectionError>("Server is already connected");
	Disconnect();
	auto fail = [this](ExpectedVoid error) noexcept -> ExpectedVoid {
		EnsureIsClosed();
		m_conn_info.reset();
		m_port = 0;
		m_status.store(Connection::Status::Disconnected, Safe::MemoryOrder::Release);
		return error;
	};
	try {
		m_status.store(Connection::Status::Connecting, Safe::MemoryOrder::Release);
		auto expected_socket = CreateSocket();
		if (!expected_socket)
			return fail(Unexpected(expected_socket.error()));
		m_handle = expected_socket.value();
#ifdef WINDOWS
		{
			BOOL exclusive = TRUE;
			if (setsockopt(m_handle, SOL_SOCKET, SO_EXCLUSIVEADDRUSE,
					reinterpret_cast<const char*>(&exclusive), static_cast<int>(sizeof(exclusive))) == SOCKET_ERROR) {
				return fail(Unexpected<ConnectionError>("Failed to set SO_EXCLUSIVEADDRUSE: {} (error code: {})",
					std::string_view{Connection::Handler::Instance().LastError()},
					Connection::Handler::Instance().LastErrorCode()));
			}
		}
#else
		int opt = 1;
		if (setsockopt(m_handle, SOL_SOCKET, SO_REUSEADDR,
				reinterpret_cast<const char*>(&opt), static_cast<socklen_t>(sizeof(opt))) < 0) {
			return fail(Unexpected<ConnectionError>("Failed to set socket options: {} (error code: {})",
				std::string_view{Connection::Handler::Instance().LastError()},
				Connection::Handler::Instance().LastErrorCode()));
		}
#endif
		auto expected_connection_info = Connection::Info::FromHost(hostname, port, m_protocol);
		if (!expected_connection_info)
			return fail(Unexpected<ConnectionError>(expected_connection_info.error()->what()));
		m_conn_info = Safe::MakeUnique<Connection::Info>(std::move(expected_connection_info.value()));
		auto bind_result = ::bind(m_handle, m_conn_info->SockAddr().get(),
#ifdef WINDOWS
			static_cast<int>(m_conn_info->SockAddrSize()));
#else
			static_cast<socklen_t>(m_conn_info->SockAddrSize()));
#endif
		if (bind_result == -1) {
			return fail(Unexpected<ConnectionError>("Failed to bind socket: {} (error code: {})",
				std::string_view{Connection::Handler::Instance().LastError()},
				Connection::Handler::Instance().LastErrorCode()));
		}

		auto listen_result = ::listen(m_handle, SOMAXCONN);
		if (listen_result == -1) {
			return fail(Unexpected<ConnectionError>("Failed to listen on socket: {} (error code: {})",
				std::string_view{Connection::Handler::Instance().LastError()},
				Connection::Handler::Instance().LastErrorCode()));
		}

		struct sockaddr_storage bound_address{};
#ifdef WINDOWS
		int bound_address_size = static_cast<int>(sizeof(bound_address));
#else
		socklen_t bound_address_size = static_cast<socklen_t>(sizeof(bound_address));
#endif
		if (::getsockname(m_handle, reinterpret_cast<struct sockaddr*>(&bound_address), &bound_address_size) == -1) {
			return fail(Unexpected<ConnectionError>("Failed to query bound socket port: {} (error code: {})",
				std::string_view{Connection::Handler::Instance().LastError()},
				Connection::Handler::Instance().LastErrorCode()));
		}

		if (bound_address.ss_family == AF_INET)
			m_port = ntohs(reinterpret_cast<const struct sockaddr_in*>(&bound_address)->sin_port);
		else if (bound_address.ss_family == AF_INET6)
			m_port = ntohs(reinterpret_cast<const struct sockaddr_in6*>(&bound_address)->sin6_port);
		else
			return fail(Unexpected<ConnectionError>("Listener bound to an unsupported address family"));

		InitializeAfterConnect();
		m_logger << Logger::Level::LowLevel << "Server listening on " << hostname << ":" << m_port << std::endl;
		return {};
	} catch (...) {
		return fail(Unexpected<ConnectionError>("Failed to allocate listener state"));
	}
}

unsigned short Socket::Server::Port() const noexcept {
	return m_port;
}

ExpectedClient Socket::Server::Accept() noexcept {
	if (!Connection::IsConnected(m_status.load(Safe::MemoryOrder::Acquire)))
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
	try {
		Client client_socket(m_protocol, m_logger);
		Connection::HandlerType client_handle = ::accept(m_handle, nullptr, nullptr);
#ifdef WINDOWS
		if (client_handle == INVALID_SOCKET) {
#else
		if (client_handle == -1) {
#endif
			return Unexpected<ConnectionError>("Failed to accept client connection.");
		}

		client_socket.m_handle = client_handle;
		client_socket.m_status.store(Connection::Status::Connecting, Safe::MemoryOrder::Release);
		client_socket.InitializeAfterConnect();
		m_active_clients.push_back(Safe::MakeShared<Client>(std::move(client_socket)));
		return m_active_clients.back();
	} catch (...) {
		return Unexpected<ConnectionError>("Failed to allocate accepted client state");
	}
}

void Socket::Server::Disconnect() noexcept {
	for (auto& client : m_active_clients) {
		if (!client) continue;
		client->Disconnect();
	}

	m_active_clients.clear();
	Socket::Disconnect();
	m_port = 0;
}

void Socket::Server::DisconnectClient(std::string_view client_uuid) noexcept {
	auto it = std::find_if(m_active_clients.begin(), m_active_clients.end(),
		[&client_uuid](const Safe::Shared<Client>& client) {
			return client && static_cast<std::string_view>(client->UUID()) == client_uuid;
		});
	if (it != m_active_clients.end()) {
		(*it)->Disconnect();
		m_active_clients.erase(it);
	}
}
