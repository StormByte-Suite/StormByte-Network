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

#include <StormByte/network/server_telemetry.hxx>

#include <algorithm>
#include <format>

namespace StormByte::Network {
	ServerTelemetry::ServerTelemetry() noexcept = default;

	ServerTelemetry::~ServerTelemetry() noexcept = default;

	Size ServerTelemetry::CurrentConnections() const noexcept {
		return Size{m_current_connections.load(Safe::MemoryOrder::Relaxed)};
	}

	Size ServerTelemetry::AcceptedConnections() const noexcept {
		return Size{m_accepted_connections.load(Safe::MemoryOrder::Relaxed)};
	}

	Size ServerTelemetry::ClosedConnections() const noexcept {
		return Size{m_closed_connections.load(Safe::MemoryOrder::Relaxed)};
	}

	Size ServerTelemetry::PeakConnections() const noexcept {
		return Size{m_peak_connections.load(Safe::MemoryOrder::Relaxed)};
	}

	Size ServerTelemetry::PacketsDispatched() const noexcept {
		return Size{m_packets_dispatched.load(Safe::MemoryOrder::Relaxed)};
	}

	Size ServerTelemetry::HandlersCompleted() const noexcept {
		return Size{m_handlers_completed.load(Safe::MemoryOrder::Relaxed)};
	}

	Size ServerTelemetry::HandlersWithoutResponse() const noexcept {
		return Size{m_handlers_without_response.load(Safe::MemoryOrder::Relaxed)};
	}

	Size ServerTelemetry::HandlerErrors() const noexcept {
		return Size{m_handler_errors.load(Safe::MemoryOrder::Relaxed)};
	}

	Size ServerTelemetry::HandlerLatencySamples() const noexcept {
		return Size{m_latency_samples.load(Safe::MemoryOrder::Relaxed)};
	}

	std::chrono::microseconds ServerTelemetry::MeanHandlerLatency() const noexcept {
		const std::uint64_t samples = static_cast<std::uint64_t>(HandlerLatencySamples());
		return samples == 0 ? std::chrono::microseconds{0}
			: std::chrono::microseconds{m_latency_us.load(Safe::MemoryOrder::Relaxed) / samples};
	}

	Size ServerTelemetry::WorkerQueueBackpressureEvents() const noexcept {
		return Size{m_queue_backpressure_events.load(Safe::MemoryOrder::Relaxed)};
	}

	ServerTelemetry::operator StormByte::Safe::String() const {
		return StormByte::Safe::String{std::format(
			"CurrentConnections={} AcceptedConnections={} ClosedConnections={} PeakConnections={} "
			"PacketsDispatched={} HandlersCompleted={} HandlersWithoutResponse={} HandlerErrors={} HandlerLatencySamples={} "
			"MeanHandlerLatencyUs={} WorkerQueueBackpressureEvents={}",
			static_cast<std::uint64_t>(CurrentConnections()), static_cast<std::uint64_t>(AcceptedConnections()),
			static_cast<std::uint64_t>(ClosedConnections()), static_cast<std::uint64_t>(PeakConnections()),
			static_cast<std::uint64_t>(PacketsDispatched()), static_cast<std::uint64_t>(HandlersCompleted()),
			static_cast<std::uint64_t>(HandlersWithoutResponse()), static_cast<std::uint64_t>(HandlerErrors()),
			static_cast<std::uint64_t>(HandlerLatencySamples()), MeanHandlerLatency().count(),
			static_cast<std::uint64_t>(WorkerQueueBackpressureEvents()))};
	}

	void ServerTelemetry::RecordConnectionAccepted() noexcept {
		m_accepted_connections.fetch_add(std::uint64_t{1}, Safe::MemoryOrder::Relaxed);
		const std::uint64_t current = m_current_connections.fetch_add(std::uint64_t{1}, Safe::MemoryOrder::Relaxed) + 1;
		std::uint64_t peak = m_peak_connections.load(Safe::MemoryOrder::Relaxed);
		while (peak < current && !m_peak_connections.compare_exchange_weak(peak, current,
			Safe::MemoryOrder::Relaxed, Safe::MemoryOrder::Relaxed)) {}
	}

	void ServerTelemetry::RecordConnectionClosed() noexcept {
		m_closed_connections.fetch_add(std::uint64_t{1}, Safe::MemoryOrder::Relaxed);
		m_current_connections.fetch_sub(std::uint64_t{1}, Safe::MemoryOrder::Relaxed);
	}

	void ServerTelemetry::RecordPacketDispatched() noexcept {
		m_packets_dispatched.fetch_add(std::uint64_t{1}, Safe::MemoryOrder::Relaxed);
	}

	void ServerTelemetry::RecordHandlerResult(const std::chrono::microseconds elapsed,
		const bool succeeded) noexcept {
		m_latency_us.fetch_add(static_cast<std::uint64_t>(std::max<std::int64_t>(0, elapsed.count())),
			Safe::MemoryOrder::Relaxed);
		m_latency_samples.fetch_add(std::uint64_t{1}, Safe::MemoryOrder::Relaxed);
		(succeeded ? m_handlers_completed : m_handlers_without_response).fetch_add(std::uint64_t{1}, Safe::MemoryOrder::Relaxed);
	}

	void ServerTelemetry::RecordHandlerError() noexcept {
		m_handler_errors.fetch_add(std::uint64_t{1}, Safe::MemoryOrder::Relaxed);
	}

	Telemetry::Sample ServerTelemetry::MeasureHandler() {
		return Measure("Server.ProcessClientPacket");
	}

	void ServerTelemetry::RecordWorkerQueueBackpressure() noexcept {
		m_queue_backpressure_events.fetch_add(std::uint64_t{1}, Safe::MemoryOrder::Relaxed);
	}
}
