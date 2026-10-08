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
		 * Timed operations use independent Base samples sharing named clocks.
		 * Concurrent and nested operations keep their own start times.
		 */
		class STORMBYTE_NETWORK_PUBLIC Telemetry: public StormByte::Telemetry {
			public:
				/**
				 * @brief Virtual destructor anchors telemetry RTTI in Network.
				 */
				~Telemetry() noexcept override;

				/**
				 * @brief Flatten this snapshot to owned UTF-8 text.
				 * @return Telemetry text.
				 */
				virtual operator StormByte::Safe::String() const override = 0;

			protected:
				/**
				 * @brief Construct an empty Network telemetry base.
				 */
				Telemetry() noexcept;

				/**
				 * @brief Base-owned independent RAII measurement token.
				 * @details Samples may overlap, nest or move between threads.
				 * Stop is idempotent; destruction records an active sample.
				 */
				using Sample = StormByte::Clock::Sample;

				/**
				 * @brief Start a named operation sample.
				 * @param name Stable operation label.
				 * @return Scoped Base-clock sample.
				 */
				Sample Measure(std::string_view name) noexcept;
		};
	}
}

STORMBYTE_DECLARE_MAYBE_SAFE(StormByte::Network::Telemetry);
