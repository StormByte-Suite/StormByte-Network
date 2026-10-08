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
		 * @class Server
		 * @brief Forward declaration of the server owning its telemetry.
		 */
		class Server;

		/**
		 * @class ServerTelemetry
		 * @brief Server-wide cumulative telemetry aggregated across all sessions.
		 */
		class STORMBYTE_NETWORK_PUBLIC ServerTelemetry final: public Telemetry {
			public:
				/**
				 * @brief Construct all server counters at zero.
				 */
				ServerTelemetry() noexcept;

				/**
				 * @brief Virtual destructor anchors server telemetry RTTI in Network.
				 */
				~ServerTelemetry() noexcept override;

				/**
				 * @brief Number of currently active application sessions.
				 * @return Current active-session count.
				 */
				Size CurrentConnections() const noexcept;

				/**
				 * @brief Total application sessions accepted by this Server.
				 * @return Cumulative accepted-session count.
				 */
				Size AcceptedConnections() const noexcept;

				/**
				 * @brief Total application sessions closed by this Server.
				 * @return Cumulative closed-session count.
				 */
				Size ClosedConnections() const noexcept;

				/**
				 * @brief Maximum simultaneous application sessions observed.
				 * @return Highest recorded active-session count.
				 */
				Size PeakConnections() const noexcept;

				/**
				 * @brief Valid application packets submitted to the worker pool.
				 * @return Cumulative dispatched-packet count.
				 */
				Size PacketsDispatched() const noexcept;

				/**
				 * @brief Packet handlers that returned a non-null response.
				 * @return Cumulative count of handlers returning responses.
				 */
				Size HandlersCompleted() const noexcept;

				/**
				 * @brief Packet handlers that returned no response.
				 * @return Cumulative no-response count, including intentional
				 * disconnects.
				 */
				Size HandlersWithoutResponse() const noexcept;

				/**
				 * @brief Worker-pool callbacks that completed with an error.
				 * @return Cumulative worker-pool error count.
				 */
				Size HandlerErrors() const noexcept;

				/**
				 * @brief Number of completed packet-handler duration samples.
				 * @return Cumulative completed handler-duration sample count.
				 */
				Size HandlerLatencySamples() const noexcept;

				/**
				 * @brief Mean ProcessClientPacket duration, or zero without samples.
				 * @return Mean elapsed microseconds, truncated to whole microseconds;
				 * zero when no samples have been recorded.
				 */
				std::chrono::microseconds MeanHandlerLatency() const noexcept;

				/**
				 * @brief Times a ready session was blocked by a full worker queue.
				 * @return Cumulative worker-queue backpressure event count.
				 */
				Size WorkerQueueBackpressureEvents() const noexcept;

				/**
				 * @brief Flatten the aggregate server counters to owned UTF-8 text.
				 * @return Owned text containing the server's telemetry counters.
				 */
				operator StormByte::Safe::String() const override;

			private:
				/**
				 * @brief Allow the owning server to record telemetry events.
				 */
				friend class Server;

				/**
				 * @brief Count a newly accepted application session.
				 * @details Increment accepted and active counts and update the peak
				 * simultaneous-session count.
				 */
				void RecordConnectionAccepted() noexcept;

				/**
				 * @brief Count a closed session and decrement the active count.
				 * @pre The session was previously counted as accepted and has not
				 * already been counted as closed.
				 */
				void RecordConnectionClosed() noexcept;

				/**
				 * @brief Record a packet submitted to ProcessClientPacket.
				 */
				void RecordPacketDispatched() noexcept;

				/**
				 * @brief Record a completed packet handler and its duration.
				 * @param elapsed Handler duration; negative values count as zero.
				 * @param succeeded Whether the handler returned a non-null response.
				 * @details Increment the latency sample count and the corresponding
				 * response or no-response counter; worker errors are recorded
				 * separately by RecordHandlerError().
				 */
				void RecordHandlerResult(std::chrono::microseconds elapsed, bool succeeded) noexcept;

				/**
				 * @brief Record an error reported by the worker pool.
				 */
				void RecordHandlerError() noexcept;

				/**
				 * @brief Start a Base named clock around ProcessClientPacket.
				 * @return Independent scoped sample for the Server.ProcessClientPacket
				 * clock.
				 */
				Sample MeasureHandler();

				/**
				 * @brief Record one transition blocked by the bounded worker queue.
				 */
				void RecordWorkerQueueBackpressure() noexcept;

				Safe::Atomic<std::uint64_t> m_current_connections{0};			///< Currently active application sessions.
				Safe::Atomic<std::uint64_t> m_accepted_connections{0};			///< Cumulative accepted application sessions.
				Safe::Atomic<std::uint64_t> m_closed_connections{0};			///< Cumulative closed application sessions.
				Safe::Atomic<std::uint64_t> m_peak_connections{0};				///< Peak simultaneous application sessions.
				Safe::Atomic<std::uint64_t> m_packets_dispatched{0};			///< Cumulative packets submitted to handlers.
				Safe::Atomic<std::uint64_t> m_handlers_completed{0};			///< Cumulative handlers returning a non-null response.
				Safe::Atomic<std::uint64_t> m_handlers_without_response{0};	///< Empty replies, including intentional disconnects.
				Safe::Atomic<std::uint64_t> m_handler_errors{0};				///< Cumulative worker-pool errors.
				Safe::Atomic<std::uint64_t> m_latency_samples{0};				///< Completed handler samples, with or without a response.
				Safe::Atomic<std::uint64_t> m_latency_us{0};					///< Sum of nonnegative recorded handler microseconds.
				Safe::Atomic<std::uint64_t> m_queue_backpressure_events{0};		///< Cumulative worker-queue backpressure events.
		};
	}
}

STORMBYTE_DECLARE_MAYBE_SAFE(StormByte::Network::ServerTelemetry);
