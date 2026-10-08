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

#include <StormByte/network/remote_file_mount.hxx>
#include <StormByte/network/typedefs.hxx>
#include <StormByte/safe/function.hxx>
#include <StormByte/safe/optional.hxx>
#include <StormByte/safe/pointers.hxx>
#include <StormByte/safe/string.hxx>
#include <StormByte/safe/thread.hxx>

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
			 * @brief Network-owned application connection, defined privately.
			 */
			class Client;
		}

		/**
		 * @namespace StormByte::Network::Detail
		 * @brief Private implementation details of the Network module.
		 */
		namespace Detail {
			/**
			 * @brief Network-owned parser session, defined privately.
			 */
			class Session;

			/**
			 * @namespace StormByte::Network::Detail::RemoteFile
			 * @brief Private remote-file implementation namespace.
			 */
			namespace RemoteFile {
				/**
				 * @class Host
				 * @brief Forward declaration of a Network-owned peer data plane.
				 */
				class Host;
			}

			/**
			 * @brief Event-loop registration retaining one parser session.
			 * @details Aggregate construction, copying, movement, assignment and
			 * destruction operate only on a Base-owned shared handle. Its concrete
			 * session is constructed and destroyed in Network through the stored
			 * provider operation. Network must remain loaded until final release.
			 */
			struct SessionRegistration final {
				Safe::Shared<Session> session;	///< Parser retained by the event loop.
			};

			/**
			 * @brief Connection retained for synchronized pipeline configuration.
			 * @details Aggregate construction, copying, movement, assignment and
			 * destruction operate only on a Base-owned shared handle. Connection
			 * construction and final destruction remain in Network through its
			 * stored provider operation, which must remain loaded until release.
			 */
			struct ConfigurableConnection final {
				Safe::Shared<Connection::Client> connection;	///< Configurable application connection.
			};

			/**
			 * @brief Peer-plane registration shared by the event loop and mounts.
			 * @details Aggregate construction, copying, movement, assignment and
			 * destruction operate only on a Base-owned shared handle. Native host
			 * resources are released by Network's stored destruction operation;
			 * that provider must remain loaded until every owner is released.
			 */
			struct RemotePlaneRegistration final {
				Safe::Shared<RemoteFile::Host> host;	///< Registered peer data plane.
			};

			/**
			 * @enum CompletionReason
			 * @brief Result category returned by a packet worker.
			 */
			enum class CompletionReason: unsigned short {
				Success, ///< Packet handling completed successfully.
				NullHandler, ///< Packet handler returned no response packet.
				Error ///< Packet handling failed.
			};

			/**
			 * @struct Completion
			 * @brief Worker response queued for application on the event loop.
			 * @details Aggregate construction, implicit copy/move, assignment and
			 * destruction use only Base-owned text and shared packet ownership,
			 * plus a scalar outcome. Packet destruction uses its stored provider
			 * operation; that provider must remain loaded until release.
			 */
			struct Completion final {
				Safe::String uuid;							///< Client session UUID.
				PacketPointer packet;						///< Application response packet.
				CompletionReason reason = CompletionReason::Error;	///< Worker handler outcome.
			};

			/**
			 * @struct WorkerTask
			 * @brief Owned request or internal operation accepted by the worker pool.
			 * @details Aggregate construction, copying, movement, assignment and
			 * destruction retain only Base-owned text, packet ownership and a
			 * provider-owned callback. Callback and packet providers must remain
			 * loaded until every queued task is released.
			 */
			struct WorkerTask final {
				Safe::String uuid;							///< Client UUID, including embedded nulls.
				PacketPointer packet;						///< Request packet.
				Safe::Optional<Safe::Function<void()>> operation;	///< Internal operation without a completion.
			};

			/**
			 * @struct WorkerThread
			 * @brief Base-owned execution retained and joined by its Network pool.
			 * @details Aggregate construction, copying, movement and assignment
			 * share a Base-allocated thread owner. The pool must join the execution
			 * before releasing its last owner; a joinable Thread cannot be destroyed.
			 * This registration applies only to this record, not Shared or Thread.
			 */
			struct WorkerThread final {
				Safe::Shared<Safe::Thread> thread;	///< Execution joined before provider destruction.
			};

			/**
			 * @struct MountedRemoteFile
			 * @brief Active file capability and its path reservation.
			 * @details Aggregate construction, implicit copy/move, assignment and
			 * destruction use Base-owned shared host ownership and text, an enum
			 * and an allocator-free fixed-width token. Native host resources are
			 * released by Network's stored provider destruction operation.
			 */
			struct MountedRemoteFile final {
				Safe::Shared<RemoteFile::Host> host; ///< Peer data plane.
				Safe::String path_key; ///< Canonical path reservation.
				RemoteFileMount::Access access; ///< Granted access mode.
				RemoteFileMount::ChannelToken token; ///< Released mount capability.
			};

			/**
			 * @enum CommandType
			 * @brief Event-loop action requested by a worker or callback.
			 */
			enum class CommandType: unsigned short {
				DisconnectClient, ///< Disconnect the specified client session.
				DisconnectAfterReply, ///< Discard input and close after reply draining.
				DisconnectAll, ///< Disconnect every client session.
				Stop ///< Stop the event loop.
			};

			/**
			 * @struct Command
			 * @brief Event-loop command and optional target client UUID.
			 * @details Aggregate construction, implicit copy/move, assignment and
			 * destruction use only a scalar action and Base-owned text.
			 */
			struct Command final {
				CommandType type; ///< Event-loop action.
				Safe::String uuid; ///< Target client UUID.
			};

			/**
			 * @class WakeupChannel
			 * @brief Network-owned native wakeup handles, defined out-of-line.
			 * @details Not MaybeSafe-certified. Native resource construction and
			 * destruction must remain in Network when the type is implemented.
			 */
			class WakeupChannel;
		}
	}
}

STORMBYTE_DECLARE_MAYBE_SAFE(StormByte::Network::Detail::SessionRegistration);

STORMBYTE_DECLARE_MAYBE_SAFE(StormByte::Network::Detail::ConfigurableConnection);

STORMBYTE_DECLARE_MAYBE_SAFE(StormByte::Network::Detail::RemotePlaneRegistration);

STORMBYTE_DECLARE_MAYBE_SAFE(StormByte::Network::Detail::Completion);

STORMBYTE_DECLARE_MAYBE_SAFE(StormByte::Network::Detail::WorkerTask);

STORMBYTE_DECLARE_MAYBE_SAFE(StormByte::Network::Detail::WorkerThread);

STORMBYTE_DECLARE_MAYBE_SAFE(StormByte::Network::Detail::MountedRemoteFile);

STORMBYTE_DECLARE_MAYBE_SAFE(StormByte::Network::Detail::Command);