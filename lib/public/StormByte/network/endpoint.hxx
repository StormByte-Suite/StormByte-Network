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
#include <StormByte/logger/threaded_log.hxx>
#include <StormByte/network/typedefs.hxx>

#include <string_view>

/**
 * @brief Network module of the StormByte suite.
 */
namespace StormByte::Network {
	namespace Connection {
		class Client;	///< Forward declaration
	}

	/**
	 * @class Endpoint
	 * @brief Shared base for Client and Server.
	 *
	 * Not instantiated directly. Override InputPipeline() / OutputPipeline(). Use Send() / Reply() for framed request/response.
	 *
	 * @note Inheritance-oriented. Derive from Client / Server, not from Endpoint alone.
	 */
	class STORMBYTE_NETWORK_PUBLIC Endpoint {
		public:
			/**
			 * @brief Construct with a packet factory and a logger.
			 * @param deserialize_packet_function Builds domain packets from wire data.
			 * @param logger Diagnostic logger.
			 */
			Endpoint(const DeserializePacketFunction& deserialize_packet_function, StormByte::Shared<Logger::Log> logger) noexcept;

			/**
			 * @brief Copy constructor (deleted).
			 */
			Endpoint(const Endpoint& other) = delete;

			/**
			 * @brief Move constructor.
			 */
			Endpoint(Endpoint&& other) noexcept;

			/**
			 * @brief Destructor.
			 */
			virtual ~Endpoint() noexcept;

			/**
			 * @brief Copy assignment (deleted).
			 */
			Endpoint& operator=(const Endpoint& other) = delete;

			/**
			 * @brief Move assignment.
			 */
			Endpoint& operator=(Endpoint&& other) noexcept;

			/**
			 * @brief Connect or listen (meaning depends on the derived class).
			 * @param protocol Address family.
			 * @param address Host or bind address.
			 * @param port Port number.
			 * @return true on success.
			 */
			virtual bool Connect(const Connection::Protocol& protocol, std::string_view address, const unsigned short& port) = 0;

			/**
			 * @brief Tear down the endpoint.
			 */
			virtual void Disconnect() noexcept = 0;

			/**
			 * @brief Current connection/listen status.
			 * @return Status.
			 */
			virtual Connection::Status Status() const noexcept = 0;

		protected:
			DeserializePacketFunction m_deserialize_packet_function;	///< Packet factory
			StormByte::Shared<Logger::Log> m_logger;						///< Logger

			/**
			 * @brief Wrap a socket client with input/output pipelines.
			 * @param socket Underlying socket client.
			 * @return Connection::Client.
			 */
			std::shared_ptr<Connection::Client> CreateConnection(std::shared_ptr<Socket::Client> socket) noexcept;

			/**
			 * @brief Pipeline applied to inbound frame payloads.
			 * @return Pipeline.
			 */
			virtual Buffer::Pipeline InputPipeline() const noexcept = 0;

			/**
			 * @brief Pipeline applied to outbound frame payloads.
			 * @return Pipeline.
			 */
			virtual Buffer::Pipeline OutputPipeline() const noexcept = 0;

			/**
			 * @brief Send @p packet and wait for a response frame.
			 * @param client_connection Active connection.
			 * @param packet Packet to send.
			 * @return Response packet, or nullptr on failure.
			 */
			PacketPointer Send(std::shared_ptr<Connection::Client> client_connection, const Transport::Packet& packet) noexcept;

			/**
			 * @brief Send @p packet without waiting for a reply.
			 * @param client_connection Active connection.
			 * @param packet Packet to send.
			 * @return true on success.
			 */
			bool Reply(std::shared_ptr<Connection::Client> client_connection, const Transport::Packet& packet) noexcept;

		private:
			/**
			 * @brief Internal send (no receive).
			 * @param client_connection Active connection.
			 * @param packet Packet to send.
			 * @return true on success.
			 */
			bool SendPacket(std::shared_ptr<Connection::Client> client_connection, const Transport::Packet& packet) noexcept;
	};
}
