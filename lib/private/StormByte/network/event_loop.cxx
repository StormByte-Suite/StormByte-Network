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
#include <StormByte/byte_size.hxx>
#include <StormByte/size.hxx>

#include <ostream>
#include <utility>

#ifdef UNIX
#include <poll.h>
#include <unistd.h>
#include <vector>
#else
#include <winsock2.h>
#include <ws2tcpip.h>
#endif

using namespace StormByte;
using namespace StormByte::Network;
using namespace StormByte::Network::Detail;

EventLoop::EventLoop(Socket::Server& listener, Connection::HandlerType wakeup_read,
	const Safe::Atomic<Connection::Status>& status, Safe::Shared<Logger::Log> logger) noexcept:
m_listener(listener), m_wakeup_read(wakeup_read), m_status(status), m_logger(std::move(logger)) {}

EventLoop::~EventLoop() noexcept = default;

Expected<EventLoop::Event, ConnectionClosed> EventLoop::Wait(const SessionList& sessions,
	const PlaneList& planes) {
	for (const auto& session: sessions) {
		if (!session)
			return Unexpected<ConnectionClosed>("Server session snapshot contains an empty owner");
	}
	for (const auto& plane: planes) {
		if (!plane)
			return Unexpected<ConnectionClosed>("Server data-plane snapshot contains an empty owner");
	}
	for (const auto& session: sessions) {
		if (session->ReadyForProcessing())
			return Event{ EventKind::Session, session, true, session->HasOutput(), {} };
	}
	for (const auto& plane: planes) {
		if (plane->ReadyForProcessing() || plane->Expired()
			|| (plane->CanRead() && plane->HasBufferedFrame()))
			return Event{ EventKind::DataPlane, nullptr, true, plane->HasOutput(), plane };
	}
#ifdef UNIX
	std::vector<pollfd> poll_descriptors;
	const Size descriptor_count = Size{2} + Size{sessions.size()} + Size{planes.size()};
	poll_descriptors.reserve(static_cast<std::size_t>(descriptor_count));
	poll_descriptors.push_back({ m_listener.Handle(), POLLIN, 0 });
	poll_descriptors.push_back({ m_wakeup_read, POLLIN, 0 });
	for (const auto& session: sessions) {
		const short events = static_cast<short>((session->CanRead() ? POLLIN : 0) | (session->HasOutput() ? POLLOUT : 0));
		poll_descriptors.push_back({ session->Handle(), events, 0 });
	}
	for (const auto& plane: planes) {
		const short events = static_cast<short>((plane->CanRead() ? POLLIN : 0) | (plane->HasOutput() ? POLLOUT : 0));
		poll_descriptors.push_back({ plane->Handle(), events, 0 });
	}

	const int result = poll(poll_descriptors.data(), static_cast<nfds_t>(descriptor_count), 1000);
	if (result < 0)
		return Unexpected<ConnectionClosed>("Failed to wait for server events");

	if (result == 0)
		return Event{ EventKind::Timeout, nullptr, false, false, {} };

	if ((poll_descriptors[0].revents | poll_descriptors[1].revents) & (POLLERR | POLLHUP | POLLNVAL))
		return Unexpected<ConnectionClosed>("Server listener or wakeup handle is invalid");

	if (poll_descriptors[1].revents & POLLIN) {
		char signal;
		const ByteSize signal_size{sizeof(signal)};
		[[maybe_unused]] const ssize_t received = ::read(m_wakeup_read, &signal, static_cast<std::size_t>(signal_size));
		return Event{ EventKind::Wakeup, nullptr, false, false, {} };
	}

	if (poll_descriptors[0].revents & POLLIN)
		return Event{ EventKind::Listener, nullptr, false, false, {} };

	for (Size index{0}; index < Size{sessions.size()}; ++index) {
		short terminal_events = POLLIN | POLLOUT | POLLERR | POLLHUP | POLLNVAL;
#ifdef POLLRDHUP
		terminal_events = static_cast<short>(terminal_events | POLLRDHUP);
#endif
		const short events = poll_descriptors[static_cast<std::size_t>(index + Size{2})].revents;
		if (events & terminal_events) {
			short readable_events = POLLIN | POLLERR | POLLHUP | POLLNVAL;
#ifdef POLLRDHUP
			readable_events = static_cast<short>(readable_events | POLLRDHUP);
#endif
			return Event{ EventKind::Session, sessions[static_cast<std::size_t>(index)],
				(events & readable_events) != 0, (events & POLLOUT) != 0, {} };
		}
	}
	for (Size index{0}; index < Size{planes.size()}; ++index) {
		const Size descriptor_index = index + Size{2} + Size{sessions.size()};
		const short events = poll_descriptors[static_cast<std::size_t>(descriptor_index)].revents;
		short terminal_events = POLLIN | POLLOUT | POLLERR | POLLHUP | POLLNVAL;
#ifdef POLLRDHUP
		terminal_events = static_cast<short>(terminal_events | POLLRDHUP);
#endif
		if (events & terminal_events) {
			short readable_events = POLLIN | POLLERR | POLLHUP | POLLNVAL;
#ifdef POLLRDHUP
			readable_events = static_cast<short>(readable_events | POLLRDHUP);
#endif
			return Event{ EventKind::DataPlane, nullptr, (events & readable_events) != 0,
				(events & POLLOUT) != 0, planes[static_cast<std::size_t>(index)] };
		}
	}
#else
	fd_set read_fds;
	fd_set write_fds;
	FD_ZERO(&read_fds);
	FD_ZERO(&write_fds);
	FD_SET(m_listener.Handle(), &read_fds);
	FD_SET(m_wakeup_read, &read_fds);
	for (const auto& session: sessions) {
		if (session->CanRead()) {
			if (Size{read_fds.fd_count} >= Size{FD_SETSIZE})
				return Unexpected<ConnectionClosed>("Server read poller capacity exceeded");
			FD_SET(session->Handle(), &read_fds);
		}
		if (session->HasOutput()) {
			if (Size{write_fds.fd_count} >= Size{FD_SETSIZE})
				return Unexpected<ConnectionClosed>("Server write poller capacity exceeded");
			FD_SET(session->Handle(), &write_fds);
		}
	}
	for (const auto& plane: planes) {
		if (plane->CanRead()) {
			if (Size{read_fds.fd_count} >= Size{FD_SETSIZE})
				return Unexpected<ConnectionClosed>("Server read poller capacity exceeded");
			FD_SET(plane->Handle(), &read_fds);
		}
		if (plane->HasOutput()) {
			if (Size{write_fds.fd_count} >= Size{FD_SETSIZE})
				return Unexpected<ConnectionClosed>("Server write poller capacity exceeded");
			FD_SET(plane->Handle(), &write_fds);
		}
	}

	timeval timeout{ .tv_sec = 1, .tv_usec = 0 };
	const int result = select(0, &read_fds, &write_fds, nullptr, &timeout);
	if (result == SOCKET_ERROR)
		return Unexpected<ConnectionClosed>("Failed to wait for server events");

	if (result == 0)
		return Event{ EventKind::Timeout, nullptr, false, false, {} };

	if (FD_ISSET(m_wakeup_read, &read_fds)) {
		char signal;
		const ByteSize signal_size{sizeof(signal)};
		(void)::recv(m_wakeup_read, &signal, static_cast<int>(signal_size), 0);
		return Event{ EventKind::Wakeup, nullptr, false, false, {} };
	}

	if (FD_ISSET(m_listener.Handle(), &read_fds))
		return Event{ EventKind::Listener, nullptr, false, false, {} };

	for (const auto& session: sessions) {
		const bool readable = session->CanRead() && FD_ISSET(session->Handle(), &read_fds);
		const bool writable = session->HasOutput() && FD_ISSET(session->Handle(), &write_fds);
		if (readable || writable)
			return Event{ EventKind::Session, session, readable, writable, {} };
	}
	for (const auto& plane: planes) {
		const bool readable = plane->CanRead() && FD_ISSET(plane->Handle(), &read_fds);
		const bool writable = plane->HasOutput() && FD_ISSET(plane->Handle(), &write_fds);
		if (readable || writable)
			return Event{ EventKind::DataPlane, nullptr, readable, writable, plane };
	}
#endif
	return Unexpected<ConnectionClosed>("Server reported an invalid event");
}

