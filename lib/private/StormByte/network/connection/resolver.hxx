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

#include <StormByte/network/connection/info.hxx>
#include <StormByte/network/socket/operation.hxx>
#include <StormByte/safe/mutex.hxx>
#include <StormByte/safe/thread.hxx>

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
		 * @namespace StormByte::Network::Connection
		 * @brief Private address resolution.
		 */
		namespace Connection {
			/**
			 * @brief Forward declaration of provider-owned resolver work.
			 */
			struct ResolutionJob;

			/**
			 * @brief Managed single-slot resolver; share one instance to bound DNS resources.
			 * @details No queue and no detached threads. Caller timeout/cancellation
			 * abandons the result, not getaddrinfo. Busy persists until the worker
			 * finishes. Destruction joins and may wait for the OS resolver indefinitely.
			 * Keep Network, Base and any lookup provider loaded until destruction;
			 * serialize destruction against all Resolve calls.
			 */
			class STORMBYTE_NETWORK_PRIVATE Resolver final {
				public:
					/**
					 * @brief Borrowed provider function, retained until this resolver is destroyed.
					 */
					using Lookup = Expected<Info::Addresses, Exception> (*)(
						std::string_view, unsigned short, AddressPolicy) noexcept;

					/**
					 * @brief Construct an idle slot inside Network.
					 * @param lookup Blocking provider lookup; defaults to the native resolver.
					 */
					explicit Resolver(Lookup lookup = Info::ResolveAll);

					/**
					 * @brief Wait for retained work and release it inside Network.
					 */
					~Resolver() noexcept;

					/**
					 * @brief A resolver's thread and admission gate cannot be copied.
					 * @param other Source resolver.
					 */
					Resolver(const Resolver& other) = delete;

					/**
					 * @brief A resolver cannot move while callers may be waiting.
					 * @param other Source resolver.
					 */
					Resolver(Resolver&& other) = delete;

					/**
					 * @brief Resolver assignment is disabled.
					 * @param other Source resolver.
					 * @return No value; operation is deleted.
					 */
					Resolver& operator=(const Resolver& other) = delete;

					/**
					 * @brief Resolver move assignment is disabled.
					 * @param other Source resolver.
					 * @return No value; operation is deleted.
					 */
					Resolver& operator=(Resolver&& other) = delete;

					/**
					 * @brief Resolve alternatives with bounded caller waiting and no queue.
					 * @param hostname Borrowed host name, copied before worker launch.
					 * @param port Destination port.
					 * @param policy Explicit address policy.
					 * @param control Total deadline and borrowed cancellation flag.
					 * @param state Complete, Busy, TimedOut, Cancelled or Error.
					 * @return Provider-owned alternatives, or descriptive error.
					 */
					Expected<Info::Addresses, Exception> Resolve(std::string_view hostname,
						unsigned short port, AddressPolicy policy,
						const Socket::OperationControl& control, Socket::OperationState& state) noexcept;

				private:
					Lookup m_lookup;	///< Borrowed provider function.
					Safe::Mutex m_gate;	///< Safe single-slot admission gate.
					Safe::Thread m_worker;	///< At most one joinable native execution.
					Safe::Shared<ResolutionJob> m_job;	///< Retained work, including abandoned work.
			};
		}
	}
}

STORMBYTE_DECLARE_MAYBE_SAFE(StormByte::Network::Connection::Resolver);
