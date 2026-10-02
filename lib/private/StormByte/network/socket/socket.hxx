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

#include <StormByte/expected.hxx>
#include <StormByte/logger/threaded_log.hxx>
#include <StormByte/network/connection/handler.hxx>
#include <StormByte/network/connection/info.hxx>
#include <StormByte/network/connection/protocol.hxx>
#include <StormByte/network/connection/status.hxx>
#include <StormByte/network/exception.hxx>
#include <StormByte/network/typedefs.hxx>

#include <atomic>

/**
 * @brief Socket wrappers of the Network module.
 */
namespace StormByte::Network::Socket {
	class Server;
	class Client;

	/**
	 * @class Socket
	 * @brief Platform socket: create, configure, wait, disconnect.
	 *
	 * Move-only. Owned by Client/Server (friends). m_status is atomic for concurrent Disconnect/Status.
	 */
	class STORMBYTE_NETWORK_PRIVATE Socket {
		friend class Server;
		friend class Client;
		public:
			/**
			 * @brief Copy constructor (deleted).
			 */
			Socket(const Socket& other) = delete;

			/**
			 * @brief Move constructor.
			 */
			Socket(Socket&& other) noexcept;

			/**
			 * @brief Destructor (calls Disconnect).
			 */
			virtual ~Socket() noexcept;

			/**
			 * @brief Copy assignment (deleted).
			 */
			Socket& operator=(const Socket& other) = delete;

			/**
			 * @brief Move assignment.
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
				return m_status.load(std::memory_order_acquire);
			}

			/**
			 * @brief Effective MTU.
			 * @return MTU.
			 */
			constexpr const unsigned long& MTU() const noexcept {
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
			inline const std::string& UUID() const noexcept {
				return m_UUID;
			}

				/**
				 * @brief Numeric local address selected by the connected socket.
				 * @return Local IPv4/IPv6 address, or empty when unavailable.
				 */
				std::string LocalAddress() const noexcept;

			/**
			 * @brief Wait for readable data (or peer close / timeout).
			 * @param usecs Timeout in microseconds.
			 * @return Read result or ConnectionClosed.
			 */
			ExpectedReadResult WaitForData(const long long& usecs = 0) noexcept;

		protected:
			Connection::Protocol m_protocol;					///< Protocol
			std::atomic<Connection::Status> m_status;			///< Status
			Connection::HandlerType m_handle;					///< Native handle
			std::unique_ptr<Connection::Info> m_conn_info;		///< Peer info
			unsigned long m_mtu;								///< MTU
			mutable StormByte::Safe::Shared<Logger::Log> m_logger;		///< Logger

			int m_effective_send_buf = 65536;	///< SO_SNDBUF effective
			int m_effective_recv_buf = 65536;	///< SO_RCVBUF effective

			/**
			 * @brief Construct with protocol and logger.
			 * @param protocol Address family.
			 * @param logger Logger.
			 */
			Socket(const Connection::Protocol& protocol, StormByte::Safe::Shared<Logger::Log> logger) noexcept;

			/**
			 * @brief Create the OS socket.
			 * @return Handle or ConnectionError.
			 */
			Expected<Connection::HandlerType, ConnectionError> CreateSocket() noexcept;

			/**
			 * @brief Post-connect options: non-blocking, buffers, TCP_NODELAY, MTU.
			 */
			void InitializeAfterConnect() noexcept;

			/**
			 * @brief Ensure the handle is closed.
			 */
			void EnsureIsClosed() noexcept;

		private:
			constexpr static const unsigned short DEFAULT_MTU = 1500;	///< Fallback MTU
			std::string m_UUID;	///< Instance UUID

			/**
			 * @brief Path MTU or DEFAULT_MTU.
			 * @return MTU.
			 */
			int GetMTU() const noexcept;

			/**
			 * @brief Set non-blocking mode.
			 */
			void SetNonBlocking() noexcept;
	};
}
