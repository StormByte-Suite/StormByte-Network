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

#pragma once

#include <StormByte/network/connection/protocol.hxx>
#include <StormByte/network/discovery.hxx>
#include <StormByte/safe/binary.hxx>
#include <StormByte/safe/string.hxx>

#include <chrono>

#ifdef WINDOWS
#include <winsock2.h>
using DiscoverySockLen = int;
#else
#include <sys/socket.h>
#include <netinet/in.h>
using DiscoverySockLen = socklen_t;
#endif

/**
 * @namespace StormByte
 * @brief Root namespace of the StormByte suite.
 */
namespace StormByte {
	/**
	 * @namespace StormByte::Network
	 * @brief Network module of the StormByte suite.
	 */
	namespace Network {
		/**
		 * @namespace StormByte::Network::Detail
		 * @brief Private implementation details of the Network module.
		 */
		namespace Detail {
			/**
			 * @class DiscoveryTransport
			 * @brief UDP transport for Announce (server) and Discover (client).
			 *
			 * IPv4 uses SO_BROADCAST. IPv6 uses link-local multicast (ff02::1).
			 * The payload is opaque; this class only moves bytes.
			 */
			class DiscoveryTransport {
				public:
					/**
					 * @brief Construct a discovery transport for one address family.
					 * @param protocol IPv4 or IPv6.
					 */
					explicit DiscoveryTransport(Connection::Protocol protocol);

					/**
					 * @brief Close the native handle.
					 */
					~DiscoveryTransport() noexcept;

					DiscoveryTransport(const DiscoveryTransport&) = delete;
					DiscoveryTransport& operator=(const DiscoveryTransport&) = delete;

					/**
					 * @brief Bind as a server listener on the discovery port.
					 * @param port UDP port to bind.
					 * @return true on success.
					 */
					bool Bind(unsigned short port) noexcept;

					/**
					 * @brief Create an unbound client socket ready to send and receive.
					 * @return true on success.
					 */
					bool Open() noexcept;

					/**
					 * @brief Send a query to the broadcast/multicast destination.
					 * @param payload Query bytes.
					 * @param port Destination port.
					 * @return true on success.
					 */
					bool SendQuery(const StormByte::Safe::Binary& payload, unsigned short port) noexcept;

					/**
					 * @brief Receive one datagram or time out.
					 * @param timeout Maximum wait.
					 * @param reply Filled on success.
					 * @return true when a datagram was received.
					 */
					bool Receive(std::chrono::milliseconds timeout, DiscoveryReply& reply) noexcept;

					/**
					 * @brief Reply to the last received source (server side).
					 * @param payload Response bytes.
					 * @param to Destination of the last Receive.
					 * @return true on success.
					 */
					bool ReplyTo(const StormByte::Safe::Binary& payload, const DiscoveryReply& to) noexcept;

					/**
					 * @brief Close the socket.
					 */
					void Close() noexcept;

				private:
					Connection::Protocol m_protocol;
#ifdef WINDOWS
					SOCKET m_handle = INVALID_SOCKET;
#else
					int m_handle = -1;
#endif
					sockaddr_storage m_last_from{};
					DiscoverySockLen m_last_from_len = 0;

					bool CreateSocket() noexcept;
					bool EnableBroadcastOrMulticast() noexcept;
			};
		}
	}
}
