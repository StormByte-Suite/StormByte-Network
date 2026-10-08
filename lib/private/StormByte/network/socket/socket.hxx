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

#include <StormByte/byte_size.hxx>
#include <StormByte/expected.hxx>
#include <StormByte/logger/threaded_log.hxx>
#include <StormByte/network/connection/handler.hxx>
#include <StormByte/network/connection/info.hxx>
#include <StormByte/network/connection/protocol.hxx>
#include <StormByte/network/connection/status.hxx>
#include <StormByte/network/exception.hxx>
#include <StormByte/network/typedefs.hxx>
#include <StormByte/safe/atomic.hxx>
#include <StormByte/safe/pointers.hxx>
#include <StormByte/safe/string.hxx>

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
		 * @namespace StormByte::Network::Socket
		 * @brief Socket namespace.
		 */
		namespace Socket {
			/**
			 * @class Server
			 * @brief Forward declaration of the listening socket.
			 */
			class Server;

			/**
			 * @class Client
			 * @brief Forward declaration of the connected socket.
			 */
			class Client;

			/**
			 * @class Socket
			 * @brief Platform socket: create, configure, wait, disconnect.
			 *
			 * Move-only. Owned by Client/Server (friends). m_status is atomic for concurrent Disconnect/Status.
				 * Safe owners retain provider allocation and release operations; the only
				 * raw owned resource is the native OS socket handle. Network and all owner
				 * providers must remain loaded until destruction. Serialize moves and I/O
				 * against lifecycle operations; Status may be queried concurrently.
			 */
			class STORMBYTE_NETWORK_PRIVATE Socket {
				friend class Server;
				friend class Client;
				public:
					/**
					 * @brief Copy constructor (deleted).
					 * @param other Socket that cannot be copied.
					 */
					Socket(const Socket& other) = delete;

					/**
						 * @brief Transfer Safe owners and the native handle inside Network.
					 * @param other Socket whose state is transferred.
					 */
					Socket(Socket&& other) noexcept;

					/**
						 * @brief Close the native handle and release Safe owners inside Network.
					 */
					virtual ~Socket() noexcept;

					/**
					 * @brief Copy assignment (deleted).
					 * @param other Socket that cannot be copied.
					 * @return This socket (operation is deleted).
					 */
					Socket& operator=(const Socket& other) = delete;

					/**
						 * @brief Close this handle before transferring owners inside Network.
					 * @param other Socket whose state is transferred.
					 * @return This socket.
					 */
					Socket& operator=(Socket&& other) noexcept;

					/**
					 * @brief Graceful shutdown and close (idempotent).
					 */
					virtual void Disconnect() noexcept;

					/**
					 * @brief Current connection status.
					 * @return Status.
					 */
					Connection::Status Status() const noexcept {
						return m_status.load(StormByte::Safe::MemoryOrder::Acquire);
					}

					/**
					 * @brief Effective MTU.
					 * @return MTU.
					 */
					constexpr const StormByte::ByteSize& MTU() const noexcept {
						return m_mtu;
					}

					/**
					 * @brief Native handle.
					 * @return Handle.
					 */
					inline const Connection::HandlerType& Handle() const noexcept {
						return m_handle;
					}

					/**
					 * @brief Socket UUID.
					 * @return UUID.
					 */
					const StormByte::Safe::String& UUID() const noexcept {
						return m_UUID;
					}

					/**
					 * @brief Numeric local address selected by the connected socket.
					 * @return Base-owned local IPv4/IPv6 address, or empty when unavailable.
					 */
					StormByte::Safe::String LocalAddress() const noexcept;

					/**
					 * @brief Wait for readable data (or peer close / timeout).
					 * @param usecs Timeout in microseconds.
					 * @return Read result or ConnectionClosed.
					 */
					ExpectedReadResult WaitForData(const long long& usecs = 0) noexcept;

				protected:
					Connection::Protocol m_protocol;						///< Protocol
					StormByte::Safe::Atomic<Connection::Status> m_status;	///< Status
					Connection::HandlerType m_handle;						///< Native handle
					StormByte::Safe::Unique<Connection::Info> m_conn_info;	///< Peer info
					StormByte::ByteSize m_mtu;								///< MTU in bytes
					mutable StormByte::Safe::Shared<Logger::Log> m_logger;	///< Logger

					StormByte::ByteSize m_effective_send_buf{65536};	///< Effective SO_SNDBUF bytes
					StormByte::ByteSize m_effective_recv_buf{65536};	///< Effective SO_RCVBUF bytes

					/**
					 * @brief Construct with protocol and logger.
					 * @param protocol Address family.
					 * @param logger Logger.
					 */
					Socket(const Connection::Protocol& protocol, StormByte::Safe::Shared<Logger::Log> logger);

					/**
					 * @brief Create the OS socket.
					 * @return Handle or ConnectionError.
					 */
					Expected<Connection::HandlerType, ConnectionError> CreateSocket() noexcept;

					/**
					 * @brief Post-connect options: non-blocking, buffers, TCP_NODELAY, MTU.
					 */
					void InitializeAfterConnect();

					/**
					 * @brief Ensure the handle is closed.
					 */
					void EnsureIsClosed() noexcept;

					/**
					 * @brief Check the platform's invalid-handle sentinel, allowing handle zero.
					 * @return True when this socket owns a native handle.
					 */
					bool HasHandle() const noexcept;

				private:
					constexpr static const StormByte::ByteSize DEFAULT_MTU{1500};	///< Fallback MTU bytes
					StormByte::Safe::String m_UUID;	///< Base-owned instance UUID

					/**
					 * @brief Path MTU or DEFAULT_MTU.
					 * @return MTU.
					 */
					StormByte::ByteSize GetMTU() const noexcept;

					/**
					 * @brief Set non-blocking mode.
					 */
					void SetNonBlocking() noexcept;
			};
		}
	}
}

STORMBYTE_DECLARE_MAYBE_SAFE(StormByte::Network::Socket::Socket);
