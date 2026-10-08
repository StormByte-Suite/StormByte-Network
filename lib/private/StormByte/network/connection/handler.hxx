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

#include <StormByte/network/typedefs.hxx>
#include <StormByte/safe/string.hxx>

#ifdef WINDOWS
#include <winsock2.h>
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
	/**
	 * @class Handler
	 * @brief Platform bootstrap and last-error helpers (singleton).
	 *
	 * WSAStartup on Windows. Non-copyable / non-movable.
	 */
	class STORMBYTE_NETWORK_PRIVATE Handler {
		public:
			/**
			 * @brief Copy constructor (deleted).
			 * @param other Handler that cannot be copied.
			 */
			Handler(const Handler& other) = delete;

			/**
			 * @brief Move constructor (deleted).
			 * @param other Handler that cannot be moved.
			 */
			Handler(Handler&& other) noexcept = delete;

			/**
			 * @brief Destructor (WSACleanup on Windows).
			 */
			~Handler() noexcept;

			/**
			 * @brief Copy assignment (deleted).
			 * @param other Handler that cannot be copied.
			 * @return Reference to this handler (operation is deleted).
			 */
			Handler& operator=(const Handler& other) = delete;

			/**
			 * @brief Move assignment (deleted).
			 * @param other Handler that cannot be moved.
			 * @return Reference to this handler (operation is deleted).
			 */
			Handler& operator=(Handler&& other) noexcept = delete;

			/**
			 * @brief Global instance.
			 * @return Handler.
			 */
			static Handler& Instance() noexcept;

			/**
			 * @brief Last network error as text.
			 * @return Description.
			 */
			StormByte::Safe::String LastError() const noexcept;

			/**
			 * @brief Raw last error (errno / WSAGetLastError).
			 * @return Code.
			 */
			int LastErrorCode() const noexcept;

			/**
			 * @brief Platform error code as text.
			 * @param errnum Error code.
			 * @return Description, or numeric string on failure.
			 */
			StormByte::Safe::String ErrnoToString(int errnum) const noexcept;

		private:
			bool m_initialized = false;	///< Initialization flag
			#ifdef WINDOWS
			WSADATA m_wsaData;			///< Winsock data
			#endif

			/**
			 * @brief Private constructor (singleton).
			 */
			Handler() noexcept;
	};
		}
	}
}

STORMBYTE_DECLARE_MAYBE_SAFE(StormByte::Network::Connection::Handler);
