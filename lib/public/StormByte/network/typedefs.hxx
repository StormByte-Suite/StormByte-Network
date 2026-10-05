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

#include <StormByte/buffer/fifo.hxx>
#include <StormByte/buffer/consumer.hxx>
#include <StormByte/expected.hxx>
#include <StormByte/logger/log.hxx>
#include <StormByte/network/connection/protocol.hxx>
#include <StormByte/network/connection/rw.hxx>
#include <StormByte/network/connection/status.hxx>
#include <StormByte/network/exception.hxx>
#include <StormByte/network/transport/packet.hxx>
#include <StormByte/safe/pointers.hxx>
#include <StormByte/type_traits.hxx>

#ifdef WINDOWS
#include <winsock2.h>
#endif

#include <memory>
#include <utility>

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
			 * @class Socket
			 * @brief Forward declaration of the socket abstraction.
			 */
			class Socket;

			/**
			 * @class Client
			 * @brief Forward declaration of the client socket.
			 */
			class Client;
		}

		/**
		 * @namespace StormByte::Network::Connection
		 * @brief Connection namespace.
		 */
		namespace Connection {
			#ifdef UNIX
				/**
				 * @brief Native POSIX socket handle.
				 */
				using HandlerType = int;
			#else
				/**
				 * @brief Native Winsock socket handle.
				 */
				using HandlerType = SOCKET;
			#endif
		}

		/**
		 * @brief Receive buffer or connection error result.
		 */
		using ExpectedBuffer = StormByte::Expected<Buffer::FIFO, ConnectionError>;

		/**
		 * @brief Void operation or connection error result.
		 */
		using ExpectedVoid = StormByte::Expected<void, ConnectionError>;

		/**
		 * @brief Accepted client or connection error result.
		 */
		using ExpectedClient = StormByte::Expected<std::shared_ptr<Socket::Client>, ConnectionError>;

		/**
		 * @brief Wait-for-data result or closed connection error.
		 */
		using ExpectedReadResult = StormByte::Expected<Connection::Read::Result, ConnectionClosed>;

		/**
		 * @brief Base-heap shared packet pointer.
		 *
		 * Construct derived packets with MakePointer.
		 */
		using PacketPointer = StormByte::Safe::Shared<Transport::Packet>;

		/**
		 * @class DeserializePacketFunction
		 * @brief Caller-owned callback that builds a packet from opcode and payload.
		 *
		 * The context is allocated, cloned, invoked, and destroyed by function
		 * pointers instantiated in the caller's translation unit. The Network DLL
		 * stores only the context pointer and those trampolines; it never allocates
		 * or frees the callback target across the CRT boundary.
		 */
		class DeserializePacketFunction {
			public:
				/**
				 * @brief Wire opcode type.
				 */
				using OpcodeType = Transport::Packet::OpcodeType;

				/**
				 * @brief Invocation trampoline taking context, opcode, payload, and logger.
				 * @return Decoded packet, or an empty pointer.
				 */
				using InvokeFunction = PacketPointer (*)(void*, OpcodeType, Buffer::Consumer, StormByte::Safe::Shared<Logger::Log>);

				/**
				 * @brief Caller-allocator clone trampoline taking source context storage.
				 * @return Cloned context storage allocated in the caller's module.
				 */
				using CloneFunction = void* (*)(const void*);

				/**
				 * @brief Caller-allocator destroy trampoline taking context storage.
				 */
				using DestroyFunction = void (*)(void*) noexcept;

				/**
				 * @brief Construct an empty callback.
				 */
				DeserializePacketFunction() noexcept = default;

				/**
				 * @brief Own a caller-allocated callback target.
				 * @tparam Callable Copy-constructible callback type.
				 * @param callable Callback taking opcode, payload Consumer, and Shared logger.
				 */
				template<typename Callable>
				requires (!StormByte::Type::SameAs<std::decay_t<Callable>, DeserializePacketFunction>
					&& StormByte::Type::CopyConstructible<std::decay_t<Callable>>)
				STORMBYTE_FORCE_INLINE DeserializePacketFunction(Callable&& callable):
					m_context(new std::decay_t<Callable>(std::forward<Callable>(callable))),
					m_invoke(&Invoke<std::decay_t<Callable>>),
					m_clone(&Clone<std::decay_t<Callable>>),
					m_destroy(&Destroy<std::decay_t<Callable>>) {}

				/**
				 * @brief Clone using the caller's target allocator trampoline.
				 * @param other Source callback.
				 */
				STORMBYTE_NETWORK_PUBLIC DeserializePacketFunction(const DeserializePacketFunction& other);

				/**
				 * @brief Transfer callback ownership without touching its target.
				 * @param other Source callback.
				 */
				STORMBYTE_NETWORK_PUBLIC DeserializePacketFunction(DeserializePacketFunction&& other) noexcept;

				/**
				 * @brief Destroy through the caller's destruction trampoline.
				 */
				STORMBYTE_NETWORK_PUBLIC ~DeserializePacketFunction() noexcept;

				/**
				 * @brief Clone-assign using the caller's target allocator trampoline.
				 * @param other Source callback.
				 * @return This callback.
				 */
				STORMBYTE_NETWORK_PUBLIC DeserializePacketFunction& operator=(const DeserializePacketFunction& other);

				/**
				 * @brief Transfer-assign callback ownership.
				 * @param other Source callback.
				 * @return This callback.
				 */
				STORMBYTE_NETWORK_PUBLIC DeserializePacketFunction& operator=(DeserializePacketFunction&& other) noexcept;

				/**
				 * @brief Invoke the packet decoder.
				 * @param opcode Packet opcode.
				 * @param payload Payload consumer.
				 * @param logger Shared diagnostic logger.
				 * @return Decoded packet, or empty pointer.
				 */
				STORMBYTE_NETWORK_PUBLIC PacketPointer operator()(OpcodeType opcode, Buffer::Consumer payload,
					StormByte::Safe::Shared<Logger::Log> logger) const;

				/**
				 * @brief Test whether a callable target is present.
				 * @return True if a callable target is present, false otherwise.
				 */
				explicit operator bool() const noexcept {
					return m_context != nullptr;
				}

			private:
				/**
				 * @brief Invoke @p context's callback in the caller's module.
				 * @tparam Callable Concrete callback type.
				 * @param context Caller-owned callback storage.
				 * @param opcode Packet opcode.
				 * @param payload Payload consumer.
				 * @param logger Shared logger.
				 * @return Decoded packet, or an empty pointer.
				 */
				template<typename Callable>
				static PacketPointer Invoke(void* context, OpcodeType opcode, Buffer::Consumer payload,
					StormByte::Safe::Shared<Logger::Log> logger) {
					return (*static_cast<Callable*>(context))(opcode, std::move(payload), std::move(logger));
				}

				/**
				 * @brief Clone @p context using the caller's allocator.
				 * @tparam Callable Concrete callback type.
				 * @param context Caller-owned callback storage.
				 * @return Cloned storage allocated by the caller module.
				 */
				template<typename Callable>
				static void* Clone(const void* context) {
					return new Callable(*static_cast<const Callable*>(context));
				}

				/**
				 * @brief Destroy @p context using the caller's allocator.
				 * @tparam Callable Concrete callback type.
				 * @param context Caller-owned callback storage.
				 */
				template<typename Callable>
				static void Destroy(void* context) noexcept {
					delete static_cast<Callable*>(context);
				}

				/**
				 * @brief Target owned and destroyed by caller trampolines.
				 */
				void* m_context = nullptr;

				/**
				 * @brief Caller-compiled invocation trampoline.
				 */
				InvokeFunction m_invoke = nullptr;

				/**
				 * @brief Caller-compiled clone trampoline.
				 */
				CloneFunction m_clone = nullptr;

				/**
				 * @brief Caller-compiled destruction trampoline.
				 */
				DestroyFunction m_destroy = nullptr;
		};
	}
}

/**
 * @brief Declare conditional cross-module safety for packet decoder callbacks.
 *
 * Callback contexts are cloned and destroyed in their provider module. The
 * provider module and a compatible ABI must remain available until all callback
 * copies are destroyed.
 */
STORMBYTE_DECLARE_MAYBE_SAFE(StormByte::Network::DeserializePacketFunction);
