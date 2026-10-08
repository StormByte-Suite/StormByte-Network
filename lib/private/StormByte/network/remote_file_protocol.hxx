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
#include <StormByte/network/exception.hxx>
#include <StormByte/network/remote_file_mount.hxx>
#include <StormByte/network/socket/client.hxx>
#include <StormByte/safe/atomic.hxx>
#include <StormByte/safe/binary.hxx>
#include <StormByte/safe/condition_variable.hxx>
#include <StormByte/safe/function.hxx>
#include <StormByte/safe/map.hxx>
#include <StormByte/safe/mutex.hxx>
#include <StormByte/safe/optional.hxx>
#include <StormByte/safe/pointers.hxx>
#include <StormByte/safe/string.hxx>
#include <StormByte/safe/thread.hxx>
#include <StormByte/serializable.hxx>
#include <StormByte/system/device.hxx>

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
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
		 * @brief Private remote-file implementation namespace.
		 */
		namespace Detail::RemoteFile {
			/**
			 * @brief Maximum transformed channel message accepted from the wire.
			 */
			inline constexpr std::uint64_t max_message_size = 4ull * 1024ull * 1024ull;

			/**
			 * @brief Serialized fixed header size including the capability token.
			 */
			inline constexpr std::size_t message_header_size = sizeof(std::uint8_t) * 2
				+ sizeof(std::uint64_t) * 3 + 32;

			/**
			 * @brief Maximum file bytes carried in one untransformed message.
			 */
			inline constexpr std::uint64_t max_data_size = max_message_size - message_header_size;

			/**
			 * @enum Opcode
			 * @brief Private commands understood only by the Network channel endpoints.
			 */
			enum class Opcode: std::uint8_t {
				Attach = 1, ///< Present the one-shot mount capability.
				Open = 2, ///< Open the authorized server-side file handle.
				Close = 3, ///< Close the server-side file handle.
				Read = 4, ///< Read from the server-side file cursor.
				Write = 5, ///< Write at the server-side file cursor.
				Seek = 6, ///< Seek the server-side file cursor.
				Size = 7, ///< Query the server-side file length.
				Flush = 8, ///< Flush the server-side file handle.
				Truncate = 9, ///< Truncate the server-side file to zero.
				Ping = 10, ///< Heartbeat request.
				Pong = 11, ///< Heartbeat response.
				CloseToken = 12 ///< Release one mount token without closing the peer plane.
			};

			/**
			 * @enum Status
			 * @brief Result encoded in a private channel message.
			 */
			enum class Status: std::uint8_t {
				Ok = 0, ///< Operation completed.
				End = 1, ///< Reader reached the file end.
				Failed = 2 ///< Operation failed or was not authorized.
			};

			/**
			 * @struct Message
			 * @brief One decoded private channel message.
			 */
			struct Message {
				Opcode opcode{Opcode::Ping};		///< Private command identifier.
				Status status{Status::Ok};		///< Operation result.
				std::uint64_t request_id{0};		///< Request/reply correlation identifier.
				std::uint64_t offset{0};			///< Absolute file offset for read/write operations.
				std::uint64_t value{0};			///< Command length or numeric result.
				std::array<std::byte, 32> token{};	///< Capability required for every mount operation.
				Safe::Binary data;				///< Remaining command data owned by Base.
			};
		}
	}
}

STORMBYTE_DECLARE_MAYBE_SAFE(StormByte::Network::Detail::RemoteFile::Message);

/**
 * @namespace StormByte::Network::Detail::RemoteFile
 * @brief Private remote-file implementation namespace.
 */
namespace StormByte::Network::Detail::RemoteFile {
	/**
	 * @struct Pending
	 * @brief Metadata and validated response for one outstanding exchange.
	 */
	struct Pending {
		Message request;				///< Request metadata without retained file payload.
		Safe::Optional<Message> response;	///< Response supplied by the sole socket receiver.
	};
}

