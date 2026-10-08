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

#include <StormByte/network/detail/server_state.hxx>
#include <StormByte/network/endpoint.hxx>
#include <StormByte/network/remote_file_mount.hxx>
#include <StormByte/network/server_telemetry.hxx>
#include <StormByte/safe/atomic.hxx>
#include <StormByte/safe/deque.hxx>
#include <StormByte/safe/map.hxx>
#include <StormByte/safe/mutex.hxx>
#include <StormByte/safe/pair.hxx>
#include <StormByte/safe/pointers.hxx>
#include <StormByte/safe/string.hxx>
#include <StormByte/safe/thread.hxx>
#include <StormByte/safe/vector.hxx>
#include <StormByte/size.hxx>

#include <cstdint>
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
		 * @brief Private implementation details of the Network module.
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
				 * @brief Stop the source before transferring its inactive state.
				 * @param other Server whose state is transferred.
				 * @pre Neither server is moved from its event loop or a packet worker.
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
				 * @brief Stop both servers before transferring inactive state.
				 * @param other Server whose state is transferred.
				 * @return Reference to this server.
				 * @pre Neither server is moved from its event loop or a packet worker.
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
				 * @brief Install one pipeline pair for a single application session.
				 * @param uuid Server-assigned session identity.
				 * @param input Incoming transformation pipeline.
				 * @param output Outgoing transformation pipeline.
				 * @return False for missing sessions, repeated configuration, existing file
				 * planes or failed pipeline copies. No partial configuration is installed.
				 * @details Call from that session's packet handler or OnClientConnected,
				 * not from an unrelated thread. The session initially uses no-op pipelines.
				 * New file planes receive independent copies of the configured templates.
				 */
				bool ConfigureClientPipelines(std::string_view uuid, Buffer::Pipeline input,
					Buffer::Pipeline output) noexcept;

				/**
				 * @brief Admit an application opcode before its pipeline and factory run.
				 * @param uuid Server-assigned session identity.
				 * @param opcode Incoming application opcode.
				 * @return True by default; false closes only this application session.
				 * @details Runs on the event loop; synchronize state shared with workers.
				 * Frames coalesced with a handshake remain raw until the preceding handler
				 * completes. This hook then sees its updated session policy before any
				 * input transformation or packet factory executes for the next message.
				 */
				virtual bool AllowIncomingOpcode(std::string_view uuid, Transport::Packet::OpcodeType opcode) const noexcept;

				/**
				 * @brief Admit a response opcode before output transformation.
				 * @param uuid Server-assigned session identity.
				 * @param opcode Outgoing application opcode.
				 * @return True by default; false closes only this application session.
				 * @details Runs on the event loop; synchronize state shared with workers.
				 */
				virtual bool AllowOutgoingOpcode(std::string_view uuid, Transport::Packet::OpcodeType opcode) const noexcept;

				/**
				 * @brief Mount a server-owned file for an already authorized client session.
				 * @param client_uuid Client session which owns this capability.
				 * @param path Server-local path selected by application policy.
				 * @param maximum_timeout_seconds Heartbeat timeout in [3, 3600] seconds.
				 * @return Authorized descriptor, or Unavailable when mounting failed.
				 */
				RemoteFileMount MountRemoteFileReader(std::string_view client_uuid,
					std::string_view path, std::uint16_t maximum_timeout_seconds = 30) noexcept;

				/**
				 * @brief Mount a server-owned file for exclusive writing after application authorization.
				 * @param client_uuid Requesting application session.
				 * @param path Server-local file path selected by application policy.
				 * @param maximum_timeout_seconds Heartbeat timeout in [3, 3600] seconds.
				 * @return Authorized descriptor, or Unavailable when mounting failed or another writer owns the path.
				 */
				RemoteFileMount MountRemoteFileWriter(std::string_view client_uuid,
					std::string_view path, std::uint16_t maximum_timeout_seconds = 30) noexcept;

				/**
				 * @brief Disconnect a client by UUID.
				 * @param uuid Client UUID.
				 */
				void DisconnectClient(std::string_view uuid) noexcept;

				/**
				 * @brief Send the current handler's reply, then close and discard pending input.
				 * @param uuid Session handled by the calling packet worker.
				 * @details Call before returning the rejection packet from ProcessClientPacket.
				 * Network does not interpret its opcode. No subsequent queued frame is
				 * transformed, deserialized or dispatched; closure follows output draining.
				 */
				void DisconnectClientAfterReply(std::string_view uuid) noexcept;

				/**
				 * @brief Admit and initialize a newly registered application session.
				 * @param uuid Server-assigned session identity, borrowed for this call.
				 * @return True to continue packet processing; false to close the session.
				 * @details Called on the event-loop thread before any packet is dispatched.
				 * The default accepts the session. Do not block waiting for network I/O
				 * or workers; perform an application login in ProcessClientPacket instead.
				 * Synchronize derived state shared with packet workers. Rejected sessions
				 * also receive OnClientDisconnected.
				 */
				virtual bool OnClientConnected(std::string_view uuid) noexcept;

				/**
				 * @brief Release derived state when an application session is removed.
				 * @param uuid Removed session identity, borrowed for this call.
				 * @details Called once on the event-loop thread, including server shutdown.
				 * The default does nothing. An already dispatched worker may still finish;
				 * derived handlers must not recreate authorization for a removed session.
				 * Do not wait for workers in this callback. Derived destructors must call
				 * Disconnect before destroying state used by these hooks or handlers.
				 */
				virtual void OnClientDisconnected(std::string_view uuid) noexcept;

			private:
				/**
				 * @brief Result category returned by a packet worker.
				 */
				using CompletionReason = Detail::CompletionReason;

				/**
				 * @brief Worker response queued for application on the event loop.
				 */
				using Completion = Detail::Completion;

				/**
				 * @brief Active file capability and its path reservation.
				 */
				using MountedRemoteFile = Detail::MountedRemoteFile;

				static constexpr Size MAX_REMOTE_FILE_CHANNELS{128};	///< Active mount limit.

				/**
				 * @brief Event-loop action requested by a worker or callback.
				 */
				using CommandType = Detail::CommandType;

				/**
				 * @brief Event-loop command and optional target client UUID.
				 */
				using Command = Detail::Command;

				/**
				 * @brief Network-owned native wakeup handles, defined out-of-line.
				 */
				using WakeupChannel = Detail::WakeupChannel;

				Safe::Unique<Socket::Server> m_socket_server;					///< Listening socket.
				Safe::Atomic<Connection::Status> m_status{Connection::Status::Disconnected};	///< Lifecycle state.
				Safe::Thread m_accept_thread;									///< Joined accept execution.
				Safe::Unique<WakeupChannel> m_wakeup;							///< Native wakeup channel.
				Safe::Map<Safe::String, Detail::SessionRegistration> m_sessions;	///< Event-loop sessions.
				Safe::Map<Safe::String, Detail::ConfigurableConnection> m_configurable_connections;	///< Handler connections.
				Safe::Unique<Detail::WorkerPool> m_pool;							///< Bounded packet executor.
				Safe::Deque<Completion> m_completions;							///< Worker response queue.
				Connection::Protocol m_protocol{Connection::Protocol::IPv4};		///< Listener address family.
				Safe::String m_bind_address;										///< File-plane bind address.
				Safe::Vector<MountedRemoteFile> m_remote_files;					///< Authorized mounts.
				Safe::Map<Safe::String, Detail::RemotePlaneRegistration> m_remote_planes;	///< Peer planes.
				Safe::Shared<Detail::RemoteFile::MountRegistry> m_remote_file_registry;	///< Capability registry.
				Safe::Mutex m_remote_file_mutex;									///< Guards mounts and connections.
				Safe::Mutex m_completion_mutex;									///< Guards response queue.
				Safe::Deque<Command> m_commands;									///< Event-loop command queue.
				Safe::Mutex m_command_mutex;										///< Guards command queue.
				Safe::Shared<ServerTelemetry> m_telemetry;						///< Aggregate live telemetry.

				/**
				 * @brief Stop a move source before the base endpoint is transferred.
				 * @param other Source server, externally serialized against lifecycle calls.
				 * @return Stopped source server.
				 * @pre The caller is neither the source event loop nor a source worker.
				 */
				static Server&& StopForMove(Server& other) noexcept;

				/**
				 * @brief Transfer state after both servers have stopped their callbacks.
				 * @param other Stopped source server.
				 * @details Only persistent configuration, registry ownership and telemetry
				 * are retained. Threads, sessions, native wakeups and callback queues stay
				 * stopped and empty; a later Connect creates callbacks for this instance.
				 */
				void MoveStoppedState(Server& other) noexcept;

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
				void ProcessSession(const Safe::Shared<Detail::Session>& session, bool readable, bool writable) noexcept;

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
				void ProcessRemotePlane(const Safe::Shared<Detail::RemoteFile::Host>& host,
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

STORMBYTE_DECLARE_MAYBE_SAFE(StormByte::Network::Server);
