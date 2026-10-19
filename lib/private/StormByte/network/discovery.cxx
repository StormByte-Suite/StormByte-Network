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

#include <StormByte/network/discovery_transport.hxx>

#include <array>
#include <cstring>

#ifdef UNIX
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#include <poll.h>
#include <errno.h>
#else
#include <winsock2.h>
#include <ws2tcpip.h>
#endif


namespace {
	constexpr const char* kIPv6MulticastGroup = "ff02::1";
#ifndef IPV6_ADD_MEMBERSHIP
#ifdef IPV6_JOIN_GROUP
#define IPV6_ADD_MEMBERSHIP IPV6_JOIN_GROUP
#endif
#endif
}

StormByte::Network::Detail::DiscoveryTransport::DiscoveryTransport(Connection::Protocol protocol):
	m_protocol(protocol) {}

StormByte::Network::Detail::DiscoveryTransport::~DiscoveryTransport() noexcept {
	Close();
}

bool StormByte::Network::Detail::DiscoveryTransport::CreateSocket() noexcept {
#ifdef WINDOWS
	WSADATA wsa;
	if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0)
		return false;
	m_handle = ::socket(static_cast<int>(m_protocol), SOCK_DGRAM, IPPROTO_UDP);
	if (m_handle == INVALID_SOCKET)
		return false;
#else
	m_handle = ::socket(static_cast<int>(m_protocol), SOCK_DGRAM, IPPROTO_UDP);
	if (m_handle < 0)
		return false;
#endif
	int reuse = 1;
	::setsockopt(m_handle, SOL_SOCKET, SO_REUSEADDR,
#ifdef WINDOWS
		reinterpret_cast<const char*>(&reuse),
#else
		&reuse,
#endif
		sizeof(reuse));
	return EnableBroadcastOrMulticast();
}

bool StormByte::Network::Detail::DiscoveryTransport::EnableBroadcastOrMulticast() noexcept {
	if (m_protocol == Connection::Protocol::IPv4) {
		int broadcast = 1;
		return ::setsockopt(m_handle, SOL_SOCKET, SO_BROADCAST,
#ifdef WINDOWS
			reinterpret_cast<const char*>(&broadcast),
#else
			&broadcast,
#endif
			sizeof(broadcast)) == 0;
	}
	// IPv6: join the link-local all-nodes group so we receive queries/replies.
	struct ipv6_mreq mreq{};
	if (inet_pton(AF_INET6, kIPv6MulticastGroup, &mreq.ipv6mr_multiaddr) != 1)
		return false;
	mreq.ipv6mr_interface = 0;
	return ::setsockopt(m_handle, IPPROTO_IPV6, IPV6_ADD_MEMBERSHIP,
#ifdef WINDOWS
		reinterpret_cast<const char*>(&mreq),
#else
		&mreq,
#endif
		sizeof(mreq)) == 0;
}

bool StormByte::Network::Detail::DiscoveryTransport::Open() noexcept {
	if (!CreateSocket())
		return false;
	return true;
}

bool StormByte::Network::Detail::DiscoveryTransport::Bind(unsigned short port) noexcept {
	if (!CreateSocket())
		return false;

	sockaddr_storage addr{};
	if (m_protocol == Connection::Protocol::IPv4) {
		auto* v4 = reinterpret_cast<sockaddr_in*>(&addr);
		v4->sin_family = AF_INET;
		v4->sin_port = htons(port);
		v4->sin_addr.s_addr = INADDR_ANY;
		if (::bind(m_handle, reinterpret_cast<sockaddr*>(v4), sizeof(*v4)) != 0) {
			Close();
			return false;
		}
	} else {
		auto* v6 = reinterpret_cast<sockaddr_in6*>(&addr);
		v6->sin6_family = AF_INET6;
		v6->sin6_port = htons(port);
		v6->sin6_addr = in6addr_any;
		if (::bind(m_handle, reinterpret_cast<sockaddr*>(v6), sizeof(*v6)) != 0) {
			Close();
			return false;
		}
	}
	return true;
}

