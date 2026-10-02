#pragma once

#include <StormByte/buffer/io/buffered_location_reader.hxx>
#include <StormByte/buffer/io/buffered_location_writer.hxx>
#include <StormByte/network/remote_file_mount.hxx>
#include <StormByte/safe/pointers.hxx>
#include <StormByte/safe/string.hxx>

#include <memory>

/**
 * @brief Root namespace of the StormByte C++ suite.
 */
namespace StormByte {
	/**
	 * @brief Network module of the StormByte suite.
	 */
	namespace Network {
		namespace Detail::RemoteFile {
			class DataPlane; ///< Peer-scoped multiplexed request transport.
		}

		/**
		 * @class BufferedRemoteFileReader
		 * @brief File-like buffered reader backed by a peer DataPlane and mount token.
		 *
		 * The peer-scoped data plane owns the socket and heartbeat; each leaf has
		 * one capability and an independent logical file offset.
		 */
		class STORMBYTE_NETWORK_PUBLIC BufferedRemoteFileReader final:
			public Buffer::IO::BufferedLocationReader {
			public:
				/** @brief Copy construction is disabled. */
				BufferedRemoteFileReader(const BufferedRemoteFileReader&) = delete;

				/** @brief Move construction is disabled while a mount token is registered. */
				BufferedRemoteFileReader(BufferedRemoteFileReader&&) = delete;

				/** @brief Copy assignment is disabled. */
				BufferedRemoteFileReader& operator=(const BufferedRemoteFileReader&) = delete;

				/** @brief Move assignment is disabled while a mount token is registered. */
				BufferedRemoteFileReader& operator=(BufferedRemoteFileReader&&) = delete;

				/** @brief Release this leaf's token before base destruction; the DataPlane owns the heartbeat. */
				~BufferedRemoteFileReader() noexcept override;

			protected:
				/** @brief Network-interface snapshot for this connected origin. */
				StormByte::Safe::Shared<StormByte::System::Device> OriginDevice() const override;

				/** @brief Accept the network device without probing its accessor as a filesystem path. */
				bool OriginDeviceUsable(const StormByte::Safe::Shared<StormByte::System::Device>& device) const noexcept override;

				/** @brief Open the token's server-side reader handle. */
				Buffer::IO::Result OriginOpen() override;

				/** @brief Release this reader token without closing the peer plane. */
				Buffer::IO::Result OriginClose() override;

				/** @brief Pull bytes at this leaf's absolute logical offset. */
				Buffer::IO::Result OriginPull(StormByte::ByteSize count, Buffer::FIFO& destination) override;

				/** @brief Move only this Buffer leaf's logical cursor. */
				Buffer::IO::Result OriginSeek(std::ptrdiff_t offset, Buffer::Position mode) override;

				/** @brief Query the server-side file length. */
				std::optional<StormByte::ByteSize> OriginSize() const noexcept override;

			private:
				BufferedRemoteFileReader(StormByte::Safe::String locator,
					std::weak_ptr<Detail::RemoteFile::DataPlane> plane, RemoteFileMount::ChannelToken token);

				void MarkFailed() noexcept;
				bool ReleaseToken() noexcept;

				std::weak_ptr<Detail::RemoteFile::DataPlane> m_plane; ///< Client-owned peer data plane.
				RemoteFileMount::ChannelToken m_token{}; ///< Capability assigned to this reader.
				StormByte::ByteSize m_offset{0}; ///< Absolute logical read cursor.
				bool m_token_released{false}; ///< Whether CloseToken was already sent.

				friend class Client;
		};

		/**
		 * @class BufferedRemoteFileWriter
		 * @brief File-like buffered writer with an exclusive server-side mount.
		 */
		class STORMBYTE_NETWORK_PUBLIC BufferedRemoteFileWriter final:
			public Buffer::IO::BufferedLocationWriter {
			public:
				/** @brief Copy construction is disabled. */
				BufferedRemoteFileWriter(const BufferedRemoteFileWriter&) = delete;

				/** @brief Move construction is disabled while a mount token is registered. */
				BufferedRemoteFileWriter(BufferedRemoteFileWriter&&) = delete;

				/** @brief Copy assignment is disabled. */
				BufferedRemoteFileWriter& operator=(const BufferedRemoteFileWriter&) = delete;

				/** @brief Move assignment is disabled while a mount token is registered. */
				BufferedRemoteFileWriter& operator=(BufferedRemoteFileWriter&&) = delete;

				/** @brief Release this leaf's token before base destruction; the DataPlane owns the heartbeat. */
				~BufferedRemoteFileWriter() noexcept override;

			protected:
				/** @brief Network-interface snapshot for this connected origin. */
				StormByte::Safe::Shared<StormByte::System::Device> OriginDevice() const override;

				/** @brief Accept the network device without probing its accessor as a filesystem path. */
				bool OriginDeviceUsable(const StormByte::Safe::Shared<StormByte::System::Device>& device) const noexcept override;

				/** @brief Open this writer token on the peer plane. */
				Buffer::IO::Result OriginOpen() override;

				/** @brief Release this writer token without closing the peer plane. */
				Buffer::IO::Result OriginClose() override;

				/** @brief Write a complete byte span at this leaf's absolute logical offset. */
				Buffer::IO::Result OriginPush(std::span<const std::byte> data) override;

				/** @brief Flush the server-side writer handle. */
				Buffer::IO::Result OriginFlush() override;

				/** @brief Truncate the server-side file to zero bytes; this API does not accept a target size. */
				Buffer::IO::Result OriginTruncate() override;

				/** @brief Set this Buffer leaf's local writer offset. */
				Buffer::IO::Result OriginSeek(StormByte::ByteSize absolute) override;

				/** @brief Query the server-side file length. */
				StormByte::ByteSize OriginSize() const noexcept override;

			private:
				BufferedRemoteFileWriter(StormByte::Safe::String locator,
					std::weak_ptr<Detail::RemoteFile::DataPlane> plane, RemoteFileMount::ChannelToken token);

				void MarkFailed() noexcept;
				bool ReleaseToken() noexcept;

				std::weak_ptr<Detail::RemoteFile::DataPlane> m_plane; ///< Client-owned peer data plane.
				RemoteFileMount::ChannelToken m_token{}; ///< Capability assigned to this writer.
				StormByte::ByteSize m_offset{0}; ///< Absolute logical write cursor.
				bool m_token_released{false}; ///< Whether CloseToken was already sent.

				friend class Client;
		};

		/**
		 * @struct RemoteFileReaderDeleter
		 * @brief Deletes the Network-allocated reader inside the Network module.
		 */
		struct STORMBYTE_NETWORK_PUBLIC RemoteFileReaderDeleter {
			/** @brief Destroy one reader in the allocating module. */
			void operator()(BufferedRemoteFileReader* reader) const noexcept;
		};

		/** @brief Caller-owned handle for a Network-allocated remote reader. */
		using RemoteFileReaderHandle = std::unique_ptr<BufferedRemoteFileReader, RemoteFileReaderDeleter>;

		/**
		 * @struct RemoteFileWriterDeleter
		 * @brief Deletes the Network-allocated writer inside the Network module.
		 */
		struct STORMBYTE_NETWORK_PUBLIC RemoteFileWriterDeleter {
			/** @brief Destroy one writer in the allocating module. */
			void operator()(BufferedRemoteFileWriter* writer) const noexcept;
		};

		/** @brief Caller-owned handle for a Network-allocated remote writer. */
		using RemoteFileWriterHandle = std::unique_ptr<BufferedRemoteFileWriter, RemoteFileWriterDeleter>;
	}
}