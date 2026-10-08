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
			 * @class Session
			 * @brief Forward declaration of the session implementation.
			 */
			class Session;
		}
		/**
		 * @namespace StormByte::Network::Transport
		 * @brief Transport namespace.
		 */
		namespace Transport {
			/**
			 * @class Frame
			 * @brief On-wire unit: opcode + payload size + payload.
			 * @details Layout: Opcode (OpcodeType) + payload size (size_t) + payload.
			 * Opcodes >= Packet::PROCESS_THRESHOLD run payload through pipelines.
			 */
			class STORMBYTE_NETWORK_PRIVATE Frame {
				friend class StormByte::Network::Detail::Session;
				public:
					/**
					 * @brief Construct an empty invalid frame.
					 */
					Frame() noexcept;

					/**
					 * @brief Build a frame from a packet.
					 * @param packet Source packet.
					 */
					Frame(const Packet& packet) noexcept;

					/**
					 * @brief Copy constructor.
					 * @param other Source frame.
					 */
					Frame(const Frame& other);

					/**
					 * @brief Move constructor.
					 * @param other Source frame.
					 */
					Frame(Frame&& other) noexcept;

					/**
					 * @brief Destructor.
					 */
					virtual ~Frame() noexcept;

					/**
					 * @brief Copy assignment.
					 * @param other Source frame.
					 * @return This frame.
					 */
					Frame& operator=(const Frame& other);

					/**
					 * @brief Move assignment.
					 * @param other Source frame.
					 * @return This frame.
					 */
					Frame& operator=(Frame&& other) noexcept;

					/**
					 * @brief Read one frame from the socket.
					 * @param client Safe shared socket client.
					 * @param in_pipeline Input pipeline.
					 * @param logger Logger.
					 * @return Frame (default-constructed on failure).
					 */
					static Frame ProcessInput(StormByte::Safe::Shared<Socket::Client> client,
						Buffer::Pipeline& in_pipeline, StormByte::Safe::Shared<Logger::Log> logger) noexcept;

					/**
					 * @brief Deserialize payload into a Packet.
					 * @param packet_fn Deserializer callback.
					 * @param logger Logger.
					 * @return Packet pointer, or an empty pointer on failure.
					 */
					PacketPointer ProcessPacket(const DeserializePacketFunction& packet_fn,
						StormByte::Safe::Shared<Logger::Log> logger) noexcept;

					/**
					 * @brief Transform one raw frame after opcode policy and previous handlers.
					 * @param pipeline Connection input pipeline in its current negotiated state.
					 * @param logger Diagnostic logger.
					 * @return False if a pipeline fails; the factory must not be invoked.
					 */
					bool DecodeInput(Buffer::Pipeline& pipeline, StormByte::Safe::Shared<Logger::Log> logger) noexcept;

					/**
					 * @brief Read the wire opcode before input transformation.
					 * @return Application opcode.
					 */
					Packet::OpcodeType Opcode() const noexcept {
						return m_opcode;
					}

					/**
					 * @brief Serialize this frame to a Consumer.
					 * @param out_pipeline Output pipeline.
					 * @param logger Logger.
					 * @return Consumer of framed bytes.
					 */
					Buffer::Consumer ProcessOutput(Buffer::Pipeline& out_pipeline,
						StormByte::Safe::Shared<Logger::Log> logger) noexcept;

				private:
					Packet::OpcodeType m_opcode{0};		///< Opcode; zero for an invalid frame.
					StormByte::Safe::Binary m_payload;	///< Base-owned payload bytes.

					/**
					 * @brief Retain raw wire fields without executing pipelines or factories.
					 * @param opcode Frame opcode.
					 * @param payload Raw Base-owned payload bytes.
					 * @return Raw frame; DecodeInput is deferred until opcode admission.
					 */
					static Frame FromWire(Packet::OpcodeType opcode, StormByte::Safe::Binary&& payload) noexcept;

					/**
					 * @brief Construct from opcode and payload.
					 * @param opcode Opcode.
					 * @param payload Base-owned payload (moved).
					 */
					Frame(Packet::OpcodeType opcode, StormByte::Safe::Binary&& payload) noexcept;
			};
		}
	}
}

STORMBYTE_DECLARE_MAYBE_SAFE(StormByte::Network::Transport::Frame);
