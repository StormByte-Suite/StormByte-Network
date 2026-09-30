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

#include <StormByte/exception.hxx>
#include <StormByte/network/visibility.h>

/**
 * @brief Network module of the StormByte suite.
 */
namespace StormByte::Network {
	/**
	 * @class Exception
	 * @brief Base exception for the Network module.
	 */
	class STORMBYTE_NETWORK_PUBLIC Exception: public StormByte::Exception {
		public:
			/**
			 * @brief Construct with a component prefix and a format string.
			 * @tparam Args Format argument types.
			 * @param component Subsystem name.
			 * @param fmt Format string.
			 * @param args Format arguments.
			 */
			template <typename... Args>
			Exception(std::string_view component, std::format_string<Args...> fmt, Args&&... args):
			StormByte::Exception(
				StormByte::Exception::Path{std::string{"Network::"}.append(component)},
				fmt, std::forward<Args>(args)...) {}

			/**
			 * @brief Destructor. Defined in the Network library to anchor RTTI.
			 */
			~Exception() noexcept override;

			using StormByte::Exception::Exception;
	};

	/**
	 * @class ConnectionError
	 * @brief Connection or socket operation failed.
	 */
	class STORMBYTE_NETWORK_PUBLIC ConnectionError: public Exception {
		public:
			/**
			 * @brief Construct from a format string.
			 * @tparam Args Format argument types.
			 * @param fmt Format string.
			 * @param args Format arguments.
			 */
			template <typename... Args>
			ConnectionError(std::format_string<Args...> fmt, Args&&... args):
			Exception("Connection", fmt, std::forward<Args>(args)...) {}

			/**
			 * @brief Destructor. Defined in the Network library to anchor RTTI.
			 */
			~ConnectionError() noexcept override;

			using Exception::Exception;
	};

	/**
	 * @class ConnectionClosed
	 * @brief Peer closed while waiting or transferring.
	 */
	class STORMBYTE_NETWORK_PUBLIC ConnectionClosed final: public Exception {
		public:
			/**
			 * @brief Construct from a format string.
			 * @tparam Args Format argument types.
			 * @param fmt Format string.
			 * @param args Format arguments.
			 */
			template <typename... Args>
			ConnectionClosed(std::format_string<Args...> fmt, Args&&... args):
			Exception("Connection", "Connection closed: {}",
				std::format(fmt, std::forward<Args>(args)...)) {}

			/**
			 * @brief Destructor. Defined in the Network library to anchor RTTI.
			 */
			~ConnectionClosed() noexcept override;

			using Exception::Exception;
	};

	/**
	 * @class PacketError
	 * @brief Transport packet error.
	 */
	class STORMBYTE_NETWORK_PUBLIC PacketError final: public Exception {
		public:
			/**
			 * @brief Construct from a format string.
			 * @tparam Args Format argument types.
			 * @param fmt Format string.
			 * @param args Format arguments.
			 */
			template <typename... Args>
			PacketError(std::format_string<Args...> fmt, Args&&... args):
			Exception("Transport::Packet: ", fmt, std::forward<Args>(args)...) {}

			/**
			 * @brief Destructor. Defined in the Network library to anchor RTTI.
			 */
			~PacketError() noexcept override;

			using Exception::Exception;
	};

	/**
	 * @class FrameError
	 * @brief Transport frame error.
	 */
	class STORMBYTE_NETWORK_PUBLIC FrameError final: public Exception {
		public:
			/**
			 * @brief Construct from a format string.
			 * @tparam Args Format argument types.
			 * @param fmt Format string.
			 * @param args Format arguments.
			 */
			template <typename... Args>
			FrameError(std::format_string<Args...> fmt, Args&&... args):
			Exception("Transport::Frame: ", fmt, std::forward<Args>(args)...) {}

			/**
			 * @brief Destructor. Defined in the Network library to anchor RTTI.
			 */
			~FrameError() noexcept override;

			using Exception::Exception;
	};
}
