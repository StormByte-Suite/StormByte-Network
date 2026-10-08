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

#include <StormByte/network/exception.hxx>

using namespace StormByte::Network;

Exception::Exception(std::string_view message):
	StormByte::Exception(StormByte::Exception::Path{"Network"}, "{}", message) {}

Exception::Exception(StormByte::Exception::Path child, std::string_view message):
	StormByte::Exception(StormByte::Exception::Path{JoinPath(child.text)}, "{}", message) {}

Exception::Exception(const Exception& other) = default;

Exception::Exception(Exception&& other) noexcept = default;

Exception::~Exception() noexcept = default;

Exception& Exception::operator=(const Exception& other) = default;

Exception& Exception::operator=(Exception&& other) noexcept = default;

StormByte::Safe::String Exception::JoinPath(std::string_view child) {
	StormByte::Safe::String result{"Network."};
	result.append(child);
	return result;
}

ConnectionError::ConnectionError(const ConnectionError& other) = default;

ConnectionError::ConnectionError(ConnectionError&& other) noexcept = default;

ConnectionError::~ConnectionError() noexcept = default;

ConnectionError& ConnectionError::operator=(const ConnectionError& other) = default;

ConnectionError& ConnectionError::operator=(ConnectionError&& other) noexcept = default;

ConnectionClosed::ConnectionClosed(const ConnectionClosed& other) = default;

ConnectionClosed::ConnectionClosed(ConnectionClosed&& other) noexcept = default;

ConnectionClosed::~ConnectionClosed() noexcept = default;

ConnectionClosed& ConnectionClosed::operator=(const ConnectionClosed& other) = default;

ConnectionClosed& ConnectionClosed::operator=(ConnectionClosed&& other) noexcept = default;

PacketError::PacketError(const PacketError& other) = default;

PacketError::PacketError(PacketError&& other) noexcept = default;

PacketError::~PacketError() noexcept = default;

PacketError& PacketError::operator=(const PacketError& other) = default;

PacketError& PacketError::operator=(PacketError&& other) noexcept = default;

FrameError::FrameError(const FrameError& other) = default;

FrameError::FrameError(FrameError&& other) noexcept = default;

FrameError::~FrameError() noexcept = default;

FrameError& FrameError::operator=(const FrameError& other) = default;

FrameError& FrameError::operator=(FrameError&& other) noexcept = default;
