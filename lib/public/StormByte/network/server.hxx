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

#include <atomic>
#include <filesystem>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <deque>
#include <string_view>
#include <vector>

/**
 * @brief Network module of the StormByte suite.
 */
namespace StormByte::Network {
	namespace Connection {
		class Client;	///< Forward declaration
	}

	namespace Socket {
		class Server;	///< Forward declaration
	}

	namespace Detail {
		class Session;	///< Forward declaration
		class WorkerPool;	///< Forward declaration
		namespace RemoteFile {
			class Host;	///< Forward declaration
			class MountRegistry; ///< New class for managing mount registrations
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
			Server(DeserializePacketFunction deserialize_packet_function, StormByte::Safe::Shared<Logger::Log> logger) noexcept;

			/**
			 * @brief Copy constructor (deleted).
			 */
			Server(const Server& other) = delete;

			/**
			 * @brief Move constructor.
			 */
			Server(Server&& other) noexcept;

			/**
			 * @brief Destructor (joins threads, disconnects clients).
			 */
			virtual ~Server() noexcept;

			/**
			 * @brief Copy assignment (deleted).
			 */
			Server& operator=(const Server& other) = delete;

			/**
			 * @brief Move assignment.
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
			std::unique_ptr<Socket::Server> m_socket_server;											///< Listen socket
			std::atomic<Connection::Status> m_status;												///< Server status
			std::thread m_accept_thread;															///< Accept loop thread
			Connection::HandlerType m_wakeup_read;												///< Wakeup read handle
			Connection::HandlerType m_wakeup_write;												///< Wakeup write handle
			std::unordered_map<std::string, std::shared_ptr<Detail::Session>> m_sessions;	///< Active parser sessions
			std::unique_ptr<Detail::WorkerPool> m_pool;														///< Packet worker pool
			enum class CompletionReason: unsigned short { Success, NullHandler, Error }; ///< Completion outcome
			struct Completion {
				std::string uuid; ///< Client UUID
				PacketPointer packet; ///< Response packet
				CompletionReason reason; ///< Completion outcome
			};
			std::deque<Completion> m_completions; ///< Worker completions
			Connection::Protocol m_protocol{Connection::Protocol::IPv4}; ///< Bound protocol.
			std::string m_bind_address; ///< Bound address for private listeners.
			struct MountedRemoteFile {
				std::shared_ptr<Detail::RemoteFile::Host> host; ///< Authorized private channel.
				std::string path_key; ///< Canonical path used for shared/exclusive mount reservation.
				RemoteFileMount::Access access; ///< Shared reader or exclusive writer reservation.
				RemoteFileMount::ChannelToken token; ///< Capability reservation released by CloseToken.
			};
			std::vector<MountedRemoteFile> m_remote_files; ///< Authorized private channels.
			std::unordered_map<std::string, std::shared_ptr<Detail::RemoteFile::Host>> m_remote_planes; ///< One plane per peer UUID.
			std::shared_ptr<Detail::RemoteFile::MountRegistry> m_remote_file_registry; ///< Shared path handles and token table.
			std::mutex m_remote_file_mutex; ///< Protects private channel registration.
			static constexpr std::size_t MAX_REMOTE_FILE_CHANNELS = 128; ///< Bound channel resource use.
			std::mutex m_completion_mutex; ///< Protects completions
			enum class CommandType: unsigned short { DisconnectClient, DisconnectAll, Stop }; ///< Loop command
			struct Command { CommandType type; std::string uuid; }; ///< Loop command
			std::deque<Command> m_commands; ///< Commands from workers/user callbacks
			std::mutex m_command_mutex; ///< Protects commands

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
			 */
			void ProcessSession(const std::shared_ptr<Detail::Session>& session, bool readable, bool writable) noexcept;

			/** @brief Enqueue a worker completion and wake EventLoop. */
			void PostCompletion(Completion completion) noexcept;

			/** @brief Apply all worker completions on EventLoop. */
			void DrainCompletions() noexcept;

			/** @brief Enqueue a command for EventLoop. */
			void PostCommand(Command command) noexcept;

			/** @brief Apply all commands on EventLoop. */
			void DrainCommands() noexcept;

			/** @brief Direct session removal, called only by EventLoop. */
			void DisconnectClientOnLoop(std::string_view uuid) noexcept;

			/** @brief Revoke and stop every private file channel. */
			void RevokeAllRemoteFiles() noexcept;

			/** @brief Read one peer data plane and enqueue its file operation on WorkerPool. */
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
