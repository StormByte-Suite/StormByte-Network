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

#include <StormByte/network/socket/client.hxx>
#include <StormByte/network/typedefs.hxx>
#include <StormByte/safe/vector.hxx>

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
		 * @namespace StormByte::Network::Socket
		 * @brief Socket namespace.
		 */
		namespace Socket {
			/**
			 * @class Server
			 * @brief Listening socket: bind, listen, accept.
				 * @details Safe accepted-client owners retain provider release operations.
				 * Network, Base and logger providers must remain loaded until release.
				 * Serialize accept, moves and lifecycle operations externally. Destruction
				 * disconnects accepted clients even when another Safe handle retains them.
			 */
			class STORMBYTE_NETWORK_PRIVATE Server final: public Socket {
				public:
					/**
					 * @brief Construct with protocol and logger.
					 * @param protocol Address family.
					 * @param logger Logger.
					 */
					Server(const Connection::Protocol& protocol, StormByte::Safe::Shared<Logger::Log> logger);

					/**
					 * @brief Copy constructor (deleted).
					 * @param other Server that cannot be copied.
					 */
					Server(const Server& other) = delete;

					/**
					 * @brief Transfer Safe owners and native socket state inside Network.
					 * @param other Source server.
					 */
					Server(Server&& other) noexcept;

					/**
					 * @brief Release accepted-client owners and native socket state inside Network.
					 */
					~Server() noexcept override;

					/**
					 * @brief Copy assignment (deleted).
					 * @param other Server that cannot be copied.
					 * @return This server (operation is deleted).
					 */
					Server& operator=(const Server& other) = delete;

					/**
					 * @brief Move-assign Safe owners and native socket state inside Network.
					 * @param other Source server.
					 * @return This server.
					 */
					Server& operator=(Server&& other) noexcept;

					/**
					 * @brief Bind and listen on host:port.
					 * @param hostname Bind address.
					 * @param port Port.
					 * @return Empty Expected on success.
					 */
					ExpectedVoid Listen(std::string_view hostname, const unsigned short& port) noexcept;

					/**
					 * @brief Port assigned to the listener, including an ephemeral port.
					 * @return Bound port, or 0 when not listening.
					 */
					unsigned short Port() const noexcept;

					/**
					 * @brief Accept one client allocated through the Network provider.
					 * @return Safe shared Client or error.
					 */
					ExpectedClient Accept() noexcept;

					/**
					 * @brief Disconnect all accepted clients then the listener.
					 */
					void Disconnect() noexcept override;

					/**
					 * @brief Disconnect one accepted client by UUID.
					 * @param client_uuid Client UUID.
					 */
					void DisconnectClient(std::string_view client_uuid) noexcept;

				private:
					StormByte::Safe::Vector<StormByte::Safe::Shared<Client>> m_active_clients;	///< Safe owners of accepted clients.
					unsigned short m_port = 0;											///< Bound listener port.
			};
		}
	}
}

STORMBYTE_DECLARE_MAYBE_SAFE(StormByte::Network::Socket::Server);
