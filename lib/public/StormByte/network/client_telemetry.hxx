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
 *    Foundation, either version 3 of the License, or (at your option) any
 *    later version.
 *
 * 2. Commercial license
 *    Alternatively, this file may be used under the terms of a commercial
 *    license agreement with the copyright holder
 *    (David C. Manuelda <StormByte@gmail.com>).
 *
 * Both licenses apply only to original StormByte-Network source in this
 * repository. They do not cover other StormByte modules or any third-party
 * material shipped with this repository, which remains under its own license.
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

#include <StormByte/network/telemetry.hxx>
#include <StormByte/network/visibility.h>

#include <atomic>
#include <chrono>
#include <cstdint>

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
		class Client;

		/**
		 * @class ClientTelemetry
		 * @brief Cumulative telemetry owned by one Client instance.
		 */
		class STORMBYTE_NETWORK_PUBLIC ClientTelemetry final: public Telemetry {
			public:
				/** @brief Construct all client counters at zero. */
				ClientTelemetry() noexcept;

				/** @brief Virtual destructor anchors client telemetry RTTI in Network. */
				~ClientTelemetry() noexcept override;

				/** @brief Number of calls to Client::Connect. */
				std::uint64_t ConnectionAttempts() const noexcept;

				/** @brief Successful connections made by this Client. */
				std::uint64_t ConnectionsEstablished() const noexcept;

				/** @brief Failed connection attempts made by this Client. */
				std::uint64_t ConnectionFailures() const noexcept;

				/** @brief Whether this Client currently has an application connection. */
				bool Connected() const noexcept;

				/** @brief Requests initiated through Client::Send. */
				std::uint64_t Requests() const noexcept;

				/** @brief Requests that returned a decoded response packet. */
				std::uint64_t Responses() const noexcept;

				/** @brief Requests for which Send returned no decoded response. */
				std::uint64_t RequestsWithoutResponse() const noexcept;

				/** @brief Number of completed request-latency samples. */
				std::uint64_t RequestLatencySamples() const noexcept;

				/** @brief Mean complete Send round-trip, or zero without samples. */
				std::chrono::microseconds MeanRequestLatency() const noexcept;

				/** @brief Flatten this client's counters to owned UTF-8 text. */
				operator StormByte::Safe::String() const override;

			private:
				friend class Client;

				/** @brief Record the start of a Client::Connect call. */
				void RecordConnectionAttempt() noexcept;

				/** @brief Record a successful Client::Connect call. */
				void RecordConnectionEstablished() noexcept;

				/** @brief Record a failed Client::Connect call. */
				void RecordConnectionFailed() noexcept;

				/** @brief Record application disconnection if currently connected. */
				void RecordDisconnected() noexcept;

				/** @brief Count a Client::Send attempt. */
				void RecordRequest() noexcept;

				/** @brief Record response result and elapsed complete round-trip. */
				void RecordRequestResult(std::chrono::microseconds elapsed, bool responded) noexcept;

				/** @brief Start a Base named clock around one Send operation. */
				Sample MeasureRequest();

				std::atomic<std::uint64_t> m_connection_attempts{0}; ///< Connect calls.
				std::atomic<std::uint64_t> m_connections_established{0}; ///< Successful Connect calls.
				std::atomic<std::uint64_t> m_connection_failures{0}; ///< Failed Connect calls.
				std::atomic<bool> m_connected{false}; ///< Current application-connection state.
				std::atomic<std::uint64_t> m_requests{0}; ///< Client::Send calls.
				std::atomic<std::uint64_t> m_responses{0}; ///< Requests with decoded responses.
				std::atomic<std::uint64_t> m_requests_without_response{0}; ///< Requests without decoded responses.
				std::atomic<std::uint64_t> m_latency_samples{0}; ///< Completed round-trip samples.
				std::atomic<std::uint64_t> m_latency_us{0}; ///< Sum of round-trip microseconds.
		};
	}
}