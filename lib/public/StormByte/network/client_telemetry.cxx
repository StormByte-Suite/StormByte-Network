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

#include <StormByte/network/client_telemetry.hxx>

#include <algorithm>
#include <format>
#include <string>

namespace StormByte::Network {
	ClientTelemetry::ClientTelemetry() noexcept = default;

	ClientTelemetry::~ClientTelemetry() noexcept = default;

	Size ClientTelemetry::ConnectionAttempts() const noexcept {
		return Size{m_connection_attempts.load(Safe::MemoryOrder::Relaxed)};
	}

	Size ClientTelemetry::ConnectionsEstablished() const noexcept {
		return Size{m_connections_established.load(Safe::MemoryOrder::Relaxed)};
	}

	Size ClientTelemetry::ConnectionFailures() const noexcept {
		return Size{m_connection_failures.load(Safe::MemoryOrder::Relaxed)};
	}

	bool ClientTelemetry::Connected() const noexcept {
		return m_connected.load(Safe::MemoryOrder::Acquire);
	}

	Size ClientTelemetry::Requests() const noexcept {
		return Size{m_requests.load(Safe::MemoryOrder::Relaxed)};
	}

	Size ClientTelemetry::Responses() const noexcept {
		return Size{m_responses.load(Safe::MemoryOrder::Relaxed)};
	}

	Size ClientTelemetry::RequestsWithoutResponse() const noexcept {
		return Size{m_requests_without_response.load(Safe::MemoryOrder::Relaxed)};
	}

	Size ClientTelemetry::RequestLatencySamples() const noexcept {
		return Size{m_latency_samples.load(Safe::MemoryOrder::Relaxed)};
	}

	std::chrono::microseconds ClientTelemetry::MeanRequestLatency() const noexcept {
		const std::uint64_t samples = static_cast<std::uint64_t>(RequestLatencySamples());
		return samples == 0 ? std::chrono::microseconds{0}
			: std::chrono::microseconds{m_latency_us.load(Safe::MemoryOrder::Relaxed) / samples};
	}

	ClientTelemetry::operator StormByte::Safe::String() const {
		return StormByte::Safe::String{std::format(
			"ConnectAttempts={} ConnectionsEstablished={} ConnectionFailures={} Connected={} "
			"Requests={} Responses={} RequestsWithoutResponse={} RequestLatencySamples={} MeanRequestLatencyUs={}",
			static_cast<std::uint64_t>(ConnectionAttempts()), static_cast<std::uint64_t>(ConnectionsEstablished()),
			static_cast<std::uint64_t>(ConnectionFailures()), Connected(), static_cast<std::uint64_t>(Requests()),
			static_cast<std::uint64_t>(Responses()), static_cast<std::uint64_t>(RequestsWithoutResponse()),
			static_cast<std::uint64_t>(RequestLatencySamples()), MeanRequestLatency().count())};
	}

	void ClientTelemetry::RecordConnectionAttempt() noexcept {
		m_connection_attempts.fetch_add(std::uint64_t{1}, Safe::MemoryOrder::Relaxed);
	}

	void ClientTelemetry::RecordConnectionEstablished() noexcept {
		m_connections_established.fetch_add(std::uint64_t{1}, Safe::MemoryOrder::Relaxed);
		m_connected.store(true, Safe::MemoryOrder::Release);
	}

	void ClientTelemetry::RecordConnectionFailed() noexcept {
		m_connection_failures.fetch_add(std::uint64_t{1}, Safe::MemoryOrder::Relaxed);
	}

	void ClientTelemetry::RecordDisconnected() noexcept {
		m_connected.store(false, Safe::MemoryOrder::Release);
	}

	void ClientTelemetry::RecordRequest() noexcept {
		m_requests.fetch_add(std::uint64_t{1}, Safe::MemoryOrder::Relaxed);
	}

	void ClientTelemetry::RecordRequestResult(const std::chrono::microseconds elapsed,
		const bool responded) noexcept {
		m_latency_us.fetch_add(static_cast<std::uint64_t>(std::max<std::int64_t>(0, elapsed.count())),
			Safe::MemoryOrder::Relaxed);
		m_latency_samples.fetch_add(std::uint64_t{1}, Safe::MemoryOrder::Relaxed);
		(responded ? m_responses : m_requests_without_response).fetch_add(std::uint64_t{1}, Safe::MemoryOrder::Relaxed);
	}

	Telemetry::Sample ClientTelemetry::MeasureRequest() {
		return Measure("Client.Send");
	}
}
