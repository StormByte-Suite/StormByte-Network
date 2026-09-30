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

#include <StormByte/network/event_loop.hxx>

#ifdef UNIX
#include <poll.h>
#include <unistd.h>
#else
#include <winsock2.h>
#include <ws2tcpip.h>
#endif

#include <array>

namespace StormByte::Network::Detail {
	EventLoop::EventLoop(Socket::Server& listener, Connection::HandlerType wakeup_read,
		const std::atomic<Connection::Status>& status,
		StormByte::Shared<Logger::Log> logger) noexcept:
	m_listener(listener), m_wakeup_read(wakeup_read), m_status(status), m_logger(std::move(logger)) {}

	Expected<EventLoop::Event, ConnectionClosed> EventLoop::Wait(const SessionList& sessions) noexcept {
		for (const auto& session: sessions) {
			if (session->ReadyForProcessing()) {
				return Event{ EventKind::Session, session, true, session->HasOutput() };
			}
		}
#ifdef UNIX
		std::vector<pollfd> descriptors;
		descriptors.reserve(2 + sessions.size());
		descriptors.push_back({ m_listener.Handle(), POLLIN, 0 });
		descriptors.push_back({ m_wakeup_read, POLLIN, 0 });
		for (const auto& session: sessions) {
			const short events = static_cast<short>((session->CanRead() ? POLLIN : 0) | (session->HasOutput() ? POLLOUT : 0));
			descriptors.push_back({ session->Handle(), events, 0 });
		}

		const int result = poll(descriptors.data(), descriptors.size(), 1000);
		if (result < 0) {
			return Unexpected<ConnectionClosed>("Failed to wait for server events");
		}

		if (result == 0) {
			return Event{ EventKind::Timeout, nullptr };
		}

		if (descriptors[1].revents & POLLIN) {
			char signal;
			[[maybe_unused]] const ssize_t received = ::read(m_wakeup_read, &signal, sizeof(signal));
			return Event{ EventKind::Wakeup, nullptr };
		}

		if (descriptors[0].revents & POLLIN) {
			return Event{ EventKind::Listener, nullptr };
		}

		for (std::size_t index = 0; index < sessions.size(); ++index) {
			short terminal_events = POLLIN | POLLOUT | POLLERR | POLLHUP;
#ifdef POLLRDHUP
			terminal_events = static_cast<short>(terminal_events | POLLRDHUP);
#endif
			if (descriptors[index + 2].revents & terminal_events) {
				short readable_events = POLLIN | POLLERR | POLLHUP;
#ifdef POLLRDHUP
				readable_events = static_cast<short>(readable_events | POLLRDHUP);
#endif
				return Event{ EventKind::Session, sessions[index],
					(descriptors[index + 2].revents & readable_events) != 0,
					(descriptors[index + 2].revents & POLLOUT) != 0 };
			}
		}

		return Unexpected<ConnectionClosed>("Server reported an invalid event");
#else
		fd_set read_fds;
		fd_set write_fds;
		FD_ZERO(&read_fds);
		FD_ZERO(&write_fds);
		FD_SET(m_listener.Handle(), &read_fds);
		FD_SET(m_wakeup_read, &read_fds);
		std::shared_ptr<Session> ready_session;
		for (const auto& session: sessions) {
			if (session->CanRead()) {
				FD_SET(session->Handle(), &read_fds);
			}

			if (session->HasOutput()) {
				FD_SET(session->Handle(), &write_fds);
			}
		}

		timeval timeout{ .tv_sec = 1, .tv_usec = 0 };
		const int result = select(0, &read_fds, &write_fds, nullptr, &timeout);
		if (result == SOCKET_ERROR) {
			return Unexpected<ConnectionClosed>("Failed to wait for server events");
		}

		if (result == 0) {
			return Event{ EventKind::Timeout, nullptr };
		}

		if (FD_ISSET(m_wakeup_read, &read_fds)) {
			char signal;
			(void)::recv(m_wakeup_read, &signal, sizeof(signal), 0);
			return Event{ EventKind::Wakeup, nullptr };
		}

		if (FD_ISSET(m_listener.Handle(), &read_fds)) {
			return Event{ EventKind::Listener, nullptr };
		}

		for (const auto& session: sessions) {
			const bool readable = session->CanRead() && FD_ISSET(session->Handle(), &read_fds);
			const bool writable = session->HasOutput() && FD_ISSET(session->Handle(), &write_fds);
			if (readable || writable) {
				ready_session = session;
				return Event{ EventKind::Session, std::move(ready_session), readable, writable };
			}
		}

		return Unexpected<ConnectionClosed>("Server reported an invalid event");
#endif
	}

	void EventLoop::Run(const ListenerCallback& on_listener_ready,
		const SessionSnapshot& snapshot,
		const SessionCallback& on_session_ready,
		const WakeupCallback& on_wakeup) noexcept {
		while (Connection::IsConnected(m_status.load(std::memory_order_acquire))) {
			auto wait_result = Wait(snapshot());
			if (!wait_result) {
				m_logger << Logger::Level::Error << wait_result.error()->what() << std::endl;
				return;
			}

			switch (wait_result->kind) {
				case EventKind::Listener:
					on_listener_ready();
					break;
				case EventKind::Session:
					on_session_ready(wait_result->session, wait_result->readable, wait_result->writable);
					break;
				case EventKind::Wakeup:
					if (Connection::IsConnected(m_status.load(std::memory_order_acquire))) {
						on_wakeup();
					}

					break;
				case EventKind::Timeout:
					break;
			}
		}
	}
}
