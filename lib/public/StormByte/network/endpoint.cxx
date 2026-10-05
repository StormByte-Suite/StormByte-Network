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
#include <StormByte/network/endpoint.hxx>
using namespace StormByte::Network;
DeserializePacketFunction::DeserializePacketFunction(const DeserializePacketFunction& other):
	m_context(other.m_context ? other.m_clone(other.m_context) : nullptr),
	m_invoke(other.m_invoke),
	m_clone(other.m_clone),
	m_destroy(other.m_destroy) {}

DeserializePacketFunction::DeserializePacketFunction(DeserializePacketFunction&& other) noexcept:
	m_context(std::exchange(other.m_context, nullptr)),
	m_invoke(std::exchange(other.m_invoke, nullptr)),
	m_clone(std::exchange(other.m_clone, nullptr)),
	m_destroy(std::exchange(other.m_destroy, nullptr)) {}

DeserializePacketFunction::~DeserializePacketFunction() noexcept {
	if (m_context)
		m_destroy(m_context);
}

DeserializePacketFunction& DeserializePacketFunction::operator=(const DeserializePacketFunction& other) {
	if (this == &other)
		return *this;
	DeserializePacketFunction copy(other);
	return *this = std::move(copy);
}

DeserializePacketFunction& DeserializePacketFunction::operator=(DeserializePacketFunction&& other) noexcept {
	if (this != &other) {
		if (m_context)
			m_destroy(m_context);
		m_context = std::exchange(other.m_context, nullptr);
		m_invoke = std::exchange(other.m_invoke, nullptr);
		m_clone = std::exchange(other.m_clone, nullptr);
		m_destroy = std::exchange(other.m_destroy, nullptr);
	}
	return *this;
}

PacketPointer DeserializePacketFunction::operator()(OpcodeType opcode, Buffer::Consumer payload,
	StormByte::Safe::Shared<Logger::Log> logger) const {
	return m_context ? m_invoke(m_context, opcode, std::move(payload), std::move(logger)) : PacketPointer{};
}

Endpoint::Endpoint(DeserializePacketFunction deserialize_packet_function,
	StormByte::Safe::Shared<Logger::Log> logger) noexcept:
	m_deserialize_packet_function(std::move(deserialize_packet_function)),
	m_logger(std::move(logger)) {}

Endpoint::~Endpoint() noexcept = default;

Endpoint::Endpoint(Endpoint&& other) noexcept = default;

Endpoint& Endpoint::operator=(Endpoint&& other) noexcept = default;

PacketPointer Endpoint::Send(std::shared_ptr<Connection::Client> client_connection, const Transport::Packet& packet) noexcept {
	if (!SendPacket(client_connection, packet)) {
		return nullptr;
	}

	Transport::Frame response_frame = client_connection->Receive(m_logger);
	if (!response_frame.DecodeInput(client_connection->InputPipeline(), m_logger))
		return {};
	return response_frame.ProcessPacket(m_deserialize_packet_function, m_logger);
}

bool Endpoint::Reply(std::shared_ptr<Connection::Client> client_connection, const Transport::Packet& packet) noexcept {
	return SendPacket(client_connection, packet);
}

std::shared_ptr<Connection::Client> Endpoint::CreateConnection(std::shared_ptr<Socket::Client> socket) noexcept {
	return std::make_shared<Connection::Client>(std::move(socket), Buffer::Pipeline{}, Buffer::Pipeline{});
}

bool Endpoint::SendPacket(std::shared_ptr<Connection::Client> client_connection, const Transport::Packet& packet) noexcept {
	if (!client_connection || !Connection::IsConnected(client_connection->Status())) {
		m_logger << Logger::Level::Error << "Cannot send packet: not connected." << std::endl;
		return false;
	}

	if (!client_connection->Send(packet, m_logger)) {
		m_logger << Logger::Level::Error << "Failed to send packet." << std::endl;
		return false;
	}

	return true;
}