STORMBYTE_DECLARE_MAYBE_SAFE(StormByte::Network::Detail::RemoteFile::Pending);

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
		 * @brief Private remote-file implementation namespace.
		 */
		namespace Detail::RemoteFile {

			/**
			 * @class DataPlane
			 * @brief One serialized, multiplexed remote-file transport for a Client peer.
			 */
			class STORMBYTE_NETWORK_PRIVATE DataPlane final {
				public:
					/**
					 * @brief Mount capability registered on this plane.
					 */
					using Token = RemoteFileMount::ChannelToken;

				/**
				 * @brief Bind the peer's single connected data socket and shared pipelines.
				 * @param socket Connected peer data-plane socket.
				 * @param input Pipeline applied after receive and before decode.
				 * @param output Pipeline applied after encode and before send.
				 * @param timeout_seconds Maximum heartbeat silence interval.
				 * @param logger Diagnostic logger.
				 * @param local_address Local interface address selected by the socket.
				 * @param device Shared polymorphic device snapshot for this plane.
				 * @param port Server data-plane port published by the mount.
				 */
				DataPlane(Safe::Shared<Socket::Client> socket, Buffer::Pipeline input,
					Buffer::Pipeline output, std::uint16_t timeout_seconds,
					Safe::Shared<Logger::Log> logger, Safe::String local_address,
					Safe::Shared<System::Device> device, std::uint16_t port) noexcept;

				/**
				 * @brief Stop both threads before releasing the shared socket and Safe state.
				 */
				~DataPlane() noexcept;

				/**
				 * @brief Register one mount capability on the established plane.
				 * @param token Capability to attach.
				 * @param on_failure Callback retaining its provider-owned context.
				 * @return Whether attachment and callback registration succeeded.
				 */
				bool RegisterToken(const Token& token, Safe::Function<void()> on_failure) noexcept;

				/**
				 * @brief Release one mount without closing the peer plane.
				 * @param token Capability to release.
				 * @return Whether the peer acknowledged release.
				 */
				bool ReleaseToken(const Token& token) noexcept;

				/**
				 * @brief Start the plane's single ping/pong monitor.
				 * @return Whether the heartbeat thread started.
				 */
				bool StartHeartbeat() noexcept;

				/**
				 * @brief Send one token-authorized operation and validate its matching response.
				 * @param request Operation to send.
				 * @return Correlated response or a connection error.
				 */
				StormByte::Expected<Message, ConnectionError> Exchange(Message request) noexcept;

				/**
				 * @brief Check whether the peer plane has failed or been stopped.
				 * @return Whether the plane is unavailable.
				 */
				bool Failed() const noexcept;

				/**
				 * @brief Close the socket and join both threads from an external caller.
				 */
				void Stop() noexcept;

				/**
				 * @brief Local address selected by the plane socket.
				 * @return Borrowed Base-owned address valid during this plane's lifetime.
				 */
				const Safe::String& LocalAddress() const noexcept;

				/**
				 * @brief Shared device snapshot for every leaf on this plane.
				 * @return Shared polymorphic device owned through its provider factory.
				 */
				StormByte::Safe::Shared<StormByte::System::Device> Device() const noexcept;

				/**
				 * @brief Server port assigned to this peer plane.
				 * @return Assigned server port.
				 */
				std::uint16_t Port() const noexcept;

			private:
				/**
				 * @brief Receive and correlate pending operation and heartbeat responses.
				 */
				void RunReceiver() noexcept;

				/**
				 * @brief Exchange periodic pings until shutdown or failure.
				 */
				void RunHeartbeat() noexcept;

				/**
				 * @brief Fail the plane, wake waiters and notify registered mounts once.
				 */
				void MarkFailed() noexcept;

				/**
				 * @brief Invoke token callbacks through their provider-owned contexts.
				 */
				void NotifyTokenFailures() noexcept;

				Safe::Shared<Socket::Client> m_socket;	///< Socket with provider-owned lifecycle.
				Buffer::Pipeline m_input;		///< Incoming transformations.
				Buffer::Pipeline m_output;		///< Outgoing transformations.
				const std::uint16_t m_timeout_seconds;	///< Heartbeat silence deadline.
				Safe::Shared<Logger::Log> m_logger;	///< Shared diagnostic logger.
				Safe::String m_local_address;	///< Local interface address owned by Base.
				Safe::Shared<System::Device> m_device;	///< Provider-owned device snapshot.
				const std::uint16_t m_port;		///< Assigned server port.
				mutable Safe::Mutex m_mutex;		///< Protects pending exchanges.
				Safe::Mutex m_operation_mutex;	///< Serializes operations independently of heartbeat.
				Safe::Mutex m_send_mutex;		///< Serializes frames and sequence assignment.
				Safe::ConditionVariable m_response_condition;	///< Wakes response waiters and receiver.
				std::array<Safe::Optional<Pending>, 2> m_pending;	///< Operation and heartbeat slots.
				Safe::Thread m_receiver_thread;	///< Sole input-pipeline and socket receiver.
				Safe::Mutex m_heartbeat_mutex;	///< Protects heartbeat shutdown state.
				Safe::ConditionVariable m_heartbeat_condition;	///< Wakes the heartbeat on shutdown.
				Safe::Mutex m_token_mutex;		///< Protects registered failure callbacks.
				Safe::Map<Safe::Binary, Safe::Optional<Safe::Function<void()>>> m_token_failures;	///< Provider-owned callbacks by capability.
				Safe::Thread m_heartbeat_thread;	///< Periodic ping/pong monitor.
				Safe::Atomic<std::uint64_t> m_next_request_id{1};	///< Next request sequence.
				Safe::Atomic<bool> m_failed{false};	///< Published plane failure state.
				bool m_stopping{false};			///< Heartbeat shutdown requested.
			};

			/**
			 * @brief Build a polymorphic network-interface snapshot for a connected socket.
			 * @param local_address Borrowed local interface address.
			 * @return Device allocated through its Safe polymorphic factory.
			 */
			Safe::Shared<System::Device> CreateNetworkDevice(std::string_view local_address);

			/**
			 * @brief Serialize a message field-by-field.
			 * @param message Message to encode.
			 * @return Base-owned wire payload.
			 */
			Safe::Binary Serialize(const Message& message);

			/**
			 * @brief Decode one exact message and reject unknown opcode/status values.
			 * @param payload Borrowed wire bytes.
			 * @return Decoded Safe message or a deserialization error.
			 */
			StormByte::Expected<Message, StormByte::DeserializeError> Deserialize(
				std::span<const std::byte> payload) noexcept;

			/**
			 * @brief Run bytes through the supplied pipeline synchronously.
			 * @param pipeline Transformations executed in order.
			 * @param input Base-owned input bytes.
			 * @param logger Shared diagnostic logger.
			 * @return Bounded Base-owned output or a connection error.
			 */
			StormByte::Expected<Safe::Binary, ConnectionError> Process(
				Buffer::Pipeline& pipeline, Safe::Binary input,
				StormByte::Safe::Shared<StormByte::Logger::Log> logger);

			/**
			 * @brief Read a fixed-width message size and bounded payload from the socket.
			 * @param socket Connected data socket.
			 * @param timeout_seconds Receive deadline in seconds.
			 * @return Base-owned payload or a connection error.
			 */
			StormByte::Expected<Safe::Binary, ConnectionError> ReceivePayload(
				Socket::Client& socket, std::uint16_t timeout_seconds) noexcept;

			/**
			 * @brief Send payload with a fixed-width size prefix.
			 * @param socket Connected data socket.
			 * @param payload Borrowed payload bytes.
			 * @return Success or a connection error.
			 */
			StormByte::Network::ExpectedVoid SendPayload(Socket::Client& socket,
				std::span<const std::byte> payload) noexcept;
		}
	}
}

STORMBYTE_DECLARE_MAYBE_SAFE(StormByte::Network::Detail::RemoteFile::DataPlane);
