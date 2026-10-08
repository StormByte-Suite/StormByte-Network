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

#include <StormByte/buffer/consumer.hxx>
#include <StormByte/network/visibility.h>
#include <StormByte/safe/binary.hxx>
#include <StormByte/size.hxx>

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
			 * @struct SessionOutputStream
			 * @brief One queued pipeline output source and its buffered bytes.
			 * @details Construction, copying, movement, assignment and destruction
			 * run in Network. Consumer copies share Buffer-owned ring storage through
			 * Buffer's provider callbacks; binary copies use Base-owned storage.
			 * Network, Buffer and Base must remain loaded with a compatible ABI
			 * until every record is released.
			 */
			struct STORMBYTE_NETWORK_PRIVATE SessionOutputStream final {
				/**
				 * @brief Create an empty output source in Network.
				 */
				SessionOutputStream();

				/**
				 * @brief Retain a pipeline output source.
				 * @param consumer Source transferred into this stream.
				 */
				explicit SessionOutputStream(Buffer::Consumer&& consumer) noexcept;

				/**
				 * @brief Share the source and copy buffered bytes through their providers.
				 * @param other Source record.
				 */
				SessionOutputStream(const SessionOutputStream& other);

				/**
				 * @brief Transfer source and buffered-byte ownership.
				 * @param other Source record.
				 */
				SessionOutputStream(SessionOutputStream&& other) noexcept;

				/**
				 * @brief Release source and byte ownership through their providers.
				 */
				~SessionOutputStream() noexcept;

				/**
				 * @brief Share the source and replace the buffered-byte snapshot.
				 * @param other Source record.
				 * @return This record.
				 */
				SessionOutputStream& operator=(const SessionOutputStream& other);

				/**
				 * @brief Transfer source and buffered-byte ownership.
				 * @param other Source record.
				 * @return This record.
				 */
				SessionOutputStream& operator=(SessionOutputStream&& other) noexcept;

				Buffer::Consumer source;	///< Buffer-owned pipeline output source.
				Safe::Binary data;			///< Base-owned buffered chunk.
				ByteSize offset{0};			///< Bytes already written from the chunk.
			};
		}
	}
}

STORMBYTE_DECLARE_MAYBE_SAFE(StormByte::Network::Detail::SessionOutputStream);