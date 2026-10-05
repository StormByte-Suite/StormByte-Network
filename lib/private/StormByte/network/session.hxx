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

#include <StormByte/network/connection/client.hxx>
#include <StormByte/network/transport/frame.hxx>
#include <StormByte/network/visibility.h>

#include <vector>
#include <deque>

namespace StormByte::Network::Detail {
	/**
	 * @class Session
	 * @brief Incremental frame state for one server-side client connection.
	 */
	class STORMBYTE_NETWORK_PRIVATE Session final {
		public:
			using FrameList = std::vector<Transport::Frame>; ///< Frames parsed from one receive operation.

			/**
			 * @brief Create a session around an existing connection.
			 * @param uuid Client UUID.
			 * @param client High-level connection.
			 */
			Session(std::string uuid, std::shared_ptr<Connection::Client> client) noexcept;

			/** @brief Releases session buffers and queued output in the Network library. */
			~Session() noexcept;

			/** @brief Client UUID. */
			const std::string& UUID() const noexcept;

			/** @brief Underlying high-level connection. */
			std::shared_ptr<Connection::Client>& Client() noexcept;

			/** @brief Whether this session is closed. */
			bool Closed() const noexcept;

			/** @brief Native socket handle. */
			Connection::HandlerType Handle() const noexcept;

			/** @brief Whether one request is executing in the pool. */
			bool InFlight() const noexcept;

			/** @brief Mark one request as executing or completed. */
			void SetInFlight(bool value) noexcept;

			/** @brief Whether the socket can be polled for more input. */
			bool CanRead() const noexcept;

			/** @brief Temporarily block reads when the task queue is full. */
			void SetTaskBlocked(bool value) noexcept;

			/** @brief Queue parsed frames owned by the EventLoop. */
			void QueueFrames(FrameList frames) noexcept;

			/** @brief Whether a parsed frame is waiting for submission. */
			bool HasPendingFrame() const noexcept;

			/** @brief Whether a pending frame may be submitted now. */
			bool ReadyForProcessing() const noexcept;

			/** @brief Remove the next parsed frame. */
			Transport::Frame TakeFrame() noexcept;

			/**
			 * @brief Read and parse complete frames from a ready socket.
			 * @param in_pipeline Input payload pipeline.
			 * @param logger Diagnostic logger.
			 * @return Complete frames, or connection error.
			 */
			StormByte::Expected<FrameList, ConnectionError> ReadReady(
				Buffer::Pipeline& in_pipeline, StormByte::Safe::Shared<Logger::Log> logger) noexcept;

			/** @brief Close the session. */
			void Close() noexcept;

			/**
			 * @brief Discard unprocessed input and prevent further packet dispatch.
			 */
			void CloseAfterReply() noexcept;

			/**
			 * @brief Whether the session is waiting only for reply draining.
			 * @return True after CloseAfterReply.
			 */
			bool ClosingAfterReply() const noexcept;

			/** @brief Whether serialized output has bytes ready to write. */
			bool HasOutput() noexcept;

			/**
			 * @brief Serialize and queue one response.
			 * @param packet Response packet.
			 * @param logger Diagnostic logger.
			 * @return false when the per-session cap is exceeded.
			 */
			bool QueueResponse(const PacketPointer& packet, StormByte::Safe::Shared<Logger::Log> logger) noexcept;

			/**
			 * @brief Attempt one non-blocking output write.
			 * @return true when output is fully drained, false on would-block.
			 */
			StormByte::Expected<bool, ConnectionError> FlushOutput() noexcept;

		private:
			enum class ParsePhase: unsigned short { Header, Payload }; ///< Parser phase.
			static constexpr StormByte::ByteSize FRAME_HEADER_SIZE =
				StormByte::ByteSize{sizeof(Transport::Packet::OpcodeType) + sizeof(std::size_t)}; ///< Wire header size.

			std::string m_uuid; ///< Client UUID.
			std::shared_ptr<Connection::Client> m_client; ///< Client connection.
			StormByte::BinaryData m_input; ///< Unparsed bytes.
			StormByte::BinaryData m_payload; ///< Partial payload.
			Transport::Packet::OpcodeType m_opcode = 0; ///< Current opcode.
			StormByte::ByteSize m_bytes_needed = FRAME_HEADER_SIZE; ///< Remaining bytes.
			ParsePhase m_phase = ParsePhase::Header; ///< Current parser phase.
			bool m_closed = false; ///< Terminal state.

			/**
			 * @brief Reply-draining state owned by the event loop.
			 */
			bool m_close_after_reply{false};
			bool m_in_flight = false; ///< Request executing in pool.
			bool m_task_blocked = false; ///< Pool queue was full.
			FrameList m_ready_frames; ///< Parsed frames waiting for submission.
			struct OutputStream {
				Buffer::Consumer source; ///< Pipeline output source.
				StormByte::BinaryData data; ///< Currently buffered chunk.
				StormByte::ByteSize offset{0}; ///< Bytes already written from chunk.

				explicit OutputStream(Buffer::Consumer&& consumer) noexcept;
			};
			std::deque<OutputStream> m_output_frames; ///< Serialized output streams.
			StormByte::ByteSize m_output_bytes{0}; ///< Queued output bytes.
			std::size_t m_output_frame_count = 0; ///< Logical response count.
			static constexpr StormByte::ByteSize MAX_OUTPUT_BYTES{1024 * 1024}; ///< Per-session byte cap.
			static constexpr std::size_t MAX_OUTPUT_FRAMES = 8; ///< Per-session frame cap.

			/** @brief Fill the front stream from its pipeline without blocking. */
			bool PrepareOutput() noexcept;

			/**
			 * @brief Append bytes and extract complete frames.
			 * @param received Newly received bytes.
			 * @param in_pipeline Input payload pipeline.
			 * @param logger Diagnostic logger.
			 * @return Complete frames or connection error.
			 */
			StormByte::Expected<FrameList, ConnectionError> AppendReceived(
				StormByte::BinaryData&& received, Buffer::Pipeline& in_pipeline,
				StormByte::Safe::Shared<Logger::Log> logger) noexcept;
	};
}
