#pragma once

#include <StormByte/network/visibility.h>
#include <StormByte/serializable.hxx>

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

/**
 * @brief Root namespace of the StormByte suite.
 */
namespace StormByte {
	/**
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
				/** @brief Fixed-width opaque channel capability. */
				using ChannelToken = std::array<std::byte, 32>;

				/**
				 * @enum Status
				 * @brief Outcome of the application-authorized mount request.
				 */
				enum class Status: std::uint8_t {
					Authorized = 1, ///< Channel was authorized and mounted.
					NotAuthorized = 2, ///< Application policy denied the request.
					Unavailable = 3, ///< Requested read file does not exist.
					FileBeingRead = 4, ///< Writer rejected because one or more readers are mounted.
					FileBeingWritten = 5, ///< Reader/writer rejected because an exclusive writer is mounted.
					Failed = 6 ///< Mount failed for a reason other than authorization or path conflict.
				};

				/**
				 * @enum Access
				 * @brief Permitted direction for this mounted channel.
				 */
				enum class Access: std::uint8_t {
					None = 0, ///< No channel capability is present.
					Read = 1, ///< Client may read from the mounted file.
					Write = 2 ///< Client may write to the mounted file.
				};

				/** @brief Construct an application-level authorization denial. */
				static RemoteFileMount NotAuthorized() noexcept;

				/** @brief Construct a missing-file result without channel credentials. */
				static RemoteFileMount Unavailable() noexcept;

				/** @brief Construct a result for a write blocked by mounted readers. */
				static RemoteFileMount FileBeingRead() noexcept;

				/** @brief Construct a result for a mount blocked by an exclusive writer. */
				static RemoteFileMount FileBeingWritten() noexcept;

				/** @brief Construct a non-authorization mount failure. */
				static RemoteFileMount Failed() noexcept;

				/** @brief Copy a validated mount descriptor. */
				RemoteFileMount(const RemoteFileMount&) = default;

				/** @brief Move a validated mount descriptor. */
				RemoteFileMount(RemoteFileMount&&) noexcept = default;

				/** @brief Copy-assign a validated mount descriptor. */
				RemoteFileMount& operator=(const RemoteFileMount&) = default;

				/** @brief Move-assign a validated mount descriptor. */
				RemoteFileMount& operator=(RemoteFileMount&&) noexcept = default;

				/** @brief Out-of-line destructor anchors the non-trivial codec type. */
				~RemoteFileMount() noexcept;

				/** @brief Session data-plane port shared by every Authorized mount from one Client; zero when rejected. */
				std::uint16_t Port() const noexcept;

				/** @brief Maximum idle interval for the Client's peer data-plane heartbeat. */
				std::uint16_t MaximumTimeoutSeconds() const noexcept;

				/** @brief Mount request result. */
				Status Result() const noexcept;

				/** @brief Authorized access mode. */
				Access Mode() const noexcept;

				/** @brief Opaque capability checked by the private channel protocol. */
				const ChannelToken& Token() const noexcept;

			private:
				RemoteFileMount(Status status, ChannelToken token, std::uint16_t port,
					std::uint16_t maximum_timeout_seconds, Access access) noexcept;

				Status m_status;
				ChannelToken m_token;
				std::uint16_t m_port;
				std::uint16_t m_maximum_timeout_seconds;
				Access m_access;

				friend class Server;
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
			/** @brief Exact serialized size of a mount descriptor. */
			static STORMBYTE_NETWORK_PUBLIC ByteSize Size(const Network::RemoteFileMount& data) noexcept;

			/** @brief Encode a mount descriptor field-by-field. */
			static STORMBYTE_NETWORK_PUBLIC BinaryData Write(const Network::RemoteFileMount& data) noexcept;

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