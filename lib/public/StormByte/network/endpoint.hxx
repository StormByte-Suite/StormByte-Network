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
			 * @brief Forward declaration of the connected client implementation.
			 */
			class Client;
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
				Endpoint(DeserializePacketFunction deserialize_packet_function, StormByte::Safe::Shared<Logger::Log> logger) noexcept;

				/**
				 * @brief Copy constructor (deleted).
				 * @param other Endpoint that cannot be copied.
				 */
				Endpoint(const Endpoint& other) = delete;

				/**
				 * @brief Move constructor.
				 * @param other Endpoint whose state is transferred.
				 */
				Endpoint(Endpoint&& other) noexcept;

				/**
				 * @brief Destructor.
				 */
				virtual ~Endpoint() noexcept;

				/**
				 * @brief Copy assignment (deleted).
				 * @param other Endpoint that cannot be copied.
				 * @return Reference to this endpoint (operation is deleted).
				 */
				Endpoint& operator=(const Endpoint& other) = delete;

				/**
				 * @brief Move assignment.
				 * @param other Endpoint whose state is transferred.
				 * @return Reference to this endpoint.
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
				/**
				 * @brief Packet factory used to deserialize received wire data.
				 */
				DeserializePacketFunction m_deserialize_packet_function;

				/**
				 * @brief Shared diagnostic logger for this endpoint.
				 */
				StormByte::Safe::Shared<Logger::Log> m_logger;

				/**
				 * @brief Wrap a socket client with input/output pipelines.
				 * @param socket Underlying socket client.
				 * @return Connection::Client.
				 */
				std::shared_ptr<Connection::Client> CreateConnection(std::shared_ptr<Socket::Client> socket) noexcept;

				/**
				 * @brief Build the endpoint's inbound byte transformation pipeline.
				 * @return Pipeline.
				 *
				 * The returned pipeline is owned by its connection. Remote-file
				 * channels build another instance from this hook and may outlive the
				 * application connection. Pipes must own or share every object they
				 * use; they must not retain raw references to the Endpoint or derived
				 * object. Clone/Move must preserve that ownership contract.
				 */
				virtual Buffer::Pipeline InputPipeline() const noexcept = 0;

				/**
				 * @brief Build the endpoint's outbound byte transformation pipeline.
				 * @return Pipeline.
				 *
				 * The returned pipeline is owned by its connection. Remote-file
				 * channels build another instance from this hook and may outlive the
				 * application connection. Pipes must own or share every object they
				 * use; they must not retain raw references to the Endpoint or derived
				 * object. Clone/Move must preserve that ownership contract.
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
}

/**
 * @brief Endpoint resources have Network-owned lifecycles.
 *
 * Derived providers must keep their own resources and virtual hooks valid
 * until destruction, with a compatible ABI.
 */
STORMBYTE_DECLARE_MAYBE_SAFE(StormByte::Network::Endpoint);
