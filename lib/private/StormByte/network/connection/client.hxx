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
#include <StormByte/safe/mutex.hxx>
#include <StormByte/safe/pair.hxx>
#include <StormByte/safe/pointers.hxx>

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
		 * @namespace StormByte::Network::Connection
		 * @brief Connection namespace.
		 */
		namespace Connection {
			/**
			 * @class Client
			 * @brief High-level connection over a Socket::Client with I/O pipelines.
			 * @note Moving, I/O and borrowed state access require external serialization.
			 */
			class STORMBYTE_NETWORK_PRIVATE Client final {
				public:
					/**
					 * @brief Shared connection owner retaining its Network lifecycle.
					 */
					using Pointer = StormByte::Safe::Shared<Client>;

					/**
					 * @brief Non-owning observer of a connection owner.
					 */
					using WeakPointer = StormByte::Safe::Weak<Client>;

					/**
					 * @brief Bind a socket and two pipelines.
					 * @param socket Underlying socket client.
					 * @param in_pipeline Input pipeline.
					 * @param out_pipeline Output pipeline.
					 */
					Client(StormByte::Safe::Shared<Network::Socket::Client> socket, Buffer::Pipeline in_pipeline, Buffer::Pipeline out_pipeline) noexcept;

					/**
					 * @brief Copy constructor (deleted).
					 * @param other Connection that cannot be copied.
					 */
					Client(const Client& other) = delete;

					/**
					 * @brief Transfer connection state with externally serialized ownership.
					 * @param other Source connection.
					 */
					Client(Client&& other) noexcept;

					/**
					 * @brief Release connection state inside Network.
					 */
					~Client() noexcept;

					/**
					 * @brief Copy assignment (deleted).
					 * @param other Connection that cannot be copied.
					 * @return Reference to this connection (operation is deleted).
					 */
					Client& operator=(const Client& other) = delete;

					/**
					 * @brief Transfer state with externally serialized ownership.
					 * @param other Source connection.
					 * @return Reference to this connection.
					 */
					Client& operator=(Client&& other) noexcept;

					/**
					 * @brief Allocate an exact connection through Network's Safe factory.
					 * @param socket Underlying socket client.
					 * @param input Initial input pipeline.
					 * @param output Initial output pipeline.
					 * @return Shared connection owner.
					 */
					static Pointer Create(StormByte::Safe::Shared<Network::Socket::Client> socket, Buffer::Pipeline input = {}, Buffer::Pipeline output = {}) noexcept;

					/**
					 * @brief Install one complete pair before any file plane is created.
					 * @param input Incoming transformation pipeline.
					 * @param output Outgoing transformation pipeline.
					 * @return False after configuration or sealing, without replacing state.
					 */
					bool ConfigurePipelines(Buffer::Pipeline input, Buffer::Pipeline output);

					/**
					 * @brief Copy independent pipeline templates and seal configuration.
					 * @return Input and output templates for one file plane.
					 */
					StormByte::Safe::Pair<Buffer::Pipeline, Buffer::Pipeline> FilePipelines();

					/**
					 * @brief Borrow the input pipeline.
					 * @return Pipeline requiring externally serialized access.
					 */
					Buffer::Pipeline& InputPipeline() noexcept;

					/**
					 * @brief Borrow the output pipeline.
					 * @return Pipeline requiring externally serialized access.
					 */
					Buffer::Pipeline& OutputPipeline() noexcept;

					/**
					 * @brief Borrow the underlying socket owner.
					 * @return Safe socket owner requiring externally serialized access.
					 */
					StormByte::Safe::Shared<Network::Socket::Client>& Socket() noexcept;

					/**
					 * @brief Send a frame through the output pipeline.
					 * @param frame Frame to send.
					 * @param logger Logger.
					 * @return True on success, false without a socket.
					 */
					bool Send(Transport::Frame&& frame, StormByte::Safe::Shared<Logger::Log> logger) noexcept;

					/**
					 * @brief Status from the socket, or Disconnected without a socket.
					 * @return Connection status.
					 */
					Connection::Status Status() const noexcept;

					/**
					 * @brief Receive one framed message.
					 * @param logger Logger.
					 * @return Frame, empty on failure.
					 * @pre A socket owner is bound to this connection.
					 */
					Transport::Frame Receive(StormByte::Safe::Shared<Logger::Log> logger) noexcept;

				private:
					StormByte::Safe::Shared<Network::Socket::Client> m_socket;	///< Safe socket owner.
					Buffer::Pipeline m_in_pipeline;							///< Input pipeline.
					Buffer::Pipeline m_out_pipeline;							///< Output pipeline.
					Buffer::Pipeline m_file_input;							///< Independent file input template.
					Buffer::Pipeline m_file_output;							///< Independent file output template.
					StormByte::Safe::Mutex m_configuration_mutex;				///< Protects configuration and sealing.
					bool m_configured{false};								///< Configuration succeeded or was sealed.
			};
		}
	}
}

STORMBYTE_DECLARE_MAYBE_SAFE(StormByte::Network::Connection::Client);
