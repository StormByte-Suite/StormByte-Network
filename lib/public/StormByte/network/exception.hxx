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
	 * @brief Root exception for Network. `what()` is `StormByte.Network: message`.
	 *
	 * This type applies the `Network` segment. A further segment is an @ref Path
	 * (`Connection`, `Transport.Packet`, `Transport.Frame`). The body does not
	 * include the path. Literal `{}` are replaced by @ref StormByte::Safe::String::ReplaceToken.
	 * This does not call `std::format`. The destructor is defined in this module.
	 *
	 * The message constructor takes `string_view`. A literal, a C string,
	 * `std::string` and `Safe::String` reach it through their existing
	 * conversions. `Safe::String` converts implicitly.
	 *
	 * @see ConnectionError, ConnectionClosed, PacketError, FrameError
	 */
	class STORMBYTE_NETWORK_PUBLIC Exception: public StormByte::Exception {
		public:
			/**
			 * @struct Path
			 * @brief Additional path segments nested below `StormByte.Network`.
			 */
			struct Path {
				std::string_view segments; ///< Additional dotted path segments.

				/**
				 * @brief Wraps additional path segments.
				 * @param segments Dotted segments appended below `Network`. Empty adds none.
				 */
				explicit constexpr Path(std::string_view segments = {}) noexcept: segments(segments) {}
			};

			/**
			 * @brief Replace literal `{}` under `StormByte.Network`.
			 * @tparam Args Views, numbers, or types that convert to `std::string`. A number uses `std::to_string`.
			 * @param pattern Body with literal `{}` tokens. Not a `std::format` string.
			 * @param args Replacements. One per token, in order.
			 */
			template <typename... Args>
				requires (sizeof...(Args) >= 1)
			explicit Exception(std::string_view pattern, Args&&... args)
				: StormByte::Exception(StormByte::Exception::Path{"Network"}, pattern, std::forward<Args>(args)...) {}

			/**
			 * @brief Copy a borrowed plain message under `StormByte.Network` in the provider.
			 * @param message Exception text. Copied during the call and never retained.
			 */
			explicit Exception(std::string_view message);

			/**
			 * @brief Replace literal `{}` under an additional Network path.
			 * @tparam Args Views, numbers, or types that convert to `std::string`. A number uses `std::to_string`.
			 * @param path Additional segments nested below `StormByte.Network`.
			 * @param pattern Body with literal `{}` tokens. Not a `std::format` string.
			 * @param args Replacements. One per token, in order.
			 */
			template <typename... Args>
				requires (sizeof...(Args) >= 1)
			explicit Exception(Path path, std::string_view pattern, Args&&... args)
				: Exception(path, static_cast<std::string_view>(StormByte::Safe::String::ReplaceToken(pattern, std::forward<Args>(args)...))) {}

			/**
			 * @brief Copy a borrowed plain message under an additional Network path.
			 * @param path Additional segments nested below `StormByte.Network`.
			 * @param message Exception text. Not a format string.
			 */
			explicit Exception(Path path, std::string_view message);

			/**
			 * @brief Copy Base-owned message storage through its provider.
			 * @param other Exception to copy.
			 */
			Exception(const Exception& other);

			/**
			 * @brief Transfer Base-owned message storage without allocating.
			 * @param other Exception to move from.
			 */
			Exception(Exception&& other) noexcept;

			/**
			 * @brief Copy Base-owned message storage through its provider.
			 * @param other Exception to copy.
			 * @return This exception.
			 */
			Exception& operator=(const Exception& other);

			/**
			 * @brief Transfer Base-owned message storage without allocating.
			 * @param other Exception to move from.
			 * @return This exception.
			 */
			Exception& operator=(Exception&& other) noexcept;

			/**
			 * @brief Destroy the exception. Defined in this module so `catch` matches across a DLL.
			 */
			~Exception() noexcept override;
	};

	/**
	 * @class ConnectionError
	 * @brief Connection or socket operation failed. `what()` is `StormByte.Network.Connection: message`.
	 */
	class STORMBYTE_NETWORK_PUBLIC ConnectionError: public Exception {
		public:
			/**
			 * @brief Replace literal `{}` under `StormByte.Network.Connection`.
			 * @tparam Args Views, numbers, or types that convert to `std::string`. A number uses `std::to_string`.
			 * @param pattern Body with literal `{}` tokens. Not a `std::format` string.
			 * @param args Replacements. One per token, in order.
			 */
			template <typename... Args>
				requires (sizeof...(Args) >= 1)
			explicit ConnectionError(std::string_view pattern, Args&&... args)
				: Exception(Path{"Connection"}, pattern, std::forward<Args>(args)...) {}

			/**
			 * @brief Copy a borrowed plain message under `StormByte.Network.Connection`.
			 * @param message Exception text. Copied during the call and never retained.
			 */
			explicit ConnectionError(std::string_view message)
				: Exception(Path{"Connection"}, message) {}

			/**
			 * @brief Replace literal `{}` under an additional Network path.
			 * @tparam Args Views, numbers, or types that convert to `std::string`. A number uses `std::to_string`.
			 * @param path Additional segments nested below `StormByte.Network`.
			 * @param pattern Body with literal `{}` tokens. Not a `std::format` string.
			 * @param args Replacements. One per token, in order.
			 */
			template <typename... Args>
				requires (sizeof...(Args) >= 1)
			explicit ConnectionError(Path path, std::string_view pattern, Args&&... args)
				: Exception(path, pattern, std::forward<Args>(args)...) {}

			/**
			 * @brief Copy a borrowed plain message under an additional Network path.
			 * @param path Additional segments nested below `StormByte.Network`.
			 * @param message Exception text. Not a format string.
			 */
			explicit ConnectionError(Path path, std::string_view message)
				: Exception(path, message) {}

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
	};

	/**
	 * @class ConnectionClosed
	 * @brief Peer closed while waiting or transferring.
	 *
	 * The message constructors prefix `Connection closed: ` under `StormByte.Network.Connection`.
	 * A @ref Path constructor does not add that prefix.
	 */
	class STORMBYTE_NETWORK_PUBLIC ConnectionClosed final: public Exception {
		public:
			/**
			 * @brief Replace literal `{}`, then prefix `Connection closed: `.
			 * @tparam Args Views, numbers, or types that convert to `std::string`. A number uses `std::to_string`.
			 * @param pattern Body with literal `{}` tokens. Not a `std::format` string.
			 * @param args Replacements. One per token, in order.
			 */
			template <typename... Args>
				requires (sizeof...(Args) >= 1)
			explicit ConnectionClosed(std::string_view pattern, Args&&... args)
				: Exception(Path{"Connection"}, "Connection closed: {}",
					static_cast<std::string_view>(StormByte::Safe::String::ReplaceToken(pattern, std::forward<Args>(args)...))) {}

			/**
			 * @brief Prefix a borrowed message with `Connection closed: `.
			 * @param message Exception text. Not a format string.
			 */
			explicit ConnectionClosed(std::string_view message)
				: Exception(Path{"Connection"}, "Connection closed: {}", message) {}

			/**
			 * @brief Replace literal `{}` under an additional Network path. No closed prefix.
			 * @tparam Args Views, numbers, or types that convert to `std::string`. A number uses `std::to_string`.
			 * @param path Additional segments nested below `StormByte.Network`.
			 * @param pattern Body with literal `{}` tokens. Not a `std::format` string.
			 * @param args Replacements. One per token, in order.
			 */
			template <typename... Args>
				requires (sizeof...(Args) >= 1)
			explicit ConnectionClosed(Path path, std::string_view pattern, Args&&... args)
				: Exception(path, pattern, std::forward<Args>(args)...) {}

			/**
			 * @brief Copy a borrowed plain message under an additional Network path. No closed prefix.
			 * @param path Additional segments nested below `StormByte.Network`.
			 * @param message Exception text. Not a format string.
			 */
			explicit ConnectionClosed(Path path, std::string_view message)
				: Exception(path, message) {}

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
	};

	/**
	 * @class PacketError
	 * @brief Transport packet error. `what()` is `StormByte.Network.Transport.Packet: message`.
	 */
	class STORMBYTE_NETWORK_PUBLIC PacketError final: public Exception {
		public:
			/**
			 * @brief Replace literal `{}` under `StormByte.Network.Transport.Packet`.
			 * @tparam Args Views, numbers, or types that convert to `std::string`. A number uses `std::to_string`.
			 * @param pattern Body with literal `{}` tokens. Not a `std::format` string.
			 * @param args Replacements. One per token, in order.
			 */
			template <typename... Args>
				requires (sizeof...(Args) >= 1)
			explicit PacketError(std::string_view pattern, Args&&... args)
				: Exception(Path{"Transport.Packet"}, pattern, std::forward<Args>(args)...) {}

			/**
			 * @brief Copy a borrowed plain message under `StormByte.Network.Transport.Packet`.
			 * @param message Exception text. Copied during the call and never retained.
			 */
			explicit PacketError(std::string_view message)
				: Exception(Path{"Transport.Packet"}, message) {}

			/**
			 * @brief Replace literal `{}` under an additional Network path.
			 * @tparam Args Views, numbers, or types that convert to `std::string`. A number uses `std::to_string`.
			 * @param path Additional segments nested below `StormByte.Network`.
			 * @param pattern Body with literal `{}` tokens. Not a `std::format` string.
			 * @param args Replacements. One per token, in order.
			 */
			template <typename... Args>
				requires (sizeof...(Args) >= 1)
			explicit PacketError(Path path, std::string_view pattern, Args&&... args)
				: Exception(path, pattern, std::forward<Args>(args)...) {}

			/**
			 * @brief Copy a borrowed plain message under an additional Network path.
			 * @param path Additional segments nested below `StormByte.Network`.
			 * @param message Exception text. Not a format string.
			 */
			explicit PacketError(Path path, std::string_view message)
				: Exception(path, message) {}

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
	};

	/**
	 * @class FrameError
	 * @brief Transport frame error. `what()` is `StormByte.Network.Transport.Frame: message`.
	 */
	class STORMBYTE_NETWORK_PUBLIC FrameError final: public Exception {
		public:
			/**
			 * @brief Replace literal `{}` under `StormByte.Network.Transport.Frame`.
			 * @tparam Args Views, numbers, or types that convert to `std::string`. A number uses `std::to_string`.
			 * @param pattern Body with literal `{}` tokens. Not a `std::format` string.
			 * @param args Replacements. One per token, in order.
			 */
			template <typename... Args>
				requires (sizeof...(Args) >= 1)
			explicit FrameError(std::string_view pattern, Args&&... args)
				: Exception(Path{"Transport.Frame"}, pattern, std::forward<Args>(args)...) {}

			/**
			 * @brief Copy a borrowed plain message under `StormByte.Network.Transport.Frame`.
			 * @param message Exception text. Copied during the call and never retained.
			 */
			explicit FrameError(std::string_view message)
				: Exception(Path{"Transport.Frame"}, message) {}

			/**
			 * @brief Replace literal `{}` under an additional Network path.
			 * @tparam Args Views, numbers, or types that convert to `std::string`. A number uses `std::to_string`.
			 * @param path Additional segments nested below `StormByte.Network`.
			 * @param pattern Body with literal `{}` tokens. Not a `std::format` string.
			 * @param args Replacements. One per token, in order.
			 */
			template <typename... Args>
				requires (sizeof...(Args) >= 1)
			explicit FrameError(Path path, std::string_view pattern, Args&&... args)
				: Exception(path, pattern, std::forward<Args>(args)...) {}

			/**
			 * @brief Copy a borrowed plain message under an additional Network path.
			 * @param path Additional segments nested below `StormByte.Network`.
			 * @param message Exception text. Not a format string.
			 */
			explicit FrameError(Path path, std::string_view message)
				: Exception(path, message) {}

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
	};
	}
}

STORMBYTE_DECLARE_MAYBE_SAFE(StormByte::Network::Exception);

STORMBYTE_DECLARE_MAYBE_SAFE(StormByte::Network::ConnectionError);

STORMBYTE_DECLARE_MAYBE_SAFE(StormByte::Network::ConnectionClosed);

STORMBYTE_DECLARE_MAYBE_SAFE(StormByte::Network::PacketError);

STORMBYTE_DECLARE_MAYBE_SAFE(StormByte::Network::FrameError);
