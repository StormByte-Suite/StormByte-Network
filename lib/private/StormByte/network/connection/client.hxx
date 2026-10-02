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
#include <StormByte/network/socket/client.hxx>
#include <StormByte/network/transport/frame.hxx>

/**
 * @brief Connection helpers of the Network module.
 */
namespace StormByte::Network::Connection {
	/**
	 * @class Client
	 * @brief High-level connection over a Socket::Client with I/O pipelines.
	 */
	class STORMBYTE_NETWORK_PRIVATE Client final {
		public:
			/**
			 * @brief Bind a socket and two pipelines.
			 * @param socket Underlying socket client.
			 * @param in_pipeline Input pipeline.
			 * @param out_pipeline Output pipeline.
			 */
			Client(std::shared_ptr<Socket::Client> socket, Buffer::Pipeline in_pipeline, Buffer::Pipeline out_pipeline) noexcept;

			/**
			 * @brief Copy constructor (deleted).
			 */
			Client(const Client& other) = delete;

			/**
			 * @brief Move constructor.
			 */
			Client(Client&& other) noexcept = default;

			/**
			 * @brief Destructor.
			 */
			~Client() noexcept;

			/**
			 * @brief Copy assignment (deleted).
			 */
			Client& operator=(const Client& other) = delete;

			/**
			 * @brief Move assignment.
			 */
			Client& operator=(Client&& other) noexcept = default;

			/**
			 * @brief Input pipeline.
			 * @return Pipeline.
			 */
			inline Buffer::Pipeline& InputPipeline() noexcept {
				return m_in_pipeline;
			}

			/**
			 * @brief Output pipeline.
			 * @return Pipeline.
			 */
			inline Buffer::Pipeline& OutputPipeline() noexcept {
				return m_out_pipeline;
			}

			/**
			 * @brief Underlying socket client.
			 * @return Socket.
			 */
			inline std::shared_ptr<Socket::Client>& Socket() noexcept {
				return m_socket;
			}

			/**
			 * @brief Send a frame (payload through the output pipeline).
			 * @param frame Frame to send (use std::move).
			 * @param logger Logger.
			 * @return true on success.
			 */
			bool Send(Transport::Frame&& frame, StormByte::Safe::Shared<Logger::Log> logger) noexcept;

			/**
			 * @brief Status from the socket (or Disconnected).
			 * @return Status.
			 */
			inline Connection::Status Status() const noexcept {
				return m_socket ? m_socket->Status() : Connection::Status::Disconnected;
			}

			/**
			 * @brief Receive one framed message.
			 * @param logger Logger.
			 * @return Frame (empty on failure).
			 */
			Transport::Frame Receive(StormByte::Safe::Shared<Logger::Log> logger) noexcept;

		private:
			std::shared_ptr<Socket::Client> m_socket;	///< Socket
			Buffer::Pipeline m_in_pipeline;				///< Input pipeline
			Buffer::Pipeline m_out_pipeline;			///< Output pipeline
	};
}
