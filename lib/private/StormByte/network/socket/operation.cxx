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

#include <StormByte/network/socket/operation.hxx>

#include <algorithm>

#ifdef UNIX
#include <cerrno>
#include <poll.h>
#else
#include <winsock2.h>
#endif

using namespace StormByte::Network::Socket;

OperationState OperationControl::Check() const noexcept {
	if (cancellation && cancellation->load(StormByte::Safe::MemoryOrder::Acquire))
		return OperationState::Cancelled;
	if (std::chrono::steady_clock::now() >= deadline)
		return OperationState::TimedOut;
	return OperationState::Complete;
}

namespace {
	/**
	 * @brief Wait for one socket direction with bounded cancellation polling.
	 * @param handle Borrowed native handle.
	 * @param control Shared total operation controls.
	 * @param reading True for incoming bytes, false for outgoing capacity.
	 * @return Readiness or the terminal stop reason.
	 */
	OperationResult WaitReady(StormByte::Network::Connection::HandlerType handle,
		const OperationControl& control, bool reading) noexcept {
		for (;;) {
			const auto state = control.Check();
			if (state != OperationState::Complete)
				return {state};
			auto wait = std::chrono::milliseconds(50);
			if (control.deadline != std::chrono::steady_clock::time_point::max())
				wait = std::min(wait, std::chrono::ceil<std::chrono::milliseconds>(
					control.deadline - std::chrono::steady_clock::now()));
			const int milliseconds = static_cast<int>(std::max(wait.count(), decltype(wait.count()){0}));
#ifdef UNIX
			pollfd descriptor{handle, static_cast<short>(reading ? POLLIN : POLLOUT), 0};
			const int result = poll(&descriptor, 1, milliseconds);
			if (result < 0) {
				const int error = errno;
				if (error == EINTR)
					continue;
				return {OperationState::Error, StormByte::ByteSize{0}, error};
			}
			if (descriptor.revents & POLLNVAL)
				return {OperationState::Error, StormByte::ByteSize{0}, EBADF};
#else
			fd_set ready, errors;
			FD_ZERO(&ready);
			FD_ZERO(&errors);
			FD_SET(handle, &ready);
			FD_SET(handle, &errors);
			timeval timeout{0, milliseconds * 1000};
			const int result = select(0, reading ? &ready : nullptr,
				reading ? nullptr : &ready, &errors, &timeout);
			if (result == SOCKET_ERROR) {
				const int error = WSAGetLastError();
				if (error == WSAEINTR)
					continue;
				return {OperationState::Error, StormByte::ByteSize{0}, error};
			}
#endif
			if (result > 0)
				return {control.Check()};
		}
	}
}

OperationResult StormByte::Network::Socket::WaitWritable(
	Connection::HandlerType handle, const OperationControl& control) noexcept {
	return WaitReady(handle, control, false);
}

OperationResult StormByte::Network::Socket::WaitReadable(
	Connection::HandlerType handle, const OperationControl& control) noexcept {
	return WaitReady(handle, control, true);
}
