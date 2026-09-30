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

#include <StormByte/network/connection/client.hxx>
#include <StormByte/network/client.hxx>
#include <StormByte/network/transport/frame.hxx>
#include <StormByte/network/transport/packet.hxx>
using namespace StormByte::Network;
Client::Client(const DeserializePacketFunction& deserialize_packet_function,
	StormByte::Shared<Logger::Log> logger) noexcept:
	Endpoint(deserialize_packet_function, std::move(logger)),
	m_connection(nullptr) {}

Client::~Client() noexcept {
	Disconnect();
}

Client::Client(Client&& other) noexcept = default;

Client& Client::operator=(Client&& other) noexcept = default;

bool Client::Connect(const Connection::Protocol& protocol, std::string_view address, const unsigned short& port) {
	if (m_connection) {
		m_logger << Logger::Level::Error << "Client is already connected." << std::endl;
		return false;
	}

	try {
		std::shared_ptr<Socket::Client> socket = std::make_shared<Socket::Client>(protocol, m_logger);
		if (!socket->Connect(address, port)) {
			m_logger << Logger::Level::Error << "Failed to connect to " << std::string_view{address} << ":" << port
					<< " using protocol " << Connection::ProtocolString(protocol) << std::endl;
			return false;
		}

		m_connection = CreateConnection(socket);
		m_logger << Logger::Level::LowLevel << "Successfully connected to " << address << ":" << port
				<< " using protocol " << Connection::ProtocolString(protocol) << std::endl;
		return true;
	} catch (const std::bad_alloc& bd) {
		m_logger << Logger::Level::Error << "Failed to allocate memory for socket: " << bd.what() << std::endl;
		return false;
	}
}

void Client::Disconnect() noexcept {
	if (m_connection) {
		m_logger << Logger::Level::LowLevel << "Disconnecting client." << std::endl;
		m_connection.reset();
	}
}

Connection::Status Client::Status() const noexcept {
	return m_connection ? m_connection->Status() : Connection::Status::Disconnected;
}

PacketPointer Client::Send(const Transport::Packet& packet) noexcept {
	return Endpoint::Send(m_connection, packet);
}
