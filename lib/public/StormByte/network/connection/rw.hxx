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

/**
 * @brief Connection types of the Network module.
 */
namespace StormByte::Network::Connection {
	/**
	 * @brief Read-side result codes.
	 */
	namespace Read {
		/**
		 * @enum Result
		 * @brief Outcome of a wait/read.
		 */
		enum class STORMBYTE_NETWORK_PUBLIC Result {
			Success,			///< Data available or read ok
			WouldBlock,			///< Non-blocking: no data yet
			Closed,				///< Local/socket closed
			Failed,				///< Hard failure
			Timeout,			///< Wait timed out
			ShutdownRequest		///< Peer shutdown detected
		};
	}

	/**
	 * @brief Write-side result codes.
	 */
	namespace Write {
		/**
		 * @enum Result
		 * @brief Outcome of a write.
		 */
		enum class STORMBYTE_NETWORK_PUBLIC Result {
			Success,	///< Write completed
			Failed		///< Write failed
		};
	}

	/**
	 * @brief Read result as text.
	 * @param result Read result.
	 * @return Static string literal naming the result.
	 */
	constexpr STORMBYTE_NETWORK_PUBLIC const char* ToString(const StormByte::Network::Connection::Read::Result& result) noexcept {
		switch (result) {
			case StormByte::Network::Connection::Read::Result::Success:			return "Success";
			case StormByte::Network::Connection::Read::Result::WouldBlock:		return "WouldBlock";
			case StormByte::Network::Connection::Read::Result::Failed:			return "Failed";
			case StormByte::Network::Connection::Read::Result::Closed:			return "Closed";
			case StormByte::Network::Connection::Read::Result::Timeout:			return "Timeout";
			case StormByte::Network::Connection::Read::Result::ShutdownRequest:	return "ShutdownRequest";
			default:															return "Unknown";
		}
	}

	/**
	 * @brief Write result as text.
	 * @param result Write result.
	 * @return Static string literal naming the result.
	 */
	constexpr STORMBYTE_NETWORK_PUBLIC const char* ToString(const StormByte::Network::Connection::Write::Result& result) noexcept {
		switch (result) {
			case StormByte::Network::Connection::Write::Result::Success:	return "Success";
			case StormByte::Network::Connection::Write::Result::Failed:		return "Failed";
			default:														return "Unknown";
		}
	}
}
