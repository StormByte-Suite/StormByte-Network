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
#include <StormByte/network/remote_file_mount.hxx>
#include <StormByte/network/server_telemetry.hxx>
#include <StormByte/safe/pointers.hxx>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
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
		 * @namespace StormByte::Network::Socket
		 * @brief Socket namespace.
		 */
		namespace Socket {
			/**
			 * @class Server
			 * @brief Forward declaration of the listening socket implementation.
			 */
			class Server;
		}

		/**
		 * @namespace StormByte::Network::Detail
		 * @brief Private implementation namespace of the Network module.
		 */
		namespace Detail {
			/**
			 * @class Session
			 * @brief Forward declaration of a client parser session.
			 */
			class Session;

			/**
			 * @class WorkerPool
			 * @brief Forward declaration of the packet worker pool.
			 */
			class WorkerPool;

			/**
			 * @namespace StormByte::Network::Detail::RemoteFile
			 * @brief Private remote-file implementation namespace.
			 */
			namespace RemoteFile {
				/**
				 * @class Host
				 * @brief Forward declaration of a private peer data plane.
				 */
				class Host;

				/**
				 * @class MountRegistry
				 * @brief Forward declaration of the remote-file mount registry.
				 */
				class MountRegistry;
			}
		}

		/**
		 * @class Server
		 * @brief Abstract application server.
		 *
		 * Listen socket and single-threaded event loop. Implement ProcessClientPacket(); override pipelines as needed.
		 *
		 * @note Inheritance-oriented. Subclass required.
		 */
		class STORMBYTE_NETWORK_PUBLIC Server: private Endpoint {
			public:
				/**
				 * @brief Construct with a packet factory and a logger.
				 * @param deserialize_packet_function Builds domain packets from wire data.
				 * @param logger Diagnostic logger.
				 */
				Server(DeserializePacketFunction deserialize_packet_function, StormByte::Safe::Shared<Logger::Log> logger);

				/**
				 * @brief Copy constructor (deleted).
				 * @param other Server that cannot be copied.
				 */
				Server(const Server& other) = delete;

				/**
				 * @brief Move constructor.
				 * @param other Server whose state is transferred.
				 */
				Server(Server&& other) noexcept;

				/**
				 * @brief Destructor (joins threads, disconnects clients).
				 */
				virtual ~Server() noexcept;

				/**
				 * @brief Copy assignment (deleted).
				 * @param other Server that cannot be copied.
				 * @return Reference to this server (operation is deleted).
				 */
				Server& operator=(const Server& other) = delete;

				/**
				 * @brief Move assignment.
				 * @param other Server whose state is transferred.
				 * @return Reference to this server.
				 */
				Server& operator=(Server&& other) noexcept;

				/**
				 * @brief Bind, listen and start the accept thread.
				 * @param protocol Address family.
				 * @param address Bind address.
				 * @param port Port number.
				 * @return true on success.
				 */
				bool Connect(const Connection::Protocol& protocol, std::string_view address, const unsigned short& port) override;

				/**
				 * @brief Stop accept, disconnect clients and close the listener.
				 */
				void Disconnect() noexcept override;

				/**
				 * @brief Listener / server status.
				 * @return Status.
				 */
				Connection::Status Status() const noexcept override;

				/**
				 * @brief Shared live telemetry aggregated across this Server's sessions.
				 * @return Server-owned aggregate counters and handler latency.
				 */
				StormByte::Safe::Shared<ServerTelemetry> Telemetry() const noexcept;

			protected:
				/**
				 * @brief Mount a server-owned file for an already authorized client session.
				 * @param client_uuid Client session which owns this capability.
				 * @param path Server-local path selected by application policy.
				 * @param maximum_timeout_seconds Heartbeat timeout in [3, 3600] seconds.
				 * @return Authorized descriptor, or Unavailable when mounting failed.
				 */
				RemoteFileMount MountRemoteFileReader(std::string_view client_uuid,
					const std::filesystem::path& path, std::uint16_t maximum_timeout_seconds = 30) noexcept;

				/**
				 * @brief Mount a server-owned file for exclusive writing after application authorization.
				 * @param client_uuid Requesting application session.
				 * @param path Server-local file path selected by application policy.
				 * @param maximum_timeout_seconds Heartbeat timeout in [3, 3600] seconds.
				 * @return Authorized descriptor, or Unavailable when mounting failed or another writer owns the path.
				 */
				RemoteFileMount MountRemoteFileWriter(std::string_view client_uuid,
					const std::filesystem::path& path, std::uint16_t maximum_timeout_seconds = 30) noexcept;

				/**
				 * @brief Disconnect a client by UUID.
				 * @param uuid Client UUID.
				 */
				void DisconnectClient(std::string_view uuid) noexcept;

			private:
				/**
				 * @brief Result category returned by a packet worker.
				 */
				enum class CompletionReason: unsigned short {
					/**
					 * @brief Packet handling completed successfully.
					 */
					Success,

					/**
					 * @brief Packet handler returned no response packet.
					 */
					NullHandler,

					/**
					 * @brief Packet handling failed.
					 */
					Error
				};

				/**
				 * @struct Completion
				 * @brief Worker response queued for application on the event loop.
				 */
				struct Completion {
					/**
					 * @brief UUID of the client session that submitted the packet.
					 */
					std::string uuid;

					/**
					 * @brief Application response packet, if one was produced.
					 */
					PacketPointer packet;

					/**
					 * @brief Worker handler outcome.
					 */
					CompletionReason reason;
				};

				/**
				 * @struct MountedRemoteFile
				 * @brief Active file capability and its path reservation.
				 */
				struct MountedRemoteFile {
					/**
					 * @brief Peer data plane serving this mount.
					 */
					std::shared_ptr<Detail::RemoteFile::Host> host;

					/**
					 * @brief Canonical path key held by this reservation.
					 */
					std::string path_key;

					/**
					 * @brief Shared-reader or exclusive-writer access mode.
					 */
					RemoteFileMount::Access access;

					/**
					 * @brief Capability token released when the mount closes.
					 */
					RemoteFileMount::ChannelToken token;
				};

				/**
				 * @brief Upper bound on active remote-file mounts.
				 */
				static constexpr std::size_t MAX_REMOTE_FILE_CHANNELS = 128;

				/**
				 * @brief Event-loop action requested by a worker or callback.
				 */
				enum class CommandType: unsigned short {
					/**
					 * @brief Disconnect the specified client session.
					 */
					DisconnectClient,

					/**
					 * @brief Disconnect every client session.
					 */
					DisconnectAll,

					/**
					 * @brief Stop the event loop.
					 */
					Stop
				};

				/**
				 * @struct Command
				 * @brief Event-loop command and optional target client UUID.
				 */
				struct Command {
					/**
					 * @brief Command to apply on the event loop.
					 */
					CommandType type;

					/**
					 * @brief Target UUID for per-client commands.
					 */
					std::string uuid;
				};

				/**
				 * @class Implementation
				 * @brief Private listener, session, and event-loop state.
				 */
				class Implementation;

				/**
				 * @brief Base-owned private server engine.
				 */
				StormByte::Safe::Unique<Implementation> m_engine;

				/**
				 * @brief Accept-loop thread body.
				 */
				void AcceptClients() noexcept;

				/**
				 * @brief Create the private accept-loop wakeup channel.
				 * @return true when the channel is ready.
				 */
				bool CreateWakeup() noexcept;

				/**
				 * @brief Signal the accept loop to stop waiting.
				 */
				void SignalWakeup() noexcept;

				/**
				 * @brief Close both ends of the wakeup channel.
				 */
				void CloseWakeup() noexcept;

				/**
				 * @brief Accept and register one ready client.
				 */
				void AcceptOneClient() noexcept;

				/**
				 * @brief Read, process, and reply for one ready session.
				 * @param session Ready session.
				 * @param readable Whether the session socket is ready for reading.
				 * @param writable Whether the session socket is ready for writing.
				 */
				void ProcessSession(const std::shared_ptr<Detail::Session>& session, bool readable, bool writable) noexcept;

				/**
				 * @brief Enqueue a worker completion and wake the event loop.
				 * @param completion Worker result to apply.
				 */
				void PostCompletion(Completion completion) noexcept;

				/**
				 * @brief Apply all queued worker completions on the event loop.
				 */
				void DrainCompletions() noexcept;

				/**
				 * @brief Enqueue a command for the event loop.
				 * @param command Action to apply.
				 */
				void PostCommand(Command command) noexcept;

				/**
				 * @brief Apply all queued commands on the event loop.
				 */
				void DrainCommands() noexcept;

				/**
				 * @brief Remove a session directly from the event loop.
				 * @param uuid Client session to remove.
				 */
				void DisconnectClientOnLoop(std::string_view uuid) noexcept;

				/**
				 * @brief Revoke and stop every private file channel.
				 */
				void RevokeAllRemoteFiles() noexcept;

				/**
				 * @brief Read one peer data plane and enqueue its file operation.
				 * @param host Peer data plane to process.
				 * @param readable Whether the socket is ready for reading.
				 * @param writable Whether the socket is ready for writing.
				 */
				void ProcessRemotePlane(const std::shared_ptr<Detail::RemoteFile::Host>& host,
					bool readable, bool writable) noexcept;

				/**
				 * @brief Application packet handler.
				 * @param client_uuid Sender UUID.
				 * @param packet Received packet.
				 * @return Response packet, or nullptr on error / no reply.
				 */
				virtual PacketPointer ProcessClientPacket(std::string_view client_uuid, PacketPointer packet) noexcept = 0;
		};
	}
}

/**
 * @brief Server state is owned by Network.
 *
 * Derived providers must disconnect before destroying handler state and remain
 * loaded with Base and Network until destruction with a compatible ABI.
 */
STORMBYTE_DECLARE_MAYBE_SAFE(StormByte::Network::Server);
