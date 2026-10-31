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
#include <StormByte/network/connection/info.hxx>

#include <cstring>
#include <memory>
#include <string>
#include <utility>

#ifdef WINDOWS
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h>
#endif

using namespace StormByte::Network::Connection;
using StormByte::Network::Exception;

Info::Info(StormByte::Safe::Shared<sockaddr> sock_addr) noexcept:
	m_sock_addr(std::move(sock_addr)), m_sock_addr_size(0), m_mtu(DEFAULT_MTU), m_ip(), m_port(0) {
	Initialize();
}

Info::Info(Info&& other) noexcept = default;

Info::~Info() noexcept = default;

Info& Info::operator=(Info&& other) noexcept = default;

StormByte::Expected<Info, Exception> Info::FromHost(std::string_view hostname, const unsigned short& port, const Protocol& protocol) noexcept {
	auto expected_sock_addr = Info::ResolveHostname(hostname, port, protocol);
	if (!expected_sock_addr)
		return Unexpected(expected_sock_addr.error());
	return Info(std::move(expected_sock_addr.value()));
}

StormByte::Expected<Info::Addresses, Exception> Info::ResolveAll(
	std::string_view hostname, unsigned short port, AddressPolicy policy) noexcept {
	if (hostname.find('\0') != std::string_view::npos)
		return Unexpected<Exception>("Invalid host name");
	addrinfo hints{}, *result = nullptr;
	switch (policy) {
		case AddressPolicy::IPv4Only:
			hints.ai_family = AF_INET;
			break;
		case AddressPolicy::IPv6Only:
			hints.ai_family = AF_INET6;
			break;
		case AddressPolicy::SystemOrder:
		case AddressPolicy::PreferIPv4:
		case AddressPolicy::PreferIPv6:
			hints.ai_family = AF_UNSPEC;
			break;
		default:
			return Unexpected<Exception>("Unsupported address policy");
	}
	hints.ai_socktype = SOCK_STREAM;
	try {
		(void)Handler::Instance();
		const StormByte::Safe::String host{hostname};
		const std::string service = std::to_string(port);
		hints.ai_flags = AI_NUMERICSERV;
		const int error = getaddrinfo(host.c_str(), service.c_str(), &hints, &result);
		std::unique_ptr<addrinfo, decltype(&freeaddrinfo)> guard(result, freeaddrinfo);
		if (error != 0)
			return Unexpected<Exception>("Can't resolve host '{}': getaddrinfo code {}", hostname, error);
		Addresses addresses;
		Addresses deferred;
		const int preferred = policy == AddressPolicy::PreferIPv4 ? AF_INET :
			policy == AddressPolicy::PreferIPv6 ? AF_INET6 : AF_UNSPEC;
		for (const auto* entry = result; entry; entry = entry->ai_next) {
			if (!entry->ai_addr || (entry->ai_family != AF_INET && entry->ai_family != AF_INET6))
				continue;
			const auto length = entry->ai_family == AF_INET ? sizeof(sockaddr_in) : sizeof(sockaddr_in6);
			if (static_cast<std::size_t>(entry->ai_addrlen) < length)
				continue;
			auto storage = StormByte::Safe::MakeShared<sockaddr_storage>();
			std::memcpy(storage.get(), entry->ai_addr, length);
			auto address = StormByte::Safe::ReinterpretPointerCast<const sockaddr>(storage);
			if (preferred != AF_UNSPEC && entry->ai_family != preferred)
				deferred.push_back(std::move(address));
			else
				addresses.push_back(std::move(address));
		}
		for (auto& address: deferred)
			addresses.push_back(std::move(address));
		if (addresses.empty())
			return Unexpected<Exception>("No usable resolved addresses");
		return addresses;
	} catch (...) {
		return Unexpected<Exception>("Failed to allocate resolved addresses");
	}
}

StormByte::Expected<Info, Exception> Info::FromSockAddr(StormByte::Safe::Shared<const sockaddr> sock_addr) noexcept {
	return FromSockAddr(sock_addr.get());
}

