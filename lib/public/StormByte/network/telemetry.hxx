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

#include <StormByte/network/visibility.h>
#include <StormByte/safe/string.hxx>
#include <StormByte/telemetry.hxx>

#include <chrono>
#include <string_view>

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
		 * @class Telemetry
		 * @brief Base for per-client and aggregate server telemetry snapshots.
		 *
		 * Timed operations use Base's named clocks. Each active sample receives
		 * a clock unique to its telemetry object, thread and nesting depth, so
		 * concurrent operations never start or stop the same Clock instance.
		 */
		class STORMBYTE_NETWORK_PUBLIC Telemetry: public StormByte::Telemetry {
			public:
				/** @brief Virtual destructor anchors telemetry RTTI in Network. */
				~Telemetry() noexcept override;

				/**
				 * @brief Flatten this snapshot to owned UTF-8 text.
				 * @return Telemetry text.
				 */
				virtual operator StormByte::Safe::String() const override = 0;

			protected:
				/** @brief Construct an empty Network telemetry base. */
				Telemetry() noexcept;

				/**
				 * @class Sample
				 * @brief RAII measurement using one Base named clock.
				 */
				class Sample final {
					public:
						/** @brief Copying an active clock sample is disabled. */
						Sample(const Sample&) = delete;

						/**
						 * @brief Transfer responsibility for stopping the clock.
						 * @param other Active sample to take.
						 */
						Sample(Sample&& other) noexcept;

						/** @brief Copy assignment is disabled. */
						Sample& operator=(const Sample&) = delete;

						/** @brief Move assignment is disabled. */
						Sample& operator=(Sample&&) = delete;

						/** @brief Stop the clock if still active. */
						~Sample() noexcept;

						/**
						 * @brief Stop and return this operation's elapsed time.
						 * @return Elapsed microseconds; repeated calls return the same value.
						 */
						std::chrono::microseconds Stop() noexcept;

					private:
						friend class Telemetry;

						/**
						 * @brief Start a named Base clock for this thread and nesting depth.
						 * @param owner Telemetry object owning the clock drawer.
						 * @param name Operation label.
						 */
						Sample(Telemetry& owner, std::string_view name) noexcept;

						StormByte::Clock* m_clock{nullptr}; ///< Base clock while the sample is active.
						std::chrono::microseconds m_before{0}; ///< Accumulated clock time before this sample.
						std::chrono::microseconds m_elapsed{0}; ///< Elapsed time after stopping.
						std::size_t* m_depth{nullptr}; ///< Creating thread's active sample depth.
				};

				/**
				 * @brief Start a named operation sample.
				 * @param name Stable operation label.
				 * @return Scoped Base-clock sample.
				 */
				Sample Measure(std::string_view name) noexcept;
		};
	}
}