bool StormByte::Network::Detail::DiscoveryTransport::SendQuery(const StormByte::Safe::Binary& payload, unsigned short port) noexcept {
	if (payload.empty())
		return false;

	sockaddr_storage dest{};
	DiscoverySockLen dest_len = 0;
	if (m_protocol == Connection::Protocol::IPv4) {
		auto* v4 = reinterpret_cast<sockaddr_in*>(&dest);
		v4->sin_family = AF_INET;
		v4->sin_port = htons(port);
		v4->sin_addr.s_addr = INADDR_BROADCAST;
		dest_len = sizeof(*v4);
	} else {
		auto* v6 = reinterpret_cast<sockaddr_in6*>(&dest);
		v6->sin6_family = AF_INET6;
		v6->sin6_port = htons(port);
		if (inet_pton(AF_INET6, kIPv6MulticastGroup, &v6->sin6_addr) != 1)
			return false;
		dest_len = sizeof(*v6);
	}

	const int sent = ::sendto(m_handle,
#ifdef WINDOWS
		reinterpret_cast<const char*>(payload.data()),
#else
		payload.data(),
#endif
		static_cast<int>(payload.size()), 0,
		reinterpret_cast<sockaddr*>(&dest), dest_len);
	return sent == static_cast<int>(payload.size());
}

bool StormByte::Network::Detail::DiscoveryTransport::Receive(std::chrono::milliseconds timeout, StormByte::Network::DiscoveryReply& reply) noexcept {
	if (
#ifdef WINDOWS
		m_handle == INVALID_SOCKET
#else
		m_handle < 0
#endif
	)
		return false;

#ifdef UNIX
	struct pollfd pfd{};
	pfd.fd = m_handle;
	pfd.events = POLLIN;
	const int ready = ::poll(&pfd, 1, static_cast<int>(timeout.count()));
	if (ready <= 0)
		return false;
#else
	fd_set read_fds;
	FD_ZERO(&read_fds);
	FD_SET(m_handle, &read_fds);
	timeval tv{};
	tv.tv_sec = static_cast<long>(timeout.count() / 1000);
	tv.tv_usec = static_cast<long>((timeout.count() % 1000) * 1000);
	const int ready = ::select(0, &read_fds, nullptr, nullptr, &tv);
	if (ready <= 0)
		return false;
#endif

	std::array<char, 2048> buffer{};
	m_last_from_len = sizeof(m_last_from);
	const int received = ::recvfrom(m_handle, buffer.data(), static_cast<int>(buffer.size()), 0,
		reinterpret_cast<sockaddr*>(&m_last_from), &m_last_from_len);
	if (received <= 0)
		return false;

	reply.payload = StormByte::Safe::Binary(reinterpret_cast<const std::byte*>(buffer.data()),
		static_cast<std::size_t>(received));

	std::array<char, INET6_ADDRSTRLEN> text{};
	if (m_last_from.ss_family == AF_INET) {
		auto* v4 = reinterpret_cast<sockaddr_in*>(&m_last_from);
		inet_ntop(AF_INET, &v4->sin_addr, text.data(), text.size());
		reply.from_port = ntohs(v4->sin_port);
	} else if (m_last_from.ss_family == AF_INET6) {
		auto* v6 = reinterpret_cast<sockaddr_in6*>(&m_last_from);
		inet_ntop(AF_INET6, &v6->sin6_addr, text.data(), text.size());
		reply.from_port = ntohs(v6->sin6_port);
	} else {
		return false;
	}
	reply.from_address = StormByte::Safe::String{text.data()};
	return true;
}

bool StormByte::Network::Detail::DiscoveryTransport::ReplyTo(const StormByte::Safe::Binary& payload, const StormByte::Network::DiscoveryReply&) noexcept {
	if (payload.empty() || m_last_from_len == 0)
		return false;
	const int sent = ::sendto(m_handle,
#ifdef WINDOWS
		reinterpret_cast<const char*>(payload.data()),
#else
		payload.data(),
#endif
		static_cast<int>(payload.size()), 0,
		reinterpret_cast<sockaddr*>(&m_last_from), m_last_from_len);
	return sent == static_cast<int>(payload.size());
}

void StormByte::Network::Detail::DiscoveryTransport::Close() noexcept {
#ifdef WINDOWS
	if (m_handle != INVALID_SOCKET) {
		::closesocket(m_handle);
		m_handle = INVALID_SOCKET;
	}
#else
	if (m_handle >= 0) {
		::close(m_handle);
		m_handle = -1;
	}
#endif
}
