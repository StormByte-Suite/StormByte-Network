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

#include <StormByte/network/visibility.h>

#ifdef WINDOWS
	#include <winsock2.h>
#else
	#include <netinet/in.h>
	#include <sys/socket.h>
#endif

/**
 * @brief Connection types of the Network module.
 */
namespace StormByte::Network::Connection {
	/**
	 * @enum Protocol
	 * @brief Address family for sockets.
	 */
	enum class STORMBYTE_NETWORK_PUBLIC Protocol: int {
		IPv4 = AF_INET,		///< IPv4 (AF_INET)
		IPv6 = AF_INET6,	///< IPv6 (AF_INET6)
	};

	/**
	 * @brief Protocol as text.
	 * @param protocol Protocol value.
	 * @return Static string literal: "IPv4", "IPv6", or "Unknown".
	 */
	constexpr STORMBYTE_NETWORK_PUBLIC const char* ProtocolString(const Protocol& protocol) noexcept {
		switch (protocol) {
			case Protocol::IPv4:	return "IPv4";
			case Protocol::IPv6:	return "IPv6";
			default:				return "Unknown";
		}
	}

	/**
	 * @brief Protocol as AF_* integer.
	 * @param protocol Protocol value.
	 * @return AF_INET or AF_INET6.
	 */
	constexpr STORMBYTE_NETWORK_PUBLIC int ProtocolInt(const Protocol& protocol) noexcept {
		return static_cast<int>(protocol);
	}
}
