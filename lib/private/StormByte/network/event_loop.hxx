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

#include <StormByte/network/socket/server.hxx>
#include <StormByte/network/session.hxx>
#include <StormByte/network/typedefs.hxx>
#include <StormByte/network/visibility.h>

#include <atomic>
#include <functional>
#include <memory>
#include <vector>

namespace StormByte::Network::Detail {
	/**
	 * @class EventLoop
	 * @brief Private listener and wakeup event loop.
	 *
	 * The loop owns listener, wakeup, and session I/O. Packet processing is
	 * intentionally synchronous here until a bounded worker pool is introduced.
	 */
	class STORMBYTE_NETWORK_PRIVATE EventLoop final {
		public:
			using ListenerCallback = std::function<void()>; ///< Listener-ready callback.
			using SessionList = std::vector<std::shared_ptr<Session>>; ///< Session snapshot.
			using SessionSnapshot = std::function<SessionList()>; ///< Session snapshot callback.
			using SessionCallback = std::function<void(const std::shared_ptr<Session>&, bool, bool)>; ///< Session-ready callback.
			using WakeupCallback = std::function<void()>; ///< Wakeup callback.

			/**
			 * @brief Bind the loop to a listener and wakeup read handle.
			 * @param listener Listening socket.
			 * @param wakeup_read Read end of the private wakeup channel.
			 * @param status Server lifecycle status.
			 * @param logger Diagnostic logger.
			 */
			EventLoop(Socket::Server& listener, Connection::HandlerType wakeup_read,
				const std::atomic<Connection::Status>& status,
				StormByte::Shared<Logger::Log> logger) noexcept;

			/**
			 * @brief Run until the server stops or the wakeup is signalled.
			 * @param on_listener_ready Called when the listener is readable.
			 */
			void Run(const ListenerCallback& on_listener_ready,
				const SessionSnapshot& snapshot,
				const SessionCallback& on_session_ready,
				const WakeupCallback& on_wakeup) noexcept;

		private:
			Socket::Server& m_listener; ///< Listening socket.
			Connection::HandlerType m_wakeup_read; ///< Wakeup read handle.
			const std::atomic<Connection::Status>& m_status; ///< Server status.
			StormByte::Shared<Logger::Log> m_logger; ///< Diagnostic logger.

			enum class EventKind: unsigned short { Timeout, Listener, Session, Wakeup }; ///< Wait event kind.
			struct Event { EventKind kind; std::shared_ptr<Session> session; bool readable = false; bool writable = false; }; ///< Wait event.

			/**
			 * @brief Wait for listener, wakeup, or a session descriptor.
			 * @param sessions Current session snapshot.
			 * @return Wait event.
			 */
			Expected<Event, ConnectionClosed> Wait(const SessionList& sessions) noexcept;
	};
}
