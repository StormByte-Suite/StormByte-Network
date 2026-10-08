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

#include <StormByte/byte_size.hxx>
#include <StormByte/expected.hxx>
#include <StormByte/network/connection/protocol.hxx>
#include <StormByte/network/exception.hxx>
#include <StormByte/safe/pointers.hxx>
#include <StormByte/safe/string.hxx>

#include <string_view>

#ifdef WINDOWS
#include <winsock2.h>
#else
#include <sys/socket.h>
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
		 * @namespace StormByte::Network::Connection
		 * @brief Connection namespace.
		 */
		namespace Connection {
			constexpr const unsigned short DEFAULT_MTU = 1500;	///< Default MTU in bytes.

			/**
			 * @class Info
			 * @brief Resolved peer address with provider-owned native storage.
			 */
			class STORMBYTE_NETWORK_PRIVATE Info final {
				public:
					/**
					 * @brief Copy constructor (deleted).
					 * @param other Address that cannot be copied.
					 */
					Info(const Info& other) noexcept = delete;

					/**
					 * @brief Transfer the address and semantic state inside Network.
					 * @param other Source address.
					 */
					Info(Info&& other) noexcept;

					/**
					 * @brief Release Safe address and string owners inside Network.
					 */
					~Info() noexcept;

					/**
					 * @brief Copy assignment (deleted).
					 * @param other Address that cannot be copied.
					 * @return Reference to this address (operation is deleted).
					 */
					Info& operator=(const Info& other) noexcept = delete;

					/**
					 * @brief Transfer address state inside Network.
					 * @param other Source address.
					 * @return Reference to this address.
					 */
					Info& operator=(Info&& other) noexcept;

					/**
					 * @brief Resolve a hostname and build Info.
					 * @param hostname Borrowed host name, without embedded nulls.
					 * @param port Port.
					 * @param protocol Address family.
					 * @return Info or error.
					 */
					static StormByte::Expected<Info, Exception> FromHost(std::string_view hostname, const unsigned short& port, const Protocol& protocol) noexcept;

					/**
					 * @brief Copy an existing Safe socket address into Network-owned storage.
					 * @param sock_addr IPv4 or IPv6 socket address owner.
					 * @return Info or error for null or unsupported addresses.
					 */
					static StormByte::Expected<Info, Exception> FromSockAddr(StormByte::Safe::Shared<const sockaddr> sock_addr) noexcept;

					/**
					 * @brief Copy a borrowed native address into Network-owned Safe storage.
					 * @param sock_addr Valid IPv4 or IPv6 address, borrowed for this call only.
					 * @return Info or error for null or unsupported addresses.
					 */
					static StormByte::Expected<Info, Exception> FromSockAddr(const sockaddr* sock_addr) noexcept;

					/**
					 * @brief Borrow the resolved IP string.
					 * @return Safe string, valid for this object's lifetime.
					 */
					const StormByte::Safe::String& IP() const noexcept;

					/**
					 * @brief Port number.
					 * @return Port in host byte order.
					 */
					const unsigned short& Port() const noexcept;

					/**
					 * @brief Retain the immutable native address independently of this Info.
					 * @return Safe address owner.
					 */
					StormByte::Safe::Shared<const sockaddr> SockAddr() const noexcept;

					/**
					 * @brief Native address length for connect, bind and address comparison.
					 * @return IPv4 or IPv6 structure size in bytes.
					 */
					StormByte::ByteSize SockAddrSize() const noexcept;

				private:
					StormByte::Safe::Shared<sockaddr> m_sock_addr;	///< Safe owner retaining native address storage.
					StormByte::ByteSize m_sock_addr_size;			///< Native address length in bytes.
					[[maybe_unused]] unsigned int m_mtu;			///< MTU in bytes, reserved.
					StormByte::Safe::String m_ip;					///< Resolved IP string.
					unsigned short m_port;						///< Port in host byte order.

					/**
					 * @brief Construct from a validated provider-owned address.
					 * @param sock_addr Safe native address owner.
					 */
					Info(StormByte::Safe::Shared<sockaddr> sock_addr) noexcept;

					/**
					 * @brief Resolve and copy the full native address through a Network factory.
					 * @param hostname Host name.
					 * @param port Port.
					 * @param protocol Address family.
					 * @return Safe native address owner or error.
					 */
					static StormByte::Expected<StormByte::Safe::Shared<sockaddr>, Exception> ResolveHostname(std::string_view hostname, const unsigned short& port, const Protocol& protocol) noexcept;

					/**
					 * @brief Fill semantic IP, port and byte length from the owned address.
					 */
					void Initialize() noexcept;
			};
		}
	}
}

STORMBYTE_DECLARE_MAYBE_SAFE(StormByte::Network::Connection::Info);
