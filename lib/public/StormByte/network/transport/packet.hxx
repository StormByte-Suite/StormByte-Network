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
#include <StormByte/network/visibility.h>
#include <StormByte/serializable.hxx>

/**
 * @brief Transport types of the Network module.
 */
namespace StormByte::Network::Transport {
	/**
	 * @class Packet
	 * @brief Polymorphic wire packet: opcode + payload hook.
	 *
	 * Derive and override DoSerialize() for the payload (excluding opcode). Serialize() writes opcode then payload.
	 *
	 * Opcodes must fit OpcodeType (unsigned short).
	 */
	class STORMBYTE_NETWORK_PUBLIC Packet {
		public:
			using OpcodeType = unsigned short;	///< Opcode storage type

			/**
			 * @brief Copy constructor.
			 */
			Packet(const Packet& other) = default;

			/**
			 * @brief Move constructor.
			 */
			Packet(Packet&& other) noexcept = default;

			/**
			 * @brief Destructor.
			 */
			virtual ~Packet() noexcept = default;

			/**
			 * @brief Copy assignment.
			 */
			Packet& operator=(const Packet& other) = default;

			/**
			 * @brief Move assignment.
			 */
			Packet& operator=(Packet&& other) noexcept = default;

			/**
			 * @brief Stored opcode.
			 * @return Opcode.
			 */
			inline const OpcodeType& Opcode() const noexcept {
				return m_opcode;
			}

			/**
			 * @brief Serialize opcode followed by DoSerialize() payload.
			 * @return Complete on-wire buffer.
			 */
			Buffer::FIFO Serialize() const noexcept;

			/**
			 * @brief Opcodes at or above this value run payload through Buffer pipelines when framing.
			 */
			static constexpr unsigned short PROCESS_THRESHOLD = 10;

		protected:
			OpcodeType m_opcode;	///< Packet opcode

			/**
			 * @brief Construct with an opcode.
			 * @param opcode Packet opcode.
			 */
			constexpr Packet(const OpcodeType& opcode) noexcept:
			m_opcode(opcode) {}

			/**
			 * @brief Payload-only serialization (no opcode).
			 * @return Payload bytes (may be empty).
			 */
			virtual StormByte::BinaryData DoSerialize() const noexcept = 0;
	};
}
