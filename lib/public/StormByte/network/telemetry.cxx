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

#include <StormByte/network/telemetry.hxx>

#include <algorithm>
#include <array>
#include <atomic>
#include <charconv>
#include <cstdint>
#include <string_view>
#include <utility>

namespace StormByte::Network {
	namespace {
		std::size_t& SampleDepth() noexcept {
			static thread_local std::size_t depth = 0;
			return depth;
		}

		std::uint64_t SampleThreadId() noexcept {
			static std::atomic<std::uint64_t> next_id{0};
			static thread_local const std::uint64_t id = next_id.fetch_add(1, std::memory_order_relaxed);
			return id;
		}

	}

	Telemetry::Telemetry() noexcept = default;
	Telemetry::~Telemetry() noexcept = default;

	Telemetry::Sample Telemetry::Measure(const std::string_view name) noexcept {
		return Sample(*this, name);
	}

	Telemetry::Sample::Sample(Telemetry& owner, const std::string_view name) noexcept:
		m_depth(&SampleDepth()) {
		std::array<char, 128> clock_name{};
		std::size_t used = 0;
		auto append = [&clock_name, &used](const std::string_view value) noexcept {
			const std::size_t count = std::min(value.size(), clock_name.size() - used);
			std::copy_n(value.data(), count, clock_name.data() + used);
			used += count;
		};
		auto append_number = [&clock_name, &used](const std::uint64_t value) noexcept {
			if (used >= clock_name.size()) return;
			const auto result = std::to_chars(clock_name.data() + used, clock_name.data() + clock_name.size(), value);
			used = static_cast<std::size_t>(result.ptr - clock_name.data());
		};
		append("Network.");
		append(name);
		append(".");
		append_number(reinterpret_cast<std::uintptr_t>(&owner));
		append(".");
		append_number(SampleThreadId());
		append(".");
		append_number(SampleDepth());
		m_clock = &owner.Clock(std::string_view{clock_name.data(), used});
		m_before = m_clock->Time();
		m_clock->Start();
		++*m_depth;
	}

	Telemetry::Sample::Sample(Sample&& other) noexcept:
		m_clock(std::exchange(other.m_clock, nullptr)),
		m_before(other.m_before),
		m_elapsed(other.m_elapsed),
		m_depth(std::exchange(other.m_depth, nullptr)) {}

	Telemetry::Sample::~Sample() noexcept {
		(void)Stop();
	}

	std::chrono::microseconds Telemetry::Sample::Stop() noexcept {
		if (!m_clock) return m_elapsed;
		m_clock->Stop();
		m_elapsed = m_clock->Time() - m_before;
		m_clock = nullptr;
		if (m_depth) {
			--*m_depth;
			m_depth = nullptr;
		}
		return m_elapsed;
	}
}