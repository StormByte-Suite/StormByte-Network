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
#include <StormByte/network/connection/handler.hxx>
#include <StormByte/safe/atomic.hxx>

#include <chrono>

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
		 * @namespace StormByte::Network::Socket
		 * @brief Private native socket operations.
		 */
		namespace Socket {
			/**
			 * @brief Terminal or immediate progress state, independent of native error codes.
			 */
			enum class OperationState { Complete, WouldBlock, PeerClosed, TimedOut, Cancelled, Error, Busy };

			/**
			 * @brief Scalar synchronous operation controls; no resources are retained.
			 * @details Cancellation wins over expiry. The borrowed flag must outlive the
			 * call. A maximum deadline means no timeout. Lifecycle and I/O remain serialized.
			 */
			struct OperationControl {
				std::chrono::steady_clock::time_point deadline = std::chrono::steady_clock::time_point::max();	///< Absolute total deadline.
				const StormByte::Safe::Atomic<bool>* cancellation = nullptr;	///< Borrowed cancellation flag.

				/**
				 * @brief Check cancellation and the absolute deadline.
				 * @return Complete when work may continue, otherwise its stop reason.
				 */
				OperationState Check() const noexcept;
			};

			/**
			 * @brief Non-owning result of a controlled operation or single read.
			 */
			struct OperationResult {
				OperationState state = OperationState::Complete;	///< Completion or stop reason.
				StormByte::ByteSize bytes{0};	///< Bytes transferred, including progress before a failure.
				int native_error = 0;	///< Captured OS error, zero for non-error outcomes.
			};

			/**
			 * @brief Wait for writability with bounded cancellation polling and EINTR retries.
			 * @param handle Non-blocking native handle, borrowed for this call.
			 * @param control Absolute deadline and optional cancellation flag.
			 * @return Complete for readiness (including socket errors), or the stop reason.
			 */
			OperationResult WaitWritable(Connection::HandlerType handle, const OperationControl& control) noexcept;
		}
	}
}
