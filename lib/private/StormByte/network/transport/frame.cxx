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

#include <StormByte/network/socket/client.hxx>
#include <StormByte/network/transport/frame.hxx>
#include <StormByte/serializable.hxx>

using namespace StormByte::Buffer;
using namespace StormByte::Network;
using namespace StormByte::Network::Transport;

Frame::Frame(const Packet& packet) noexcept {
	FIFO packet_raw = packet.Serialize();
	m_opcode = packet.Opcode();
	packet_raw.Drop(sizeof(Packet::OpcodeType));
	if (packet_raw.Available() > StormByte::ByteSize{0})
		packet_raw.Read(StormByte::ByteSize{0}, m_payload);
}

Frame Frame::FromWire(Packet::OpcodeType opcode, StormByte::BinaryData&& payload,
	Pipeline& in_pipeline, StormByte::Shared<Logger::Log> logger) noexcept {
	if (opcode >= Packet::PROCESS_THRESHOLD) {
		Producer payload_producer;
		payload_producer.Write(std::move(payload));
		payload_producer.Close();
		Consumer processed_payload = in_pipeline.Process(
			payload_producer.Consumer(), logger, ExecutionMode::Async);
		payload.clear();
		processed_payload.ExtractUntilEoF(payload);
	}

	return Frame(opcode, std::move(payload));
}

Frame Frame::ProcessInput(std::shared_ptr<Socket::Client> client,
	Pipeline& in_pipeline, StormByte::Shared<Logger::Log> logger) noexcept {
	ExpectedBuffer expected_opcode_buffer = client->Receive(sizeof(Packet::OpcodeType));
	if (!expected_opcode_buffer) {
		logger << Logger::Level::Error << "Failed to read opcode from socket: "
			<< expected_opcode_buffer.error()->what();
		return Frame();
	}

	auto expected_opcode = Serializable<Packet::OpcodeType>::Deserialize(expected_opcode_buffer->Data());
	if (!expected_opcode) {
		logger << Logger::Level::Error << "Failed to deserialize opcode from socket: insufficient data" << std::endl;
		return Frame();
	}

	const Packet::OpcodeType opcode = *expected_opcode;
	ExpectedBuffer expected_size_buffer = client->Receive(sizeof(std::size_t));
	if (!expected_size_buffer) {
		logger << Logger::Level::Error << "Failed to read payload size from socket: "
			<< expected_size_buffer.error()->what() << std::endl;
		return Frame();
	}

	auto expected_payload_size = Serializable<std::size_t>::Deserialize(expected_size_buffer->Data());
	if (!expected_payload_size) {
		logger << Logger::Level::Error << "Failed to deserialize payload size from socket: insufficient data" << std::endl;
		return Frame();
	}

	const std::size_t payload_size = expected_payload_size.value();
	StormByte::BinaryData payload;
	if (payload_size > 0) {
		payload.reserve(StormByte::ByteSize{payload_size});
		auto into = client->ReceiveInto(payload_size, payload);
		if (!into) {
			logger << Logger::Level::Error << "Failed to read full frame from socket: "
				<< into.error()->what() << std::endl;
			return Frame();
		}
	}

	return FromWire(opcode, std::move(payload), in_pipeline, std::move(logger));
}

PacketPointer Frame::ProcessPacket(const DeserializePacketFunction& packet_fn,
	StormByte::Shared<Logger::Log> logger) noexcept {
	Producer payload_producer;
	payload_producer.Write(std::move(m_payload));
	payload_producer.Close();
	return packet_fn(m_opcode, payload_producer.Consumer(), std::move(logger));
}

Consumer Frame::ProcessOutput(Pipeline& pipeline, StormByte::Shared<Logger::Log> logger) noexcept {
	Producer producer;
	producer.Write(sizeof(Packet::OpcodeType), Serializable<Packet::OpcodeType>(m_opcode).Serialize());
	StormByte::BinaryData payload = std::move(m_payload);
	if (m_opcode >= Packet::PROCESS_THRESHOLD) {
		Producer payload_producer;
		payload_producer.Write(std::move(payload));
		payload_producer.Close();
		Consumer processed_payload = pipeline.Process(
			payload_producer.Consumer(), logger, ExecutionMode::Async);
		payload.clear();
		processed_payload.ExtractUntilEoF(payload);
	}

	producer.Write(sizeof(std::size_t), Serializable<std::size_t>(payload.size()).Serialize());
	if (!payload.empty())
		producer.Write(std::move(payload));

	producer.Close();
	return producer.Consumer();
}
