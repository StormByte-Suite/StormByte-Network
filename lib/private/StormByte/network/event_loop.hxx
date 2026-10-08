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

#include <StormByte/network/remote_file_host.hxx>
#include <StormByte/network/session.hxx>
#include <StormByte/network/socket/server.hxx>
#include <StormByte/network/typedefs.hxx>
#include <StormByte/network/visibility.h>
#include <StormByte/safe/atomic.hxx>
#include <StormByte/safe/function.hxx>
#include <StormByte/safe/vector.hxx>

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
		 * @namespace StormByte::Network::Detail
		 * @brief Private implementation details of the Network module.
		 */
		namespace Detail {
			/**
			 * @class EventLoop
			 * @brief Private listener, wakeup and endpoint I/O dispatcher.
			 * @note Listener, wakeup handle and status are borrowed for the loop's
			 * lifetime. Callbacks run synchronously on the Run caller and must not
			 * destroy the loop. Packet processing is delegated by those callbacks.
			 */
			class STORMBYTE_NETWORK_PRIVATE EventLoop final {
				public:
					/**
					 * @brief Provider-owned listener-ready callback.
					 */
					using ListenerCallback = Safe::Function<void()>;

					/**
					 * @brief Base-owned snapshot of Network-owned sessions.
					 */
					using SessionList = Safe::Vector<Safe::Shared<Session>>;

					/**
					 * @brief Callback writing a session snapshot to caller-owned storage.
					 */
					using SessionSnapshot = Safe::Function<SessionList()>;

					/**
					 * @brief Synchronously borrow a ready session and its I/O flags.
					 */
					using SessionCallback = Safe::Function<void(const Safe::Shared<Session>&, bool, bool)>;

					/**
					 * @brief Base-owned snapshot of Network-owned peer data planes.
					 */
					using PlaneList = Safe::Vector<Safe::Shared<RemoteFile::Host>>;

					/**
					 * @brief Callback writing a data-plane snapshot to caller-owned storage.
					 */
					using PlaneSnapshot = Safe::Function<PlaneList()>;

					/**
					 * @brief Synchronously borrow a ready data plane and its I/O flags.
					 */
					using PlaneCallback = Safe::Function<void(const Safe::Shared<RemoteFile::Host>&, bool, bool)>;

					/**
					 * @brief Provider-owned wakeup callback, invoked only while connected.
					 */
					using WakeupCallback = Safe::Function<void()>;

					/**
					 * @brief Bind to borrowed listener, wakeup channel and lifecycle status.
					 * @param listener Listening socket, kept alive by its owner.
					 * @param wakeup_read Read end of the private wakeup channel.
					 * @param status Safe lifecycle word, kept alive by its owner.
					 * @param logger Shared diagnostic logger; may be empty.
					 */
					EventLoop(Socket::Server& listener, Connection::HandlerType wakeup_read,
						const Safe::Atomic<Connection::Status>& status, Safe::Shared<Logger::Log> logger) noexcept;

					/**
					 * @brief Copy construction is not supported.
					 * @param other Loop that cannot be copied.
					 */
					EventLoop(const EventLoop& other) = delete;

					/**
					 * @brief Move construction is not supported for borrowed bindings.
					 * @param other Loop that cannot be moved.
					 */
					EventLoop(EventLoop&& other) = delete;

					/**
					 * @brief Release Safe owners without closing borrowed OS resources.
					 */
					~EventLoop() noexcept;

					/**
					 * @brief Copy assignment is not supported.
					 * @param other Loop that cannot be copied.
					 * @return This loop; operation is deleted.
					 */
					EventLoop& operator=(const EventLoop& other) = delete;

					/**
					 * @brief Move assignment is not supported.
					 * @param other Loop that cannot be moved.
					 * @return This loop; operation is deleted.
					 */
					EventLoop& operator=(EventLoop&& other) = delete;

					/**
					 * @brief Dispatch until disconnected or a wait or callback fails.
					 * @param on_listener_ready Called when the listener is readable.
					 * @param snapshot Supplies the current session owners.
					 * @param on_session_ready Receives a session and readable/writable flags.
					 * @param plane_snapshot Supplies the current data-plane owners.
					 * @param on_plane_ready Receives a plane and readable/writable flags.
					 * @param on_wakeup Called after consuming a signal while still connected.
					 * @note Missing or failed callbacks stop the loop; exceptions do not escape.
					 */
					void Run(const ListenerCallback& on_listener_ready, const SessionSnapshot& snapshot,
						const SessionCallback& on_session_ready, const PlaneSnapshot& plane_snapshot,
						const PlaneCallback& on_plane_ready, const WakeupCallback& on_wakeup) noexcept;

				private:
					/**
					 * @brief Kind of native or buffered readiness event.
					 */
					enum class EventKind: unsigned short { Timeout, Listener, Session, DataPlane, Wakeup };

					/**
					 * @brief One readiness event retaining its Network-owned endpoint.
					 */
					struct Event {
						EventKind kind;				///< Event type.
						Safe::Shared<Session> session;		///< Session owner for a session event.
						bool readable = false;			///< Read or terminal readiness.
						bool writable = false;			///< Write readiness.
						Safe::Shared<RemoteFile::Host> plane;	///< Plane owner for a data-plane event.
					};

					/**
					 * @brief Wait for native readiness or an already buffered endpoint.
					 * @param sessions Current session snapshot, with no empty owners.
					 * @param planes Current plane snapshot, with no empty owners.
					 * @return Ready event or a wait failure.
					 */
					Expected<Event, ConnectionClosed> Wait(const SessionList& sessions, const PlaneList& planes);

					Socket::Server& m_listener;				///< Borrowed listening socket.
					Connection::HandlerType m_wakeup_read;			///< Borrowed wakeup read handle.
					const Safe::Atomic<Connection::Status>& m_status;		///< Borrowed Safe lifecycle word.
					Safe::Shared<Logger::Log> m_logger;			///< Shared diagnostic logger.
			};
		}
	}
}

STORMBYTE_DECLARE_MAYBE_SAFE(StormByte::Network::Detail::EventLoop);
