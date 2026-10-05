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
				std::uint64_t ConnectionAttempts() const noexcept;

				/**
				 * @brief Successful connections made by this Client.
				 * @return Cumulative successful-connection count.
				 */
				std::uint64_t ConnectionsEstablished() const noexcept;

				/**
				 * @brief Failed connection attempts made by this Client.
				 * @return Cumulative failed-connection count.
				 */
				std::uint64_t ConnectionFailures() const noexcept;

				/**
				 * @brief Whether this Client currently has an application connection.
				 * @return True when the recorded application connection is active.
				 */
				bool Connected() const noexcept;

				/**
				 * @brief Requests initiated through Client::Send.
				 * @return Cumulative request-attempt count.
				 */
				std::uint64_t Requests() const noexcept;

				/**
				 * @brief Requests that returned a decoded response packet.
				 * @return Cumulative decoded-response count.
				 */
				std::uint64_t Responses() const noexcept;

				/**
				 * @brief Requests for which Send returned no decoded response.
				 * @return Cumulative count of requests without decoded responses.
				 */
				std::uint64_t RequestsWithoutResponse() const noexcept;

				/**
				 * @brief Number of completed request-latency samples.
				 * @return Cumulative completed round-trip sample count.
				 */
				std::uint64_t RequestLatencySamples() const noexcept;

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

				/**
				 * @brief Cumulative Connect calls.
				 */
				std::atomic<std::uint64_t> m_connection_attempts{0};

				/**
				 * @brief Cumulative successful Connect calls.
				 */
				std::atomic<std::uint64_t> m_connections_established{0};

				/**
				 * @brief Cumulative failed Connect calls.
				 */
				std::atomic<std::uint64_t> m_connection_failures{0};

				/**
				 * @brief Current recorded application-connection state.
				 */
				std::atomic<bool> m_connected{false};

				/**
				 * @brief Cumulative Client::Send calls.
				 */
				std::atomic<std::uint64_t> m_requests{0};

				/**
				 * @brief Cumulative requests with decoded responses.
				 */
				std::atomic<std::uint64_t> m_responses{0};

				/**
				 * @brief Cumulative requests without decoded responses.
				 */
				std::atomic<std::uint64_t> m_requests_without_response{0};

				/**
				 * @brief Completed round-trip samples, with or without a response.
				 */
				std::atomic<std::uint64_t> m_latency_samples{0};

				/**
				 * @brief Sum of nonnegative recorded round-trip microseconds.
				 */
				std::atomic<std::uint64_t> m_latency_us{0};
		};
	}
}

/**
 * @brief Client telemetry has Network-owned lifecycle operations.
 * @details Base and Network must remain loaded with a compatible ABI until
 * every shared handle is released.
 */
STORMBYTE_DECLARE_MAYBE_SAFE(StormByte::Network::ClientTelemetry);
