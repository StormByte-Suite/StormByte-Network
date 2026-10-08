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

#include <StormByte/network/visibility.h>
#include <StormByte/safe/binary.hxx>
#include <StormByte/serializable.hxx>

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

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
		 * @class RemoteFileMount
		 * @brief Public, validated descriptor for attaching to one remote file channel.
		 *
		 * Decode this value from the application response with
		 * @ref StormByte::Serializable. It contains no server filesystem path.
		 */
		class STORMBYTE_NETWORK_PUBLIC RemoteFileMount final {
			public:
				/**
				 * @brief Fixed-width opaque channel capability.
				 */
				using ChannelToken = std::array<std::byte, 32>;

				/**
				 * @enum Status
				 * @brief Outcome of the application-authorized mount request.
				 */
				enum class Status: std::uint8_t {
					/**
					 * @brief Channel was authorized and mounted.
					 */
					Authorized = 1,

					/**
					 * @brief Application policy denied the request.
					 */
					NotAuthorized = 2,

					/**
					 * @brief Requested read file does not exist.
					 */
					Unavailable = 3,

					/**
					 * @brief Writer rejected because one or more readers are mounted.
					 */
					FileBeingRead = 4,

					/**
					 * @brief Reader/writer rejected because an exclusive writer is mounted.
					 */
					FileBeingWritten = 5,

					/**
					 * @brief Mount failed for a reason other than authorization or path conflict.
					 */
					Failed = 6
				};

				/**
				 * @enum Access
				 * @brief Permitted direction for this mounted channel.
				 */
				enum class Access: std::uint8_t {
					/**
					 * @brief No channel capability is present.
					 */
					None = 0,

					/**
					 * @brief Client may read from the mounted file.
					 */
					Read = 1,

					/**
					 * @brief Client may write to the mounted file.
					 */
					Write = 2
				};

				/**
				 * @brief Construct an application-level authorization denial.
				 * @return Descriptor without channel credentials.
				 */
				static RemoteFileMount NotAuthorized() noexcept;

				/**
				 * @brief Construct a missing-file result without channel credentials.
				 * @return Unavailable mount descriptor.
				 */
				static RemoteFileMount Unavailable() noexcept;

				/**
				 * @brief Construct a result for a write blocked by mounted readers.
				 * @return Descriptor reporting a reader conflict.
				 */
				static RemoteFileMount FileBeingRead() noexcept;

				/**
				 * @brief Construct a result for a mount blocked by an exclusive writer.
				 * @return Descriptor reporting a writer conflict.
				 */
				static RemoteFileMount FileBeingWritten() noexcept;

				/**
				 * @brief Construct a non-authorization mount failure.
				 * @return Failed mount descriptor without channel credentials.
				 */
				static RemoteFileMount Failed() noexcept;

				/**
				 * @brief Copy a validated mount descriptor.
				 * @param other Descriptor to copy.
				 */
				RemoteFileMount(const RemoteFileMount& other);

				/**
				 * @brief Move a validated mount descriptor.
				 * @param other Descriptor to move; fixed-width fields remain valid.
				 */
				RemoteFileMount(RemoteFileMount&& other) noexcept;

				/**
				 * @brief Copy-assign a validated mount descriptor.
				 * @param other Descriptor to copy.
				 * @return This descriptor.
				 */
				RemoteFileMount& operator=(const RemoteFileMount& other);

				/**
				 * @brief Move-assign a validated mount descriptor.
				 * @param other Descriptor to move; fixed-width fields remain valid.
				 * @return This descriptor.
				 */
				RemoteFileMount& operator=(RemoteFileMount&& other) noexcept;

				/**
				 * @brief Out-of-line destructor anchors the non-trivial codec type.
				 */
				~RemoteFileMount() noexcept;

				/**
				 * @brief Session data-plane port shared by every Authorized mount from one Client.
				 * @return Shared port, or zero when rejected.
				 */
				std::uint16_t Port() const noexcept;

				/**
				 * @brief Maximum idle interval for the Client's peer data-plane heartbeat.
				 * @return Maximum idle interval in seconds.
				 */
				std::uint16_t MaximumTimeoutSeconds() const noexcept;

				/**
				 * @brief Mount request result.
				 * @return Result of the mount request.
				 */
				Status Result() const noexcept;

				/**
				 * @brief Authorized access mode.
				 * @return Permitted channel direction.
				 */
				Access Mode() const noexcept;

				/**
				 * @brief Opaque capability checked by the private channel protocol.
				 * @return Reference to the channel token.
				 */
				const ChannelToken& Token() const noexcept;

			private:
				/**
				 * @brief Construct a descriptor from validated mount fields.
				 * @param status Mount request result.
				 * @param token Opaque channel capability.
				 * @param port Session data-plane port.
				 * @param maximum_timeout_seconds Maximum idle interval in seconds.
				 * @param access Permitted channel direction.
				 */
				RemoteFileMount(Status status, ChannelToken token, std::uint16_t port,
					std::uint16_t maximum_timeout_seconds, Access access) noexcept;

				Status m_status;						///< Mount request result.
				ChannelToken m_token;				///< Opaque channel capability.
				std::uint16_t m_port;				///< Session data-plane port.
				std::uint16_t m_maximum_timeout_seconds;	///< Maximum peer idle interval in seconds.
				Access m_access;						///< Permitted channel direction.

				/**
				 * @brief Server may construct authorized mount descriptors.
				 */
				friend class Server;

				/**
				 * @brief Wire codec may access validated descriptor fields.
				 */
				friend struct StormByte::Detail::Codec<RemoteFileMount>;
		};
	}

	/**
	 * @namespace StormByte::Detail
	 * @brief Serialization implementation details of the StormByte suite.
	 */
	namespace Detail {
		/**
		 * @brief Versioned wire codec for @ref StormByte::Network::RemoteFileMount.
		 */
		template<>
		struct Codec<Network::RemoteFileMount> {
			/**
			 * @brief Exact serialized size of a mount descriptor.
			 * @param data Descriptor to measure.
			 * @return Serialized byte count.
			 */
			static STORMBYTE_NETWORK_PUBLIC ByteSize Size(const Network::RemoteFileMount& data) noexcept;

			/**
			 * @brief Encode a mount descriptor field-by-field.
			 * @param data Descriptor to encode.
			 * @return Encoded descriptor bytes owned by Base.
			 */
			static STORMBYTE_NETWORK_PUBLIC Safe::Binary Write(const Network::RemoteFileMount& data) noexcept;

			/**
			 * @brief Decode and validate a complete mount descriptor.
			 * @param data Input bytes; trailing bytes are rejected.
			 * @return Valid descriptor or a deserialization error.
			 */
			static STORMBYTE_NETWORK_PUBLIC Expected<Network::RemoteFileMount, DeserializeError> Read(
				std::span<const std::byte> data) noexcept;
		};
	}
}

STORMBYTE_DECLARE_MAYBE_SAFE(StormByte::Network::RemoteFileMount);
