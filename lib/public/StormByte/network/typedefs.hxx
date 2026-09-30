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

#include <StormByte/buffer/fifo.hxx>
#include <StormByte/expected.hxx>
#include <StormByte/logger/log.hxx>
#include <StormByte/network/connection/protocol.hxx>
#include <StormByte/network/connection/rw.hxx>
#include <StormByte/network/connection/status.hxx>
#include <StormByte/network/exception.hxx>
#include <StormByte/network/transport/packet.hxx>

#ifdef WINDOWS
#include <winsock2.h>
#endif

#include <functional>
#include <memory>

/**
 * @brief Network module of the StormByte suite.
 */
namespace StormByte::Network {
	namespace Socket {
		class Socket;	///< Forward declaration
		class Client;	///< Forward declaration
	}

	namespace Connection {
		#ifdef UNIX
			using HandlerType = int;		///< Native socket handle (POSIX)
		#else
			using HandlerType = SOCKET;		///< Native socket handle (Winsock)
		#endif
	}

	using ExpectedBuffer = StormByte::Expected<Buffer::FIFO, ConnectionError>;				///< Receive buffer result
	using ExpectedVoid = StormByte::Expected<void, ConnectionError>;						///< Void operation result
	using ExpectedClient = StormByte::Expected<std::shared_ptr<Socket::Client>, ConnectionError>;	///< Accept result
	using ExpectedReadResult = StormByte::Expected<Connection::Read::Result, ConnectionClosed>;	///< Wait-for-data result
	using PacketPointer = std::shared_ptr<Transport::Packet>;								///< Shared packet

	/**
	 * @brief Callback that builds a Packet from opcode + payload consumer.
	 */
	using DeserializePacketFunction = std::function<PacketPointer(
		Transport::Packet::OpcodeType,
		Buffer::Consumer,
		StormByte::Shared<Logger::Log>
	)>;
}
