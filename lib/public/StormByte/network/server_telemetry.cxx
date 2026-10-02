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

#include <StormByte/network/server_telemetry.hxx>

#include <algorithm>
#include <format>

namespace StormByte::Network {
	ServerTelemetry::ServerTelemetry() noexcept = default;
	ServerTelemetry::~ServerTelemetry() noexcept = default;

	std::uint64_t ServerTelemetry::CurrentConnections() const noexcept {
		return m_current_connections.load(std::memory_order_relaxed);
	}

	std::uint64_t ServerTelemetry::AcceptedConnections() const noexcept {
		return m_accepted_connections.load(std::memory_order_relaxed);
	}

	std::uint64_t ServerTelemetry::ClosedConnections() const noexcept {
		return m_closed_connections.load(std::memory_order_relaxed);
	}

	std::uint64_t ServerTelemetry::PeakConnections() const noexcept {
		return m_peak_connections.load(std::memory_order_relaxed);
	}

	std::uint64_t ServerTelemetry::PacketsDispatched() const noexcept {
		return m_packets_dispatched.load(std::memory_order_relaxed);
	}

	std::uint64_t ServerTelemetry::HandlersCompleted() const noexcept {
		return m_handlers_completed.load(std::memory_order_relaxed);
	}

	std::uint64_t ServerTelemetry::HandlersWithoutResponse() const noexcept {
		return m_handlers_without_response.load(std::memory_order_relaxed);
	}

	std::uint64_t ServerTelemetry::HandlerErrors() const noexcept {
		return m_handler_errors.load(std::memory_order_relaxed);
	}

	std::uint64_t ServerTelemetry::HandlerLatencySamples() const noexcept {
		return m_latency_samples.load(std::memory_order_relaxed);
	}

	std::chrono::microseconds ServerTelemetry::MeanHandlerLatency() const noexcept {
		const std::uint64_t samples = HandlerLatencySamples();
		return samples == 0 ? std::chrono::microseconds{0}
			: std::chrono::microseconds{m_latency_us.load(std::memory_order_relaxed) / samples};
	}

	std::uint64_t ServerTelemetry::WorkerQueueBackpressureEvents() const noexcept {
		return m_queue_backpressure_events.load(std::memory_order_relaxed);
	}

	ServerTelemetry::operator StormByte::Safe::String() const {
		return StormByte::Safe::String{std::format(
			"CurrentConnections={} AcceptedConnections={} ClosedConnections={} PeakConnections={} "
			"PacketsDispatched={} HandlersCompleted={} HandlersWithoutResponse={} HandlerErrors={} HandlerLatencySamples={} "
			"MeanHandlerLatencyUs={} WorkerQueueBackpressureEvents={}",
			CurrentConnections(), AcceptedConnections(), ClosedConnections(), PeakConnections(),
			PacketsDispatched(), HandlersCompleted(), HandlersWithoutResponse(), HandlerErrors(), HandlerLatencySamples(),
			MeanHandlerLatency().count(), WorkerQueueBackpressureEvents())};
	}

	void ServerTelemetry::RecordConnectionAccepted() noexcept {
		m_accepted_connections.fetch_add(1, std::memory_order_relaxed);
		const std::uint64_t current = m_current_connections.fetch_add(1, std::memory_order_relaxed) + 1;
		std::uint64_t peak = m_peak_connections.load(std::memory_order_relaxed);
		while (peak < current && !m_peak_connections.compare_exchange_weak(peak, current,
			std::memory_order_relaxed, std::memory_order_relaxed)) {}
	}

	void ServerTelemetry::RecordConnectionClosed() noexcept {
		m_closed_connections.fetch_add(1, std::memory_order_relaxed);
		m_current_connections.fetch_sub(1, std::memory_order_relaxed);
	}

	void ServerTelemetry::RecordPacketDispatched() noexcept {
		m_packets_dispatched.fetch_add(1, std::memory_order_relaxed);
	}

	void ServerTelemetry::RecordHandlerResult(const std::chrono::microseconds elapsed,
		const bool succeeded) noexcept {
		m_latency_us.fetch_add(static_cast<std::uint64_t>(std::max<std::int64_t>(0, elapsed.count())),
			std::memory_order_relaxed);
		m_latency_samples.fetch_add(1, std::memory_order_relaxed);
		(succeeded ? m_handlers_completed : m_handlers_without_response).fetch_add(1, std::memory_order_relaxed);
	}

	void ServerTelemetry::RecordHandlerError() noexcept {
		m_handler_errors.fetch_add(1, std::memory_order_relaxed);
	}

	Telemetry::Sample ServerTelemetry::MeasureHandler() {
		return Measure("Server.ProcessClientPacket");
	}

	void ServerTelemetry::RecordWorkerQueueBackpressure() noexcept {
		m_queue_backpressure_events.fetch_add(1, std::memory_order_relaxed);
	}
}