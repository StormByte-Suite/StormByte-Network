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

#include <StormByte/network/connection/info.hxx>
#ifdef UNIX
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#else
#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>
#endif
#include <StormByte/network/connection/handler.hxx>
using namespace StormByte::Network::Connection;
using StormByte::Network::Exception;
Info::Info(std::shared_ptr<sockaddr> sock_addr) noexcept:
	m_sock_addr(sock_addr), m_mtu(DEFAULT_MTU), m_ip(), m_port(0) {
	Initialize(sock_addr);
}

StormByte::Expected<Info, Exception> Info::FromHost(std::string_view hostname, const unsigned short& port, const Protocol& protocol) noexcept {
	auto expected_sock_addr = Info::ResolveHostname(hostname, port, protocol);
	if (!expected_sock_addr)
		return Unexpected(expected_sock_addr.error());
	return Info(std::move(expected_sock_addr.value()));
}

StormByte::Expected<Info, Exception> Info::FromSockAddr(std::shared_ptr<sockaddr> sockaddr) noexcept {
	if (!sockaddr)
		return Unexpected<Exception>("Invalid socket address");
	return Info(sockaddr);
}

StormByte::Expected<std::shared_ptr<sockaddr>, Exception> Info::ResolveHostname(std::string_view hostname, const unsigned short& port, const Protocol& protocol) noexcept {
	struct addrinfo hints{}, *res = nullptr;
	const std::string owned_hostname{hostname};
	hints.ai_family = ProtocolInt(protocol);
	hints.ai_socktype = SOCK_STREAM;
	int ret = getaddrinfo(owned_hostname.c_str(), nullptr, &hints, &res);
	if (ret != 0 || !res)
		return Unexpected<Exception>("Can't resolve host '{}': {}", hostname, Handler::Instance().LastError());
	std::unique_ptr<addrinfo, decltype(&freeaddrinfo)> res_guard(res, freeaddrinfo);
	char ipstr[INET6_ADDRSTRLEN];
	void* addr = nullptr;
	if (res->ai_family == AF_INET) {
		addr = &((struct sockaddr_in*)res->ai_addr)->sin_addr;
	} else if (res->ai_family == AF_INET6) {
		addr = &((struct sockaddr_in6*)res->ai_addr)->sin6_addr;
	}

	if (!addr)
		return Unexpected<Exception>("Unable to determine resolved address");
	inet_ntop(res->ai_family, addr, ipstr, sizeof(ipstr));
	sockaddr_in resolved{};
	resolved.sin_family = ProtocolInt(protocol);
	resolved.sin_port = htons(port);
	if (inet_pton(resolved.sin_family, ipstr, &resolved.sin_addr) <= 0) {
		return Unexpected<Exception>("Invalid IP address '{}'", ipstr);
	}

	auto resolved_sock = std::make_unique<sockaddr_in>(resolved);
	return std::shared_ptr<sockaddr>(reinterpret_cast<sockaddr*>(resolved_sock.release()));
}

void Info::Initialize(std::shared_ptr<sockaddr> sock_addr) noexcept {
	char ipstr[INET6_ADDRSTRLEN];
	if (sock_addr->sa_family == AF_INET) {
		inet_ntop(AF_INET, &reinterpret_cast<sockaddr_in*>(sock_addr.get())->sin_addr, ipstr, sizeof(ipstr));
		m_ip = ipstr;
		m_port = ntohs(reinterpret_cast<sockaddr_in*>(sock_addr.get())->sin_port);
	}

	else if (sock_addr->sa_family == AF_INET6) {
		inet_ntop(AF_INET6, &reinterpret_cast<sockaddr_in6*>(sock_addr.get())->sin6_addr, ipstr, sizeof(ipstr));
		m_ip = ipstr;
		m_port = ntohs(reinterpret_cast<sockaddr_in6*>(sock_addr.get())->sin6_port);
	}
}