StormByte::Expected<Info, Exception> Info::FromSockAddr(const sockaddr* sock_addr) noexcept {
	if (!sock_addr)
		return Unexpected<Exception>("Invalid socket address");
	std::size_t size;
	if (sock_addr->sa_family == AF_INET)
		size = sizeof(sockaddr_in);
	else if (sock_addr->sa_family == AF_INET6)
		size = sizeof(sockaddr_in6);
	else
		return Unexpected<Exception>("Unsupported socket address family");
	auto storage = StormByte::Safe::MakeShared<sockaddr_storage>();
	std::memcpy(storage.get(), sock_addr, size);
	return Info(StormByte::Safe::ReinterpretPointerCast<sockaddr>(storage));
}

const StormByte::Safe::String& Info::IP() const noexcept {
	return m_ip;
}

const unsigned short& Info::Port() const noexcept {
	return m_port;
}

StormByte::Safe::Shared<const sockaddr> Info::SockAddr() const noexcept {
	return m_sock_addr;
}

StormByte::ByteSize Info::SockAddrSize() const noexcept {
	return m_sock_addr_size;
}

StormByte::Expected<StormByte::Safe::Shared<sockaddr>, Exception> Info::ResolveHostname(std::string_view hostname, const unsigned short& port, const Protocol& protocol) noexcept {
	if (protocol != Protocol::IPv4 && protocol != Protocol::IPv6)
		return Unexpected<Exception>("Unsupported socket address family");
	if (hostname.find('\0') != std::string_view::npos)
		return Unexpected<Exception>("Invalid host name");
	struct addrinfo hints{}, *res = nullptr;
	const StormByte::Safe::String owned_hostname{hostname};
	hints.ai_family = ProtocolInt(protocol);
	hints.ai_socktype = SOCK_STREAM;
	(void)Handler::Instance();
	int ret = getaddrinfo(owned_hostname.c_str(), nullptr, &hints, &res);
	std::unique_ptr<addrinfo, decltype(&freeaddrinfo)> res_guard(res, freeaddrinfo);
	if (ret != 0 || !res)
		return Unexpected<Exception>("Can't resolve host '{}': {}", hostname, std::string_view{Handler::Instance().LastError()});
	if (!res->ai_addr || (res->ai_family != AF_INET && res->ai_family != AF_INET6))
		return Unexpected<Exception>("Unable to determine resolved address");
	const auto size = res->ai_family == AF_INET ? sizeof(sockaddr_in) : sizeof(sockaddr_in6);
	if (static_cast<std::size_t>(res->ai_addrlen) < size)
		return Unexpected<Exception>("Incomplete resolved socket address");
	auto storage = StormByte::Safe::MakeShared<sockaddr_storage>();
	std::memcpy(storage.get(), res->ai_addr, size);
	auto resolved = StormByte::Safe::ReinterpretPointerCast<sockaddr>(storage);
	if (res->ai_family == AF_INET)
		reinterpret_cast<sockaddr_in*>(resolved.get())->sin_port = htons(port);
	else
		reinterpret_cast<sockaddr_in6*>(resolved.get())->sin6_port = htons(port);
	return resolved;
}

void Info::Initialize() noexcept {
	char ipstr[INET6_ADDRSTRLEN]{};
	if (m_sock_addr->sa_family == AF_INET) {
		const auto* address = reinterpret_cast<const sockaddr_in*>(m_sock_addr.get());
		if (inet_ntop(AF_INET, &address->sin_addr, ipstr, sizeof(ipstr)))
			m_ip = std::string_view{ipstr};
		m_port = ntohs(address->sin_port);
		m_sock_addr_size = StormByte::ByteSize{sizeof(sockaddr_in)};
	}
	else if (m_sock_addr->sa_family == AF_INET6) {
		const auto* address = reinterpret_cast<const sockaddr_in6*>(m_sock_addr.get());
		if (inet_ntop(AF_INET6, &address->sin6_addr, ipstr, sizeof(ipstr)))
			m_ip = std::string_view{ipstr};
		m_port = ntohs(address->sin6_port);
		m_sock_addr_size = StormByte::ByteSize{sizeof(sockaddr_in6)};
	}
}
