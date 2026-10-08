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

#include <StormByte/network/detail/remote_file_records.hxx>
#include <StormByte/network/remote_file_mount.hxx>
#include <StormByte/network/remote_file_protocol.hxx>
#include <StormByte/network/socket/server.hxx>
#include <StormByte/safe/atomic.hxx>
#include <StormByte/safe/binary.hxx>
#include <StormByte/safe/map.hxx>
#include <StormByte/safe/mutex.hxx>
#include <StormByte/safe/optional.hxx>
#include <StormByte/safe/pointers.hxx>
#include <StormByte/safe/string.hxx>
#include <StormByte/safe/vector.hxx>

#include <chrono>
#include <cstddef>
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
		 * @namespace StormByte::Network::Detail::RemoteFile
		 * @brief Private server endpoint for one application-authorized file mount.
		 */
		namespace Detail::RemoteFile {
			/**
			 * @class MountRegistry
			 * @brief Server-wide token and one-handle-per-path registry.
			 */
			class STORMBYTE_NETWORK_PRIVATE MountRegistry final {
				public:
					/**
					 * @brief Capability type used by remote mounts.
					 */
					using Token = RemoteFileMount::ChannelToken;

					/**
					 * @brief Construct an empty registry.
					 */
					MountRegistry();

					/**
					 * @brief Copy construction is disabled for synchronized registry state.
					 * @param other Registry that cannot be copied.
					 */
					MountRegistry(const MountRegistry& other) = delete;

					/**
					 * @brief Move construction is disabled for synchronized registry state.
					 * @param other Registry that cannot be moved.
					 */
					MountRegistry(MountRegistry&& other) = delete;

					/**
					 * @brief Destroy mounted files inside Network.
					 */
					~MountRegistry() noexcept;

					/**
					 * @brief Copy assignment is disabled.
					 * @param other Registry that cannot be copied.
					 * @return This registry (operation is deleted).
					 */
					MountRegistry& operator=(const MountRegistry& other) = delete;

					/**
					 * @brief Move assignment is disabled.
					 * @param other Registry that cannot be moved.
					 * @return This registry (operation is deleted).
					 */
					MountRegistry& operator=(MountRegistry&& other) = delete;

					/**
					 * @brief Register one authorized token and acquire its shared path handle.
					 * @param token Random mount capability.
					 * @param path UTF-8 server-local path selected by application policy.
					 * @param access Reader or exclusive writer access.
					 * @return Whether registration succeeded.
					 */
					bool AddMount(const Token& token, std::string_view path, RemoteFileMount::Access access) noexcept;

					/**
					 * @brief Check whether this registry still owns one token.
					 * @param token Capability to look up.
					 * @return Whether the capability is registered.
					 */
					bool HasToken(const Token& token) const noexcept;

					/**
					 * @brief Release one token and its unused path handle.
					 * @param token Capability to release.
					 * @return Whether a capability was released.
					 */
					bool ReleaseToken(const Token& token) noexcept;

					/**
					 * @brief Execute one authorized file operation on the worker pool.
					 * @param request Operation to execute.
					 * @return Correlated operation response.
					 */
					Message Execute(const Message& request) noexcept;

				private:
					/**
					 * @brief Shared descriptor whose backend stays in Network.
					 */
					using FileHandle = MountedFile;

					/**
					 * @brief Capability descriptor stored by value in the Safe table.
					 */
					using TokenEntry = MountEntry;

					mutable Safe::Mutex m_mutex;	///< Protects path and token tables.
					Safe::Map<Safe::String, Safe::Shared<FileHandle>> m_files;	///< One handle per path.
					Safe::Map<Safe::Binary, TokenEntry> m_tokens;	///< Exact binary capabilities.
			};

			/**
			 * @class Host
			 * @brief One multiplexed server data-plane endpoint for one application peer.
			 */
			class STORMBYTE_NETWORK_PRIVATE Host final {
				public:
				/**
				 * @brief Fixed-size mount capability used on this peer plane.
				 */
				using Token = RemoteFileMount::ChannelToken;

				/**
				 * @brief Create one host endpoint for a peer, sharing the server mount registry.
				 * @param protocol Listener address family.
				 * @param bind_address Owned bind address.
				 * @param input Incoming byte pipeline.
				 * @param output Outgoing byte pipeline.
				 * @param timeout_seconds Heartbeat deadline in seconds.
				 * @param registry Shared server-wide capability registry.
				 * @param logger Shared diagnostic logger.
				 */
				Host(Connection::Protocol protocol, Safe::String bind_address, Buffer::Pipeline input,
					Buffer::Pipeline output, std::uint16_t timeout_seconds,
					Safe::Shared<MountRegistry> registry, Safe::Shared<Logger::Log> logger);

				/**
				 * @brief Copy construction is disabled for live peer state.
				 * @param other Host that cannot be copied.
				 */
				Host(const Host& other) = delete;

				/**
				 * @brief Move construction is disabled for live peer state.
				 * @param other Host that cannot be moved.
				 */
				Host(Host&& other) = delete;

				/**
				 * @brief Stop accepting and release this peer's mounts.
				 */
				~Host() noexcept;

				/**
				 * @brief Copy assignment is disabled.
				 * @param other Host that cannot be copied.
				 * @return This host (operation is deleted).
				 */
				Host& operator=(const Host& other) = delete;

				/**
				 * @brief Move assignment is disabled.
				 * @param other Host that cannot be moved.
				 * @return This host (operation is deleted).
				 */
				Host& operator=(Host&& other) = delete;

				/**
				 * @brief Bind one ephemeral peer data-plane listener.
				 * @return Whether the listener was started.
				 */
				bool Start() noexcept;

				/**
				 * @brief Port assigned to this peer plane.
				 * @return Assigned port, or zero before start.
				 */
				std::uint16_t Port() const noexcept;

				/**
				 * @brief Heartbeat timeout selected when the plane was mounted.
				 * @return Maximum silence interval in seconds.
				 */
				std::uint16_t TimeoutSeconds() const noexcept;

				/**
				 * @brief Check whether the plane has ended.
				 * @return Whether shutdown completed.
				 */
				bool Finished() const noexcept;

				/**
				 * @brief Stop the peer plane and release all tokens owned by it.
				 */
				void Stop() noexcept;

				/**
				 * @brief Add one registered token to this peer's authorized tokens.
				 * @param token Registered capability.
				 * @return Whether the capability was added.
				 */
				bool RegisterToken(const Token& token) noexcept;

				/**
				 * @brief Roll back one token when mount registration fails.
				 * @param token Capability to remove and release.
				 */
				void UnregisterToken(const Token& token) noexcept;

				/**
				 * @brief Current native listener or accepted-client handle.
				 * @return Current socket handle, or its empty value.
				 */
				Connection::HandlerType Handle() const noexcept;

				/**
				 * @brief Check whether this peer is waiting for its single accept.
				 * @return Whether one connection may still be accepted.
				 */
				bool WaitingForAccept() const noexcept;

				/**
				 * @brief Check whether another bounded operation can be read.
				 * @return Whether input capacity remains.
				 */
				bool CanRead() const noexcept;

				/**
				 * @brief Check whether a frame is ready for worker-pool submission.
				 * @return Whether an unblocked pending operation exists.
				 */
				bool ReadyForProcessing() const noexcept;

				/**
				 * @brief Check whether a complete frame or invalid length is buffered.
				 * @return Whether parsing can proceed without another socket read.
				 */
				bool HasBufferedFrame() const noexcept;

				/**
				 * @brief Check whether non-blocking output is queued.
				 * @return Whether at least one response frame remains.
				 */
				bool HasOutput() const noexcept;

				/**
				 * @brief Block task submission while worker-pool capacity is exhausted.
				 * @param blocked Whether submission is blocked.
				 */
				void SetTaskBlocked(bool blocked) noexcept;

				/**
				 * @brief Check whether the heartbeat or authorization deadline expired.
				 * @return Whether the peer silence exceeds its timeout.
				 */
				bool Expired() const noexcept;

				/**
				 * @brief Accept the single client connection on the peer listener.
				 * @return Whether one connection was accepted.
				 */
				bool AcceptReady() noexcept;

				/**
				 * @brief Read and parse frames from the ready peer socket.
				 * @return Success or a framing, transport or authorization error.
				 */
				StormByte::Network::ExpectedVoid ReadReady() noexcept;

				/**
				 * @brief Take the single queued operation for the worker pool.
				 * @return Pending request, or a default message when unavailable.
				 */
				Message TakeRequest() noexcept;

				/**
				 * @brief Restore an operation after a bounded worker-pool submission race.
				 * @param request Operation to restore.
				 */
				void RequeueRequest(Message request) noexcept;

				/**
				 * @brief Process one authorized disk operation on a worker.
				 * @param request Operation to execute.
				 * @return Correlated file response.
				 */
				Message ProcessRequest(const Message& request) noexcept;

				/**
				 * @brief Queue one response for non-blocking event-loop output.
				 * @param response Response to serialize and transform.
				 * @return Whether bounded output accepted the response.
				 */
				bool QueueResponse(const Message& response) noexcept;

				/**
				 * @brief Flush queued output once without waiting.
				 * @return Whether output drained, or a transport error.
				 */
				StormByte::Expected<bool, ConnectionError> FlushOutput() noexcept;

				private:
				/**
				 * @brief Serialize and transform one bounded response.
				 * @param response Response to queue.
				 * @return Whether the response was queued.
				 */
				bool QueueEncodedResponse(const Message& response) noexcept;

				/**
				 * @brief Release every capability belonging to this peer.
				 */
				void ReleaseTokens() noexcept;

				/**
				 * @brief Check the peer's authorized capability table.
				 * @param token Capability to check.
				 * @return Whether the peer owns this capability.
				 */
				bool IsRegisteredToken(const RemoteFileMount::ChannelToken& token) const noexcept;

				Connection::Protocol m_protocol;	///< Listener address family.
				Safe::String m_bind_address;		///< Owned bind address.
				Buffer::Pipeline m_input;		///< Incoming transformations.
				Buffer::Pipeline m_output;		///< Outgoing transformations.
				std::uint16_t m_timeout_seconds;	///< Heartbeat silence interval.
				Safe::Shared<MountRegistry> m_registry;	///< Shared server capability registry.
				Safe::Shared<Logger::Log> m_logger;	///< Shared diagnostic logger.
				Safe::Unique<Socket::Server> m_listener;	///< Network-owned peer listener.
				Safe::Shared<Socket::Client> m_active_client;	///< Accepted peer socket.
				mutable Safe::Mutex m_mutex;		///< Guards queued operations and responses.
				Safe::Mutex m_encoding_mutex;	///< Serializes outgoing transformations.
				Safe::Vector<Safe::Binary> m_output_queue;	///< Up to four waiting frames.
				Safe::Map<Safe::Binary, bool> m_registered_tokens;	///< Peer-owned capabilities.
				Safe::Map<Safe::Binary, bool> m_attached_tokens;	///< Attached capabilities.
				Safe::Binary m_input_buffer;		///< Partial framed input bytes.
				Safe::Optional<Message> m_pending_request;	///< Single pending file operation.
				Safe::Binary m_output_buffer;	///< Active transformed response frame.
				ByteSize m_output_offset{0};		///< Bytes already sent from the active frame.
				bool m_in_flight{false};			///< Worker is executing one operation.
				bool m_task_blocked{false};		///< Worker queue has no free slot.
				bool m_accepted{false};			///< The single accept already occurred.
				Safe::Atomic<bool> m_stopping{false};	///< Shutdown was requested.
				Safe::Atomic<bool> m_finished{false};	///< Shutdown completed.
				std::uint16_t m_port{0};			///< Assigned peer port.
				std::uint64_t m_last_request_id{0};	///< Last monotonic request sequence.
				std::chrono::steady_clock::time_point m_last_activity;	///< Last valid frame or start.
			};
		}
	}
}

STORMBYTE_DECLARE_MAYBE_SAFE(StormByte::Network::Detail::RemoteFile::MountRegistry);

STORMBYTE_DECLARE_MAYBE_SAFE(StormByte::Network::Detail::RemoteFile::Host);