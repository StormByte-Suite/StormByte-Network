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

#include <StormByte/expected.hxx>
#include <StormByte/network/connection/protocol.hxx>
#include <StormByte/network/exception.hxx>

#ifdef WINDOWS
#include <winsock2.h>
#else
#include <sys/socket.h>
#endif

#include <memory>
#include <string>
#include <string_view>

/**
 * @brief Connection helpers of the Network module.
 */
namespace StormByte::Network::Connection {
	constexpr const unsigned short DEFAULT_MTU = 1500;	///< Default MTU

	/**
	 * @class Info
	 * @brief Resolved peer address (IP, port, sockaddr).
	 */
	class STORMBYTE_NETWORK_PRIVATE Info {
		public:
			/**
			 * @brief Copy constructor (deleted).
			 */
			Info(const Info& other) noexcept = delete;

			/**
			 * @brief Move constructor.
			 */
			Info(Info&& other) noexcept = default;

			/**
			 * @brief Destructor.
			 */
			~Info() noexcept = default;

			/**
			 * @brief Copy assignment (deleted).
			 */
			Info& operator=(const Info& other) noexcept = delete;

			/**
			 * @brief Move assignment.
			 */
			Info& operator=(Info&& other) noexcept = default;

			/**
			 * @brief Resolve a hostname and build Info.
			 * @param hostname Host name.
			 * @param port Port.
			 * @param protocol Address family.
			 * @return Info or error.
			 */
			static StormByte::Expected<Info, Exception> FromHost(std::string_view hostname, const unsigned short& port, const Protocol& protocol) noexcept;

			/**
			 * @brief Build Info from an existing sockaddr.
			 * @param sockaddr Socket address.
			 * @return Info or error.
			 */
			static StormByte::Expected<Info, Exception> FromSockAddr(std::shared_ptr<sockaddr> sockaddr) noexcept;

			/**
			 * @brief Resolved IP string.
			 * @return IP.
			 */
			constexpr const std::string& IP() const noexcept {
				return m_ip;
			}

			/**
			 * @brief Port number.
			 * @return Port.
			 */
			constexpr const unsigned short& Port() const noexcept {
				return m_port;
			}

			/**
			 * @brief Shared sockaddr.
			 * @return Address.
			 */
			inline std::shared_ptr<const sockaddr> SockAddr() const noexcept {
				return m_sock_addr;
			}

		private:
			std::shared_ptr<sockaddr> m_sock_addr;	///< Socket address
			unsigned int m_mtu;						///< MTU (reserved)
			std::string m_ip;						///< IP string
			unsigned short m_port;					///< Port

			/**
			 * @brief Construct from a sockaddr.
			 * @param sock_addr Socket address.
			 */
			Info(std::shared_ptr<sockaddr> sock_addr) noexcept;

			/**
			 * @brief Hostname resolution helper.
			 * @param hostname Host name.
			 * @param port Port.
			 * @param protocol Address family.
			 * @return Shared sockaddr or error.
			 */
			static StormByte::Expected<std::shared_ptr<sockaddr>, Exception> ResolveHostname(std::string_view hostname, const unsigned short& port, const Protocol& protocol) noexcept;

			/**
			 * @brief Fill IP/port from sockaddr.
			 * @param sock_addr Socket address.
			 */
			void Initialize(std::shared_ptr<sockaddr> sock_addr) noexcept;
	};
}
