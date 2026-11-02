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

#include <StormByte/crypto/tls/session.hxx>
#include <StormByte/network/client_telemetry.hxx>
#include <StormByte/network/discovery.hxx>
#include <StormByte/network/endpoint.hxx>
#include <StormByte/network/remote_file.hxx>
#include <StormByte/safe/atomic.hxx>
#include <StormByte/safe/function.hxx>
#include <StormByte/safe/mutex.hxx>
#include <StormByte/safe/pointers.hxx>
#include <StormByte/safe/string.hxx>

#include <chrono>
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

			/**
			 * @brief Managed single-slot DNS resolver owned by Network.
			 */
			class Resolver;
		}

		/**
		 * @namespace StormByte::Network::Socket
		 * @brief Private native transport implementation.
		 */
		namespace Socket {
			/**
			 * @brief Native stream owner, never exposed by the public client.
			 */
			class Client;
		}

		/**
		 * @namespace StormByte::Network::Detail
		 * @brief Network-owned endpoint implementation state.
		 */
		namespace Detail {
			/**
			 * @brief Forward declaration of copied TLS configuration.
			 */
			class TLSConfiguration;
		}

		/**
		 * @class Client
		 * @brief Abstract application client.
		 *
		 * Derive and use protected Send() for request/response. ConfigureTLS enables
		 * authenticated TLS before connecting, including every remote-file channel.
		 *
		 * @note Inheritance-oriented. Not for direct generic use without a subclass.
			 * @details All owned fields use Safe handles; construction, moves,
			 * assignment and destruction run inside Network. Allocate an exact derived
			 * type through its provider's Safe factory when sharing ownership across
			 * modules; the base registration does not certify derived state. Keep Base,
			 * Network, Crypto, logger, packet and callback providers loaded until
			 * their objects are released. Connect and requests serialize internally;
			 * Cancel, Disconnect, Status and Telemetry may be used concurrently.
			 * Externally synchronize moves, destruction and caller-owned callback state.
			 * Opcode UUIDs and Connect addresses are synchronous borrowed views.
		 */
		class STORMBYTE_NETWORK_PUBLIC Client: private Endpoint {
			public:
				/**
				 * @brief Explicit request/connect failures; failed streams are never replayed.
				 */
				enum class Error {
					InvalidTimeout, ///< Negative budgets are invalid.
					NotConnected, ///< No usable application connection.
					AlreadyConnected, ///< Connect would replace a live stream.
					TimedOut, ///< The total operation budget expired.
					Cancelled, ///< Cancel or Disconnect interrupted this operation.
					ResolverBusy, ///< A previous OS DNS lookup is still running.
					Transport, ///< DNS, native I/O, EOF or incomplete framing.
					TLS, ///< TLS configuration or authentication failed.
					Packet, ///< Opcode policy, transformation or deserialization failed.
					Allocation ///< Network could not allocate operation state.
				};

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
				 * @brief Transfer Safe owners inside Network without moving the mutex.
				 * @param other Client whose state is transferred.
				 */
				Client(Client&& other) noexcept;

				/**
				 * @brief Disconnect and release Safe owners inside Network.
				 */
				virtual ~Client() noexcept;

				/**
				 * @brief Copy assignment (deleted).
				 * @param other Client that cannot be copied.
				 * @return Reference to this client (operation is deleted).
				 */
				Client& operator=(const Client& other) = delete;

				/**
				 * @brief Disconnect this client before transferring Safe owners in Network.
				 * @param other Client whose state is transferred.
				 * @return Reference to this client.
				 */
				Client& operator=(Client&& other) noexcept;

				/**
				 * @brief Connect to a remote host with a ten-second total default budget.
				 * @param protocol Address family.
				 * @param address Hostname or IP borrowed for this call, without embedded nulls.
				 * @param port Port number.
				 * @return true on success.
				 */
				bool Connect(const Connection::Protocol& protocol, std::string_view address, const unsigned short& port) override;

				/**
				 * @brief Resolve, connect and authenticate using one total time budget.
				 * @param protocol Address family.
				 * @param address Borrowed hostname or IP.
				 * @param port Destination port.
				 * @param timeout Total budget; zero expires immediately, max means unlimited.
				 * @return Completion or typed failure; a failed connection is discarded.
				 * @details OS DNS is not preemptible: a stopped lookup keeps the managed slot
				 * busy until it finishes, and destruction joins it. User callbacks cannot
				 * be preempted. Connect and Send serialize; moves and destruction require
				 * external lifetime synchronization, including against Cancel.
				 */
				Expected<void, Error> Connect(const Connection::Protocol& protocol, std::string_view address,
					unsigned short port, std::chrono::milliseconds timeout);

				/**
				 * @brief Cancel the active operation and invalidate its application stream.
				 * @details Thread-safe against Connect, Send, Status and Disconnect.
				 * Shuts down connected I/O without closing/reusing a handle concurrently.
				 * Cancellation is sticky until the next Connect; no failed frame is replayed.
				 * Remote-file channels retain their independent lifecycle.
				 */
				void Cancel() noexcept;

				/**
				 * @brief Require TLS for subsequent application and remote-file connections.
				 * @param configuration Borrowed Crypto inputs, deeply copied during this call.
				 * @param refresh_validation_time Use current time for each new session by default;
				 * false explicitly preserves the supplied instant for offline validation.
				 * @return False when connected or configuration validation fails; no state changes.
				 * @details Requires an explicit expected server identity. Retained across reconnects.
				 * Externally serialize configuration, connection, moves and destruction.
				 */
				bool ConfigureTLS(const Crypto::TLS::ClientConfiguration& configuration,
					bool refresh_validation_time = true) noexcept;

				/**
				 * @brief Restore plaintext for future connections while disconnected.
				 * @return False while an application or file channel remains attached.
				 */
				bool DisableTLS() noexcept;

				/**
				 * @brief Cancel I/O and disconnect if connected.
				 * @details Nonblocking when an operation owns the stream: that operation
				 * releases it on return. Safe from packet callbacks, like Cancel and Status.
				 * Other client calls must not reenter from callbacks. Destruction still
				 * requires all calls, including callbacks and cancellation, to have returned.
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

				/**
				 * @brief Send a discovery query and invoke @p on_reply for each response.
				 * @param protocol Address family.
				 * @param discovery_port UDP port where servers announce.
				 * @param timeout Maximum time to wait for replies.
				 * @param on_reply Called for each datagram. Return true to stop, false to continue.
				 * @return true if the query was sent. Does not imply a reply was received.
				 * @details Uses MakeDiscoveryQuery() as the payload. Empty query does not send.
				 */
				bool Discover(const Connection::Protocol& protocol, unsigned short discovery_port,
					std::chrono::milliseconds timeout,
					StormByte::Safe::Function<bool(const DiscoveryReply&)> on_reply) noexcept;


			protected:
				/**
				 * @brief Admit a received opcode before transformation and deserialization.
				 * @param uuid Local connection identity, borrowed during the call.
				 * @param opcode Incoming application opcode.
				 * @return True by default; false closes the application connection.
				 */
				virtual bool AllowIncomingOpcode(std::string_view uuid, Transport::Packet::OpcodeType opcode) const noexcept;

				/**
				 * @brief Admit an outgoing opcode before applying the output pipeline.
				 * @param uuid Local connection identity, borrowed during the call.
				 * @param opcode Outgoing application opcode.
				 * @return True by default; false prevents sending and closes the connection.
				 */
				virtual bool AllowOutgoingOpcode(std::string_view uuid, Transport::Packet::OpcodeType opcode) const noexcept;

				/**
				 * @brief Build the opaque discovery query.
				 * @return Query bytes. Empty means discovery is not used.
				 */
				virtual StormByte::Safe::Binary MakeDiscoveryQuery() const noexcept;

				/**
				 * @brief Interpret a discovery reply into a TCP endpoint.
				 * @param reply Opaque reply payload.
				 * @param address Filled with the TCP address when accepted.
				 * @param port Filled with the TCP port when accepted.
				 * @return true if this reply identifies a usable server.
				 */
				virtual bool InterpretDiscoveryReply(const StormByte::Safe::Binary& reply,
					StormByte::Safe::String& address, unsigned short& port) const noexcept;


				/**
				 * @brief Send @p packet and return the response (or nullptr).
				 * @param packet Request packet.
				 * @return Response, or nullptr on error.
				 */
				PacketPointer Send(const Transport::Packet& packet) noexcept;

				/**
				 * @brief Send and receive one packet under a single total budget.
				 * @param packet Borrowed request.
				 * @param timeout Total send/header/payload budget; max means unlimited.
				 * @return Response or distinct failure; failure discards the stream.
				 * @details Includes pipeline and packet callbacks, which are synchronous
				 * and cannot be preempted. Expiry/cancellation is checked on their return.
				 * Legacy Send keeps an unlimited budget and returns an empty handle on failure.
				 */
				Expected<PacketPointer, Error> Send(const Transport::Packet& packet, std::chrono::milliseconds timeout);

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
				 * @brief Remove and close the application stream while the operation lock is held.
				 */
				void DropConnection() noexcept;

				Safe::Mutex m_operation_mutex; ///< Serializes connect, requests and configuration.
				mutable Safe::Mutex m_state_mutex; ///< Guards connection publication and cancellation.
				Safe::Atomic<bool> m_cancelled{false}; ///< Sticky cancellation for the current stream.
				Safe::Shared<Socket::Client> m_active_socket; ///< Published only after native connect.
				Safe::Shared<Connection::Resolver> m_resolver; ///< Retained bounded DNS worker slot.
				Safe::Shared<const Detail::TLSConfiguration> m_tls_configuration; ///< Immutable TLS inputs, not session state.
				Safe::Shared<Connection::Client> m_connection;					///< Active framed connection.
				Safe::String m_remote_address;								///< Address used for private file channels.
				Connection::Protocol m_protocol{Connection::Protocol::IPv4};	///< File-channel address family.
				Safe::Mutex m_remote_file_mutex;								///< Guards peer-plane creation and mount registration.
				Safe::Shared<Detail::RemoteFile::DataPlane> m_remote_file_plane;	///< Shared peer data plane.
				Safe::Shared<ClientTelemetry> m_telemetry;						///< Client-scoped counters.
		};
	}
}

STORMBYTE_DECLARE_MAYBE_SAFE(StormByte::Network::Client);
