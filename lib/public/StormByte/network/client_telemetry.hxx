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

#include <StormByte/network/telemetry.hxx>
#include <StormByte/network/visibility.h>
#include <StormByte/safe/atomic.hxx>
#include <StormByte/size.hxx>

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
		/**
		 * @class Client
		 * @brief Forward declaration of the client owning its telemetry.
		 */
		class Client;

		/**
		 * @class ClientTelemetry
		 * @brief Cumulative telemetry owned by one Client instance.
		 */
		class STORMBYTE_NETWORK_PUBLIC ClientTelemetry final: public Telemetry {
			public:
				/**
				 * @brief Construct all client counters at zero.
				 */
				ClientTelemetry() noexcept;

				/**
				 * @brief Virtual destructor anchors client telemetry RTTI in Network.
				 */
				~ClientTelemetry() noexcept override;

				/**
				 * @brief Number of calls to Client::Connect.
				 * @return Cumulative connection-attempt count.
				 */
				Size ConnectionAttempts() const noexcept;

				/**
				 * @brief Successful connections made by this Client.
				 * @return Cumulative successful-connection count.
				 */
				Size ConnectionsEstablished() const noexcept;

				/**
				 * @brief Failed connection attempts made by this Client.
				 * @return Cumulative failed-connection count.
				 */
				Size ConnectionFailures() const noexcept;

				/**
				 * @brief Whether this Client currently has an application connection.
				 * @return True when the recorded application connection is active.
				 */
				bool Connected() const noexcept;

				/**
				 * @brief Requests initiated through Client::Send.
				 * @return Cumulative request-attempt count.
				 */
				Size Requests() const noexcept;

				/**
				 * @brief Requests that returned a decoded response packet.
				 * @return Cumulative decoded-response count.
				 */
				Size Responses() const noexcept;

				/**
				 * @brief Requests for which Send returned no decoded response.
				 * @return Cumulative count of requests without decoded responses.
				 */
				Size RequestsWithoutResponse() const noexcept;

				/**
				 * @brief Number of completed request-latency samples.
				 * @return Cumulative completed round-trip sample count.
				 */
				Size RequestLatencySamples() const noexcept;

				/**
				 * @brief Mean complete Send round-trip, or zero without samples.
				 * @return Mean elapsed microseconds, truncated to whole microseconds;
				 * zero when no samples have been recorded.
				 */
				std::chrono::microseconds MeanRequestLatency() const noexcept;

				/**
				 * @brief Flatten this client's counters to owned UTF-8 text.
				 * @return Owned text containing the client's telemetry counters.
				 */
				operator StormByte::Safe::String() const override;

			private:
				/**
				 * @brief Allow the owning client to record telemetry events.
				 */
				friend class Client;

				/**
				 * @brief Record the start of a Client::Connect call.
				 */
				void RecordConnectionAttempt() noexcept;

				/**
				 * @brief Count a successful connection and mark it connected.
				 */
				void RecordConnectionEstablished() noexcept;

				/**
				 * @brief Record a failed Client::Connect call.
				 */
				void RecordConnectionFailed() noexcept;

				/**
				 * @brief Mark the recorded application connection disconnected.
				 */
				void RecordDisconnected() noexcept;

				/**
				 * @brief Count a Client::Send attempt.
				 */
				void RecordRequest() noexcept;

				/**
				 * @brief Record response result and elapsed complete round-trip.
				 * @param elapsed Round-trip duration; negative values count as zero.
				 * @param responded Whether a decoded response packet was returned.
				 * @details Increment the latency sample count and the corresponding
				 * response or no-response counter.
				 */
				void RecordRequestResult(std::chrono::microseconds elapsed, bool responded) noexcept;

				/**
				 * @brief Start a Base named clock around one Send operation.
				 * @return Independent scoped sample for the Client.Send clock.
				 */
				Sample MeasureRequest();

				Safe::Atomic<std::uint64_t> m_connection_attempts{0};			///< Cumulative Connect calls.
				Safe::Atomic<std::uint64_t> m_connections_established{0};		///< Cumulative successful Connect calls.
				Safe::Atomic<std::uint64_t> m_connection_failures{0};			///< Cumulative failed Connect calls.
				Safe::Atomic<bool> m_connected{false};						///< Current recorded application-connection state.
				Safe::Atomic<std::uint64_t> m_requests{0};						///< Cumulative Client::Send calls.
				Safe::Atomic<std::uint64_t> m_responses{0};						///< Cumulative requests with decoded responses.
				Safe::Atomic<std::uint64_t> m_requests_without_response{0};	///< Cumulative requests without decoded responses.
				Safe::Atomic<std::uint64_t> m_latency_samples{0};				///< Completed round-trip samples, with or without a response.
				Safe::Atomic<std::uint64_t> m_latency_us{0};					///< Sum of nonnegative recorded round-trip microseconds.
		};
	}
}

STORMBYTE_DECLARE_MAYBE_SAFE(StormByte::Network::ClientTelemetry);
