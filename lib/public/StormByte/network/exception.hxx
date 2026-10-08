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
#include <StormByte/safe/string.hxx>
#include <StormByte/type_traits/safe.hxx>

#include <format>
#include <iterator>
#include <string_view>
#include <utility>

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
	 * @class Exception
	 * @brief Base exception for the Network module.
	 */
	class STORMBYTE_NETWORK_PUBLIC Exception: public StormByte::Exception {
		public:
			/**
			 * @brief Copy plain exception text under `StormByte.Network`.
			 * @param message Exception text; it is not a format string.
			 */
			explicit Exception(std::string_view message);

			/**
			 * @brief Copy Base-owned text under `StormByte.Network`.
			 * @param message Exception text.
			 */
			explicit Exception(const StormByte::Safe::String& message);

			/**
			 * @brief Format an exception under `StormByte.Network`.
			 * @tparam Args Format argument types.
			 * @param fmt Format string.
			 * @param args Format arguments.
			 */
			template <typename... Args>
			explicit Exception(std::format_string<Args...> fmt, Args&&... args):
				StormByte::Exception(StormByte::Exception::Path{"Network"}, fmt,
					std::forward<Args>(args)...) {}

			/**
			 * @brief Construct with a child path and a format string.
			 * @tparam Args Format argument types.
			 * @param child Path below `Network`.
			 * @param fmt Format string.
			 * @param args Format arguments.
			 */
			template <typename... Args>
			explicit Exception(StormByte::Exception::Path child, std::format_string<Args...> fmt,
				Args&&... args):
				StormByte::Exception(
					StormByte::Exception::Path{JoinPath(child.text)},
					fmt, std::forward<Args>(args)...) {}

			/**
			 * @brief Copy plain text under a Network child component.
			 * @param child Component path under Network.
			 * @param message Exception text; it is not a format string.
			 */
			explicit Exception(StormByte::Exception::Path child, std::string_view message);

			/**
			 * @brief Copy the Base-owned exception message inside Network.
			 * @param other Source exception.
			 */
			Exception(const Exception& other);

			/**
			 * @brief Transfer the Base-owned exception message inside Network.
			 * @param other Source exception.
			 */
			Exception(Exception&& other) noexcept;

			/**
			 * @brief Destructor. Defined in the Network library to anchor RTTI.
			 */
			~Exception() noexcept override;

			/**
			 * @brief Copy-assign the Base-owned exception message.
			 * @param other Source exception.
			 * @return This exception.
			 */
			Exception& operator=(const Exception& other);

			/**
			 * @brief Move-assign the Base-owned exception message.
			 * @param other Source exception.
			 * @return This exception.
			 */
			Exception& operator=(Exception&& other) noexcept;

		protected:
			/**
			 * @brief Format a message directly into Base-owned text.
			 * @tparam Args Format argument types.
			 * @param fmt Format string.
			 * @param args Format arguments.
			 * @return Formatted text without caller-owned STL storage.
			 */
			template<typename... Args>
			static StormByte::Safe::String FormatMessage(std::format_string<Args...> fmt, Args&&... args) {
				StormByte::Safe::String result;
				std::format_to(std::back_inserter(result), fmt, std::forward<Args>(args)...);
				return result;
			}

		private:
			/**
			 * @brief Join a child component path using Base-owned text.
			 * @param child Path below Network.
			 * @return Complete Network component path.
			 */
			static StormByte::Safe::String JoinPath(std::string_view child);
	};

	/**
	 * @class ConnectionError
	 * @brief Connection or socket operation failed.
	 */
	class STORMBYTE_NETWORK_PUBLIC ConnectionError: public Exception {
		public:
			/**
			 * @brief Copy plain connection-error text.
			 * @param message Exception text; it is not a format string.
			 */
			explicit ConnectionError(std::string_view message): Exception(StormByte::Exception::Path{"Connection"}, message) {}

			/**
			 * @brief Construct from a format string.
			 * @tparam Args Format argument types.
			 * @param fmt Format string.
			 * @param args Format arguments.
			 */
			template <typename... Args>
			ConnectionError(std::format_string<Args...> fmt, Args&&... args):
				Exception(StormByte::Exception::Path{"Connection"}, fmt, std::forward<Args>(args)...) {}

			/**
			 * @brief Copy the connection error inside Network.
			 * @param other Source error.
			 */
			ConnectionError(const ConnectionError& other);

			/**
			 * @brief Move the connection error inside Network.
			 * @param other Source error.
			 */
			ConnectionError(ConnectionError&& other) noexcept;

			/**
			 * @brief Destructor. Defined in the Network library to anchor RTTI.
			 */
			~ConnectionError() noexcept override;

			/**
			 * @brief Copy-assign the connection error.
			 * @param other Source error.
			 * @return This error.
			 */
			ConnectionError& operator=(const ConnectionError& other);

			/**
			 * @brief Move-assign the connection error.
			 * @param other Source error.
			 * @return This error.
			 */
			ConnectionError& operator=(ConnectionError&& other) noexcept;

			/**
			 * @brief Preserve the Network exception constructors.
			 */
			using Exception::Exception;
	};

	/**
	 * @class ConnectionClosed
	 * @brief Peer closed while waiting or transferring.
	 */
	class STORMBYTE_NETWORK_PUBLIC ConnectionClosed final: public Exception {
		public:
			/**
			 * @brief Copy plain connection-closed text.
			 * @param message Exception text; it is not a format string.
			 */
			explicit ConnectionClosed(std::string_view message):
				Exception(StormByte::Exception::Path{"Connection"}, "Connection closed: {}", message) {}

			/**
			 * @brief Construct from a format string.
			 * @tparam Args Format argument types.
			 * @param fmt Format string.
			 * @param args Format arguments.
			 */
			template <typename... Args>
			ConnectionClosed(std::format_string<Args...> fmt, Args&&... args):
				Exception(StormByte::Exception::Path{"Connection"}, "Connection closed: {}",
					std::string_view{FormatMessage(fmt, std::forward<Args>(args)...)}) {}

			/**
			 * @brief Copy the closed-connection error inside Network.
			 * @param other Source error.
			 */
			ConnectionClosed(const ConnectionClosed& other);

			/**
			 * @brief Move the closed-connection error inside Network.
			 * @param other Source error.
			 */
			ConnectionClosed(ConnectionClosed&& other) noexcept;

			/**
			 * @brief Destructor. Defined in the Network library to anchor RTTI.
			 */
			~ConnectionClosed() noexcept override;

			/**
			 * @brief Copy-assign the closed-connection error.
			 * @param other Source error.
			 * @return This error.
			 */
			ConnectionClosed& operator=(const ConnectionClosed& other);

			/**
			 * @brief Move-assign the closed-connection error.
			 * @param other Source error.
			 * @return This error.
			 */
			ConnectionClosed& operator=(ConnectionClosed&& other) noexcept;

			/**
			 * @brief Preserve the Network exception constructors.
			 */
			using Exception::Exception;
	};

	/**
	 * @class PacketError
	 * @brief Transport packet error.
	 */
	class STORMBYTE_NETWORK_PUBLIC PacketError final: public Exception {
		public:
			/**
			 * @brief Copy plain packet-error text.
			 * @param message Exception text; it is not a format string.
			 */
			explicit PacketError(std::string_view message):
				Exception(StormByte::Exception::Path{"Transport.Packet"}, message) {}

			/**
			 * @brief Construct from a format string.
			 * @tparam Args Format argument types.
			 * @param fmt Format string.
			 * @param args Format arguments.
			 */
			template <typename... Args>
			PacketError(std::format_string<Args...> fmt, Args&&... args):
				Exception(StormByte::Exception::Path{"Transport.Packet"}, fmt, std::forward<Args>(args)...) {}

			/**
			 * @brief Copy the packet error inside Network.
			 * @param other Source error.
			 */
			PacketError(const PacketError& other);

			/**
			 * @brief Move the packet error inside Network.
			 * @param other Source error.
			 */
			PacketError(PacketError&& other) noexcept;

			/**
			 * @brief Destructor. Defined in the Network library to anchor RTTI.
			 */
			~PacketError() noexcept override;

			/**
			 * @brief Copy-assign the packet error.
			 * @param other Source error.
			 * @return This error.
			 */
			PacketError& operator=(const PacketError& other);

			/**
			 * @brief Move-assign the packet error.
			 * @param other Source error.
			 * @return This error.
			 */
			PacketError& operator=(PacketError&& other) noexcept;

			/**
			 * @brief Preserve the Network exception constructors.
			 */
			using Exception::Exception;
	};

	/**
	 * @class FrameError
	 * @brief Transport frame error.
	 */
	class STORMBYTE_NETWORK_PUBLIC FrameError final: public Exception {
		public:
			/**
			 * @brief Copy plain frame-error text.
			 * @param message Exception text; it is not a format string.
			 */
			explicit FrameError(std::string_view message):
				Exception(StormByte::Exception::Path{"Transport.Frame"}, message) {}

			/**
			 * @brief Construct from a format string.
			 * @tparam Args Format argument types.
			 * @param fmt Format string.
			 * @param args Format arguments.
			 */
			template <typename... Args>
			FrameError(std::format_string<Args...> fmt, Args&&... args):
				Exception(StormByte::Exception::Path{"Transport.Frame"}, fmt, std::forward<Args>(args)...) {}

			/**
			 * @brief Copy the frame error inside Network.
			 * @param other Source error.
			 */
			FrameError(const FrameError& other);

			/**
			 * @brief Move the frame error inside Network.
			 * @param other Source error.
			 */
			FrameError(FrameError&& other) noexcept;

			/**
			 * @brief Destructor. Defined in the Network library to anchor RTTI.
			 */
			~FrameError() noexcept override;

			/**
			 * @brief Copy-assign the frame error.
			 * @param other Source error.
			 * @return This error.
			 */
			FrameError& operator=(const FrameError& other);

			/**
			 * @brief Move-assign the frame error.
			 * @param other Source error.
			 * @return This error.
			 */
			FrameError& operator=(FrameError&& other) noexcept;

			/**
			 * @brief Preserve the Network exception constructors.
			 */
			using Exception::Exception;
	};
	}
}

STORMBYTE_DECLARE_MAYBE_SAFE(StormByte::Network::Exception);

STORMBYTE_DECLARE_MAYBE_SAFE(StormByte::Network::ConnectionError);

STORMBYTE_DECLARE_MAYBE_SAFE(StormByte::Network::ConnectionClosed);

STORMBYTE_DECLARE_MAYBE_SAFE(StormByte::Network::PacketError);

STORMBYTE_DECLARE_MAYBE_SAFE(StormByte::Network::FrameError);