void EventLoop::Run(const ListenerCallback& on_listener_ready, const SessionSnapshot& snapshot,
	const SessionCallback& on_session_ready, const PlaneSnapshot& plane_snapshot,
	const PlaneCallback& on_plane_ready, const WakeupCallback& on_wakeup) noexcept {
	try {
		while (Connection::IsConnected(m_status.load(Safe::MemoryOrder::Acquire))) {
			SessionList sessions;
			PlaneList planes;
			if (snapshot.Call(sessions) != Safe::Status::Success || plane_snapshot.Call(planes) != Safe::Status::Success)
				return;

			auto wait_result = Wait(sessions, planes);
			if (!wait_result) {
				if (m_logger)
					m_logger << Logger::Level::Error << wait_result.error()->what() << std::endl;
				return;
			}

			Safe::Status callback_status = Safe::Status::Success;
			switch (wait_result->kind) {
				case EventKind::Listener:
					callback_status = on_listener_ready.Call();
					break;
				case EventKind::Session:
					callback_status = on_session_ready.Call(wait_result->session, wait_result->readable, wait_result->writable);
					break;
				case EventKind::DataPlane:
					callback_status = on_plane_ready.Call(wait_result->plane, wait_result->readable, wait_result->writable);
					break;
				case EventKind::Wakeup:
					if (Connection::IsConnected(m_status.load(Safe::MemoryOrder::Acquire)))
						callback_status = on_wakeup.Call();
					break;
				case EventKind::Timeout:
					break;
			}
			if (callback_status != Safe::Status::Success)
				return;
		}
	}
	catch (...) {}
}
