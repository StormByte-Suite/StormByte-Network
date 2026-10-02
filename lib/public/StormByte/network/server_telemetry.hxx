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
		class Server;

		/**
		 * @class ServerTelemetry
		 * @brief Server-wide cumulative telemetry aggregated across all sessions.
		 */
		class STORMBYTE_NETWORK_PUBLIC ServerTelemetry final: public Telemetry {
			public:
				/** @brief Construct all server counters at zero. */
				ServerTelemetry() noexcept;

				/** @brief Virtual destructor anchors server telemetry RTTI in Network. */
				~ServerTelemetry() noexcept override;

				/** @brief Number of currently active application sessions. */
				std::uint64_t CurrentConnections() const noexcept;

				/** @brief Total application sessions accepted by this Server. */
				std::uint64_t AcceptedConnections() const noexcept;

				/** @brief Total application sessions closed by this Server. */
				std::uint64_t ClosedConnections() const noexcept;

				/** @brief Maximum simultaneous application sessions observed. */
				std::uint64_t PeakConnections() const noexcept;

				/** @brief Valid application packets submitted to the worker pool. */
				std::uint64_t PacketsDispatched() const noexcept;

				/** @brief Packet handlers that returned a non-null response. */
				std::uint64_t HandlersCompleted() const noexcept;

				/** @brief Packet handlers that returned no response. */
				std::uint64_t HandlersWithoutResponse() const noexcept;

				/** @brief Worker-pool callbacks that completed with an error. */
				std::uint64_t HandlerErrors() const noexcept;

				/** @brief Number of completed packet-handler duration samples. */
				std::uint64_t HandlerLatencySamples() const noexcept;

				/** @brief Mean ProcessClientPacket duration, or zero without samples. */
				std::chrono::microseconds MeanHandlerLatency() const noexcept;

				/** @brief Times a ready session was blocked by a full worker queue. */
				std::uint64_t WorkerQueueBackpressureEvents() const noexcept;

				/** @brief Flatten the aggregate server counters to owned UTF-8 text. */
				operator StormByte::Safe::String() const override;

			private:
				friend class Server;

				/** @brief Record one newly accepted application session. */
				void RecordConnectionAccepted() noexcept;

				/** @brief Record one application session close. */
				void RecordConnectionClosed() noexcept;

				/** @brief Record a packet submitted to ProcessClientPacket. */
				void RecordPacketDispatched() noexcept;

				/** @brief Record a completed packet handler and its duration. */
				void RecordHandlerResult(std::chrono::microseconds elapsed, bool succeeded) noexcept;

				/** @brief Record an error reported by the worker pool. */
				void RecordHandlerError() noexcept;

				/** @brief Start a Base named clock around ProcessClientPacket. */
				Sample MeasureHandler();

				/** @brief Record one transition blocked by the bounded worker queue. */
				void RecordWorkerQueueBackpressure() noexcept;

				std::atomic<std::uint64_t> m_current_connections{0}; ///< Active application sessions.
				std::atomic<std::uint64_t> m_accepted_connections{0}; ///< Accepted application sessions.
				std::atomic<std::uint64_t> m_closed_connections{0}; ///< Closed application sessions.
				std::atomic<std::uint64_t> m_peak_connections{0}; ///< Peak simultaneous sessions.
				std::atomic<std::uint64_t> m_packets_dispatched{0}; ///< Packets submitted to handlers.
				std::atomic<std::uint64_t> m_handlers_completed{0}; ///< Handlers returning a response.
				std::atomic<std::uint64_t> m_handlers_without_response{0}; ///< Empty replies, including intentional disconnects.
				std::atomic<std::uint64_t> m_handler_errors{0}; ///< Worker-pool errors.
				std::atomic<std::uint64_t> m_latency_samples{0}; ///< Completed handler samples.
				std::atomic<std::uint64_t> m_latency_us{0}; ///< Sum of handler microseconds.
				std::atomic<std::uint64_t> m_queue_backpressure_events{0}; ///< Worker-queue backpressure events.
		};
	}
}