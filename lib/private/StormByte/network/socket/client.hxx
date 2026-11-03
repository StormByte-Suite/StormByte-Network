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
#include <StormByte/buffer/consumer.hxx>
#include <StormByte/network/connection/resolver.hxx>
#include <StormByte/network/peer_address.hxx>
#include <StormByte/network/socket/operation.hxx>
#include <StormByte/network/socket/socket.hxx>
#include <StormByte/network/typedefs.hxx>
#include <StormByte/safe/binary.hxx>
#include <StormByte/safe/mutex.hxx>
#include <StormByte/safe/pointers.hxx>

#include <chrono>
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
		 * @namespace StormByte::Network::Detail
		 * @brief Private implementation details of the Network module.
		 */
		namespace Detail {
			/**
			 * @class Session
			 * @brief Forward declaration of the session implementation.
			 */
			class Session;

			/**
			 * @namespace StormByte::Network::Detail::RemoteFile
			 * @brief Private remote-file implementation details.
			 */
			namespace RemoteFile {
				/**
				 * @class Host
				 * @brief Forward declaration of the remote-file host.
				 */
				class Host;
			}
		}

		/**
		 * @namespace StormByte::Network::Socket
		 * @brief Socket namespace.
		 */
		namespace Socket {
			/**
			 * @class Client
			 * @brief Connected client socket (connect, send, receive, peek).
				 * @details Safe owners and lifecycle operations are provided by Network.
				 * Keep Network, Base and the logger provider loaded until release.
				 * Serialize I/O, moves and lifecycle calls externally.
			 */
			class STORMBYTE_NETWORK_PRIVATE Client final: public Socket {
				public:
					/**
					 * @brief Shared client handle allocated and destroyed through Network.
					 */
					using Pointer = StormByte::Safe::Shared<Client>;

					/**
					 * @brief Allocate a client through the Network provider.
					 * @param protocol Address family.
					 * @param logger Shared diagnostic logger.
						 * @return Shared client handle, or empty when allocation fails.
					 */
					static Pointer Create(const Connection::Protocol& protocol, StormByte::Safe::Shared<Logger::Log> logger) noexcept;

					/**
					 * @brief Attach an independent Crypto session before any application I/O.
					 * @param session Newly configured, handshaking session.
					 * @param synchronous Whether to complete the bounded handshake in this call.
					 * @return False on setup, authentication or transport failure.
					 */
					bool StartTLS(Crypto::TLS::Session session, bool synchronous) noexcept;

					/**
					 * @brief Authenticate under the caller's total connect deadline.
					 * @param session Independent configured session.
					 * @param control Total connect budget and cancellation.
					 * @return Completion or terminal stop reason.
					 */
					OperationResult StartTLS(Crypto::TLS::Session session, const OperationControl& control);

					/**
					 * @brief Write application bytes, including TLS, without restarting the budget.
					 * @param data Borrowed application bytes.
					 * @param control Total operation budget.
					 * @return Completion or terminal stop reason.
					 */
					OperationResult SendApplication(std::span<const std::byte> data, const OperationControl& control);

					/**
					 * @brief Append exactly the requested application bytes under one budget.
					 * @param size Required byte count.
					 * @param out Destination bytes.
					 * @param control Total operation budget.
					 * @return Completion or terminal stop reason; partial bytes are not replayable.
					 */
					OperationResult ReceiveApplication(StormByte::ByteSize size, StormByte::Safe::Binary& out,
						const OperationControl& control);

					/**
					 * @brief Advance TLS with at most one native read and write, never waiting.
					 * @param readable Native read readiness.
					 * @return False on authentication, deadline or transport failure.
					 */
					bool PumpTLS(bool readable) noexcept;

					/**
					 * @brief Observe successful TLS authentication (or an unconfigured channel).
					 * @return True when application I/O may proceed.
					 */
					bool TLSReady() const noexcept;

					/**
					 * @brief Observe queued ciphertext requiring writable polling.
					 * @return True when TLS protocol or application output remains pending.
					 */
					bool TLSHasOutput() const noexcept;

					/**
					 * @brief Observe pending-handshake deadline expiry.
					 * @return True after ten seconds without authentication.
					 */
					bool TLSExpired() const noexcept;

					/**
					 * @brief Observe decrypted bytes retained for incremental processing.
					 * @return True when buffered TLS application bytes may remain.
					 */
					bool TLSBuffered() const noexcept;

					/**
					 * @brief Observe immutable transport mode after channel construction.
					 * @return True when this channel requires TLS.
					 */
					bool UsingTLS() const noexcept;

					/**
					 * @brief Store the remote address captured when the socket was accepted.
					 * @param address Numeric peer address and source port.
					 */
					void SetPeerAddress(PeerAddress address) noexcept;

					/**
					 * @brief Borrow the remote address captured when the socket was accepted.
					 * @return Peer address; empty for outbound client sockets.
					 */
					const PeerAddress& RemotePeerAddress() const noexcept;

					/**
					 * @brief Construct with protocol and logger.
					 * @param protocol Address family.
					 * @param logger Logger.
					 */
					Client(const Connection::Protocol& protocol, StormByte::Safe::Shared<Logger::Log> logger);

					/**
					 * @brief Copy constructor (deleted).
					 * @param other Client that cannot be copied.
					 */
					Client(const Client& other) = delete;

					/**
					 * @brief Move constructor.
					 * @param other Client whose socket ownership is transferred.
					 */
					Client(Client&& other) noexcept;

					/**
					 * @brief Destructor.
					 */
					~Client() noexcept override;

					/**
					 * @brief Copy assignment (deleted).
					 * @param other Client that cannot be copied.
					 * @return This client (operation is deleted).
					 */
					Client& operator=(const Client& other) = delete;

					/**
					 * @brief Move assignment.
					 * @param other Client whose socket ownership is transferred.
					 * @return This client.
					 */
					Client& operator=(Client&& other) noexcept;

					/**
					 * @brief Connect to host:port.
					 * @param hostname Host name.
					 * @param port Port.
					 * @return Empty Expected on success.
					 */
					ExpectedVoid Connect(std::string_view hostname, const unsigned short& port) noexcept;

					/**
					 * @brief Resolve and connect using the constructor's strict address family.
					 * @param hostname Borrowed host name.
					 * @param port Destination port.
					 * @param resolver Shared managed DNS slot, retained by its caller.
					 * @param control One total deadline covering caller DNS wait and all attempts.
					 * @return Completion or stop reason; failure leaves this client disconnected.
					 */
					OperationResult Connect(std::string_view hostname, unsigned short port,
						Connection::Resolver& resolver, const OperationControl& control) noexcept;

					/**
					 * @brief Resolve and connect with explicit family policy.
					 * @param hostname Borrowed host name.
					 * @param port Destination port.
					 * @param resolver Shared managed DNS slot.
					 * @param control One total deadline covering DNS wait and all attempts.
					 * @param policy Explicit resolution policy.
					 * @return Completion or stop reason. Already-connected clients are preserved.
					 */
					OperationResult Connect(std::string_view hostname, unsigned short port,
						Connection::Resolver& resolver, const OperationControl& control,
						Connection::AddressPolicy policy) noexcept;

					/**
					 * @brief Attempt resolved alternatives sequentially with non-blocking connect.
					 * @param addresses Borrowed immutable native address owners, in attempt order.
					 * @param control One total deadline for all alternatives, never restarted.
					 * @return Complete only after SO_ERROR succeeds; failure closes failed handles.
					 * @details Constructor family preference remains unchanged for future calls.
					 */
					OperationResult ConnectResolved(
						std::span<const StormByte::Safe::Shared<const sockaddr>> addresses,
						const OperationControl& control) noexcept;

					/**
					 * @brief Receive up to @p size bytes (no timeout).
					 * @param size Max bytes.
					 * @return Buffer or error.
					 */
					ExpectedBuffer Receive(const StormByte::ByteSize& size = StormByte::ByteSize{0}) noexcept;

					/**
					 * @brief Receive with timeout.
					 * @param size Max bytes.
					 * @param timeout_seconds 0 = wait forever between chunks.
					 * @return Buffer or error.
					 */
					ExpectedBuffer Receive(const StormByte::ByteSize& size, const unsigned short& timeout_seconds) noexcept;

					/**
					 * @brief Receive exactly into @p out (append).
					 * @param size Required byte count.
					 * @param out Destination.
					 * @param timeout_seconds Timeout between chunks (0 = forever).
					 * @return Empty Expected on success.
					 */
					ExpectedVoid ReceiveInto(const StormByte::ByteSize& size, StormByte::Safe::Binary& out, const unsigned short& timeout_seconds = 0) noexcept;

					/**
					 * @brief Peek without consuming (MSG_PEEK).
					 * @param size Bytes to peek.
					 * @return Buffer or error.
					 */
					ExpectedBuffer Peek(const StormByte::ByteSize& size) const noexcept;

					/**
					 * @brief Send a FIFO buffer.
					 * @param buffer Data.
					 * @return Empty Expected on success.
					 */
					ExpectedVoid Send(const Buffer::FIFO& buffer) noexcept;

					/**
					 * @brief Send a byte span.
					 * @param data Data.
					 * @return Empty Expected on success.
					 */
					ExpectedVoid Send(std::span<const std::byte> data) noexcept;

					/**
					 * @brief Send bytes under one total deadline without discarding progress.
					 * @param data Borrowed bytes.
					 * @param control Absolute deadline and borrowed cancellation flag.
					 * @return Stop reason, exact bytes accepted by the OS and native error.
					 * @details Timeout/cancellation leaves the connection open. Bytes already
					 * accepted cannot be undone; do not replay the entire payload blindly.
					 */
					OperationResult Send(std::span<const std::byte> data, const OperationControl& control) noexcept;

					/**
					 * @brief Attempt a partial non-blocking read into caller storage.
					 * @param data Nonempty borrowed destination.
					 * @return Complete with data, WouldBlock, PeerClosed, or Error.
					 * @details No lifecycle state is changed on EOF; legacy read APIs are unchanged.
					 */
					OperationResult ReadSome(std::span<std::byte> data) noexcept;

					/**
					 * @brief Send Base-owned bytes through a synchronous borrowed span.
					 * @param data Source bytes; ownership is retained by the caller.
					 * @return Empty Expected on success, or a connection error.
					 */
					ExpectedVoid Send(const StormByte::Safe::Binary& data) noexcept;

					/**
					 * @brief Send from a Consumer until EoF.
					 * @param data Consumer.
					 * @return Empty Expected on success.
					 */
					ExpectedVoid Send(Buffer::Consumer data) noexcept;

					/**
					 * @brief Whether the peer requested shutdown.
					 * @return true if so.
					 */
					bool HasShutdownRequest() noexcept;

					/**
					 * @brief Lightweight connectivity check; may mark Disconnected.
					 * @return true if still up.
					 */
					bool Ping() noexcept;

				private:
					PeerAddress m_peer_address; ///< Accepted peer address; empty for outbound sockets.
					Crypto::TLS::Session m_tls; ///< Independent Crypto-owned engine.
					Safe::Mutex m_tls_mutex; ///< Serializes engine steps, never socket waits.
					bool m_tls_enabled{false}; ///< Fail-closed TLS mode.
					bool m_tls_buffered{false}; ///< Crypto may retain plaintext.
					bool m_tls_pending{false}; ///< Crypto output may need draining.
					Safe::Binary m_tls_output; ///< Unsent ciphertext, bounded to one record chunk.
					ByteSize m_tls_offset{0}; ///< Sent ciphertext prefix.
					std::chrono::steady_clock::time_point m_tls_deadline{}; ///< Pending-handshake deadline.
					friend class StormByte::Network::Detail::Session;
					friend class StormByte::Network::Detail::RemoteFile::Host;

					/**
					 * @brief Attempt one non-blocking write without waiting.
					 * @param data Source bytes.
					 * @param would_block Set when the socket needs POLLOUT/select.
					 * @return Bytes written or hard error.
					 */
					Expected<StormByte::ByteSize, ConnectionError> TryWrite(
						std::span<const std::byte> data, bool& would_block) noexcept;

					/**
					 * @brief Attempt one non-blocking read without waiting.
					 * @param would_block Set when the socket needs POLLIN/select.
					 * @param maximum Maximum plaintext bytes for a TLS read.
					 * @return Bytes read or hard error.
					 */
					Expected<StormByte::Safe::Binary, ConnectionError> TryRead(bool& would_block,
						ByteSize maximum = ByteSize{16384}) noexcept;

					/**
					 * @brief Single recv with flags.
					 * @param size Max bytes.
					 * @param flags recv flags.
					 * @return Buffer or error.
					 */
					ExpectedBuffer ReadOnce(const StormByte::ByteSize& size, int flags) noexcept;

					/**
					 * @brief Non-blocking read helper.
					 * @param buffer Destination FIFO.
					 * @return Read result.
					 */
					Connection::Read::Result ReadNonBlocking(Buffer::FIFO& buffer) noexcept;

					/**
					 * @brief Shared receive loop.
					 * @param max_size Cap.
					 * @param out Append target.
					 * @param timeout_seconds Inter-chunk timeout.
					 * @param require_exact Peer close early is error when true.
					 * @return Empty Expected on success.
					 */
					ExpectedVoid ReceiveLoop(const StormByte::ByteSize& max_size, StormByte::Safe::Binary& out, const unsigned short& timeout_seconds, bool require_exact) noexcept;

					/**
					 * @brief Low-level write.
					 * @param data Source span.
					 * @param size Bytes to write.
					 * @return Empty Expected on success.
					 */
					ExpectedVoid Write(std::span<const std::byte> data, const StormByte::ByteSize& size) noexcept;
			};
		}
	}
}

STORMBYTE_DECLARE_MAYBE_SAFE(StormByte::Network::Socket::Client);
