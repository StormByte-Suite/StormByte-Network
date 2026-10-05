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

#include <StormByte/network/endpoint.hxx>
#include <StormByte/network/client_telemetry.hxx>
#include <StormByte/network/remote_file.hxx>
#include <StormByte/safe/pointers.hxx>

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
		 * @class Client
		 * @brief Abstract application client.
		 *
		 * Derive and implement InputPipeline() / OutputPipeline(). Use protected Send() for request/response.
		 *
		 * @note Inheritance-oriented. Not for direct generic use without a subclass.
		 */
		class STORMBYTE_NETWORK_PUBLIC Client: private Endpoint {
			public:
				/**
				 * @brief Construct with a packet factory and a logger.
				 * @param deserialize_packet_function Builds domain packets from wire data.
				 * @param logger Diagnostic logger.
				 */
				Client(DeserializePacketFunction deserialize_packet_function, StormByte::Safe::Shared<Logger::Log> logger);

				/**
				 * @brief Copy constructor (deleted).
				 * @param other Client that cannot be copied.
				 */
				Client(const Client& other) = delete;

				/**
				 * @brief Move constructor.
				 * @param other Client whose state is transferred.
				 */
				Client(Client&& other) noexcept;

				/**
				 * @brief Destructor (out-of-line in .cxx).
				 */
				virtual ~Client() noexcept;

				/**
				 * @brief Copy assignment (deleted).
				 * @param other Client that cannot be copied.
				 * @return Reference to this client (operation is deleted).
				 */
				Client& operator=(const Client& other) = delete;

				/**
				 * @brief Move assignment.
				 * @param other Client whose state is transferred.
				 * @return Reference to this client.
				 */
				Client& operator=(Client&& other) noexcept;

				/**
				 * @brief Connect to a remote host.
				 * @param protocol Address family.
				 * @param address Hostname or IP.
				 * @param port Port number.
				 * @return true on success.
				 */
				bool Connect(const Connection::Protocol& protocol, std::string_view address, const unsigned short& port) override;

				/**
				 * @brief Disconnect if connected.
				 */
				void Disconnect() noexcept override;

				/**
				 * @brief Current connection status.
				 * @return Status.
				 */
				Connection::Status Status() const noexcept override;

				/**
				 * @brief Shared live telemetry for this Client instance only.
				 * @return Client-owned counters and request latency.
				 */
				StormByte::Safe::Shared<ClientTelemetry> Telemetry() const noexcept;

			protected:
				/**
				 * @brief Send @p packet and return the response (or nullptr).
				 * @param packet Request packet.
				 * @return Response, or nullptr on error.
				 */
				PacketPointer Send(const Transport::Packet& packet) noexcept;

				/**
				 * @brief Attach a reader to an application-authorized remote file mount.
				 * @param mount Public mount descriptor decoded by the application factory.
				 * @return Caller-owned remote reader, or an empty handle on failure.
				 */
				RemoteFileReaderHandle CreateRemoteFileReader(const RemoteFileMount& mount) noexcept;

				/**
				 * @brief Attach a writer to an application-authorized exclusive remote mount.
				 * @param mount Public mount descriptor decoded by the application factory.
				 * @return Caller-owned remote writer, or an empty handle on failure.
				 */
				RemoteFileWriterHandle CreateRemoteFileWriter(const RemoteFileMount& mount) noexcept;

			private:
				/**
				 * @class Implementation
				 * @brief Private connection and remote-file state.
				 */
				class Implementation;

				/**
				 * @brief Base-owned private connection backend.
				 */
				StormByte::Safe::Unique<Implementation> m_backend;
		};
	}
}

/**
 * @brief Client state is owned by Network.
 *
 * Construct exact derived types with Safe factories and keep Base, Network
 * and the leaf provider loaded until destruction with a compatible ABI.
 */
STORMBYTE_DECLARE_MAYBE_SAFE(StormByte::Network::Client);
