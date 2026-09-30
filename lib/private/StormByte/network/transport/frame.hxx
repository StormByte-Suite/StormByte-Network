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

#include <StormByte/buffer/pipeline.hxx>
#include <StormByte/network/transport/packet.hxx>
#include <StormByte/network/typedefs.hxx>

namespace StormByte::Network::Socket {
	class Client;	///< Forward declaration
}

namespace StormByte::Network::Detail {
	class Session;	///< Forward declaration
}

/**
 * @brief Transport types of the Network module.
 */
namespace StormByte::Network::Transport {
	/**
	 * @class Frame
	 * @brief On-wire unit: opcode + payload size + payload.
	 *
	 * Layout: Opcode (OpcodeType) + payload size (size_t) + payload.
	 * Opcodes >= Packet::PROCESS_THRESHOLD run payload through pipelines.
	 */
	class STORMBYTE_NETWORK_PRIVATE Frame {
		friend class StormByte::Network::Detail::Session;
		public:
			/**
			 * @brief Build a frame from a packet.
			 * @param packet Source packet.
			 */
			Frame(const Packet& packet) noexcept;

			/**
			 * @brief Copy constructor.
			 */
			Frame(const Frame& other) noexcept = default;

			/**
			 * @brief Move constructor.
			 */
			Frame(Frame&& other) noexcept = default;

			/**
			 * @brief Destructor.
			 */
			virtual ~Frame() noexcept = default;

			/**
			 * @brief Copy assignment.
			 */
			Frame& operator=(const Frame& other) = default;

			/**
			 * @brief Move assignment.
			 */
			Frame& operator=(Frame&& other) noexcept = default;

			/**
			 * @brief Read one frame from the socket.
			 * @param client Socket client.
			 * @param in_pipeline Input pipeline.
			 * @param logger Logger.
			 * @return Frame (default-constructed on failure).
			 */
			static Frame ProcessInput(std::shared_ptr<Socket::Client> client, Buffer::Pipeline& in_pipeline, StormByte::Shared<Logger::Log> logger) noexcept;

			/**
			 * @brief Deserialize payload into a Packet.
			 * @param packet_fn Deserializer callback.
			 * @param logger Logger.
			 * @return Packet pointer, or nullptr on failure.
			 */
			PacketPointer ProcessPacket(const DeserializePacketFunction& packet_fn, StormByte::Shared<Logger::Log> logger) noexcept;

			/**
			 * @brief Serialize this frame to a Consumer.
			 * @param out_pipeline Output pipeline.
			 * @param logger Logger.
			 * @return Consumer of framed bytes.
			 */
			Buffer::Consumer ProcessOutput(Buffer::Pipeline& out_pipeline, StormByte::Shared<Logger::Log> logger) noexcept;

		private:
			Packet::OpcodeType m_opcode;	///< Opcode
			StormByte::BinaryData m_payload;		///< Payload bytes

			/**
			 * @brief Build a frame from already parsed wire fields.
			 * @param opcode Frame opcode.
			 * @param payload Raw payload bytes.
			 * @param in_pipeline Input pipeline.
			 * @param logger Diagnostic logger.
			 * @return Parsed frame.
			 */
			static Frame FromWire(Packet::OpcodeType opcode, StormByte::BinaryData&& payload,
				Buffer::Pipeline& in_pipeline, StormByte::Shared<Logger::Log> logger) noexcept;

			/**
			 * @brief Empty frame (error path).
			 */
			Frame() noexcept = default;

			/**
			 * @brief Construct from opcode and payload.
			 * @param opcode Opcode.
			 * @param payload Payload (moved).
			 */
			Frame(Packet::OpcodeType opcode, StormByte::BinaryData&& payload) noexcept:
			m_opcode(opcode),
			m_payload(std::move(payload)) {}
	};
}
