#pragma once

#include <StormByte/buffer/io/buffered_location_reader.hxx>
#include <StormByte/buffer/io/buffered_location_writer.hxx>
#include <StormByte/network/remote_file_mount.hxx>
#include <StormByte/safe/optional.hxx>
#include <StormByte/safe/pointers.hxx>
#include <StormByte/safe/string.hxx>

#include <memory>

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
		 * @namespace StormByte::Network::Detail::RemoteFile
		 * @brief Private remote-file implementation namespace.
		 */
		namespace Detail::RemoteFile {
			/**
			 * @class DataPlane
			 * @brief Peer-scoped multiplexed request transport.
			 */
			class DataPlane;
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
				/**
				 * @brief Copy construction is disabled.
				 * @par Parameters
				 * The unnamed parameter is the reader that cannot be copied.
				 */
				BufferedRemoteFileReader(const BufferedRemoteFileReader&) = delete;

				/**
				 * @brief Move construction is disabled while a mount token is registered.
				 * @par Parameters
				 * The unnamed parameter is the reader that cannot be moved.
				 */
				BufferedRemoteFileReader(BufferedRemoteFileReader&&) = delete;

				/**
				 * @brief Copy assignment is disabled.
				 * @par Parameters
				 * The unnamed parameter is the reader that cannot be copied.
				 * @return No value; this operation is deleted.
				 */
				BufferedRemoteFileReader& operator=(const BufferedRemoteFileReader&) = delete;

				/**
				 * @brief Move assignment is disabled while a mount token is registered.
				 * @par Parameters
				 * The unnamed parameter is the reader that cannot be moved.
				 * @return No value; this operation is deleted.
				 */
				BufferedRemoteFileReader& operator=(BufferedRemoteFileReader&&) = delete;

				/**
				 * @brief Release this leaf's token before base destruction.
				 * The DataPlane owns the heartbeat.
				 */
				~BufferedRemoteFileReader() noexcept override;

			protected:
				/**
				 * @brief Network-interface snapshot for this connected origin.
				 * @return Connected origin device, or an empty pointer if unavailable.
				 */
				StormByte::Safe::Shared<StormByte::System::Device> OriginDevice() const override;

				/**
				 * @brief Accept the network device without probing its accessor as a filesystem path.
				 * @param device Origin device to check.
				 * @return Whether the device is usable as a network origin.
				 */
				bool OriginDeviceUsable(const StormByte::Safe::Shared<StormByte::System::Device>& device) const noexcept override;

				/**
				 * @brief Open the token's server-side reader handle.
				 * @return Result of opening the reader channel.
				 */
				Buffer::IO::Result OriginOpen() override;

				/**
				 * @brief Release this reader token without closing the peer plane.
				 * @return Result of releasing the reader channel.
				 */
				Buffer::IO::Result OriginClose() override;

				/**
				 * @brief Pull bytes at this leaf's absolute logical offset.
				 * @param count Maximum number of bytes to read.
				 * @param destination FIFO receiving the bytes.
				 * @return Read result and transferred byte count, or an error.
				 */
				Buffer::IO::Result OriginPull(StormByte::ByteSize count, Buffer::FIFO& destination) override;

				/**
				 * @brief Move only this Buffer leaf's logical cursor.
				 * @param offset Signed displacement relative to the selected position.
				 * @param mode Reference position for the displacement.
				 * @return Seek result, or an error for an invalid position.
				 */
				Buffer::IO::Result OriginSeek(std::ptrdiff_t offset, Buffer::Position mode) override;

				/**
				 * @brief Query the server-side file length.
				 * @return File length in bytes, or an empty value if unavailable.
				 */
				StormByte::Safe::Optional<StormByte::ByteSize> OriginSize() const noexcept override;

			private:
				/**
				 * @brief Construct a reader attached to a mounted peer channel.
				 * @param locator Logical origin locator, not a server filesystem path.
				 * @param plane Client-owned peer data plane.
				 * @param token Capability assigned to the reader.
				 */
				BufferedRemoteFileReader(StormByte::Safe::String locator,
					std::weak_ptr<Detail::RemoteFile::DataPlane> plane, RemoteFileMount::ChannelToken token);

				/**
				 * @brief Mark the reader origin failed after a channel error.
				 */
				void MarkFailed() noexcept;

				/**
				 * @brief Release the mount token at most once.
				 * @return Whether the token was already released or its release succeeded.
				 */
				bool ReleaseToken() noexcept;

				/**
				 * @brief Client-owned peer data plane.
				 */
				std::weak_ptr<Detail::RemoteFile::DataPlane> m_plane;

				/**
				 * @brief Capability assigned to this reader.
				 */
				RemoteFileMount::ChannelToken m_token{};

				/**
				 * @brief Absolute logical read cursor.
				 */
				StormByte::ByteSize m_offset{0};

				/**
				 * @brief Whether token release has already been attempted.
				 */
				bool m_token_released{false};

				/**
				 * @brief Client may construct mounted reader leaves.
				 */
				friend class Client;

				/**
				 * @brief Allow the Base heap factory to construct this private Network leaf.
				 * @tparam T Type constructed on Base's heap.
				 * @tparam Args Constructor argument types.
				 * @par Parameters
				 * The unnamed argument pack supplies forwarded constructor arguments.
				 * @return Unique owner of the constructed leaf.
				 */
				template<class T, class... Args>
				friend StormByte::Safe::Unique<T> StormByte::Safe::Heap::MakeUnique(Args&&...);
		};

		/**
		 * @class BufferedRemoteFileWriter
		 * @brief File-like buffered writer with an exclusive server-side mount.
		 */
		class STORMBYTE_NETWORK_PUBLIC BufferedRemoteFileWriter final:
			public Buffer::IO::BufferedLocationWriter {
			public:
				/**
				 * @brief Copy construction is disabled.
				 * @par Parameters
				 * The unnamed parameter is the writer that cannot be copied.
				 */
				BufferedRemoteFileWriter(const BufferedRemoteFileWriter&) = delete;

				/**
				 * @brief Move construction is disabled while a mount token is registered.
				 * @par Parameters
				 * The unnamed parameter is the writer that cannot be moved.
				 */
				BufferedRemoteFileWriter(BufferedRemoteFileWriter&&) = delete;

				/**
				 * @brief Copy assignment is disabled.
				 * @par Parameters
				 * The unnamed parameter is the writer that cannot be copied.
				 * @return No value; this operation is deleted.
				 */
				BufferedRemoteFileWriter& operator=(const BufferedRemoteFileWriter&) = delete;

				/**
				 * @brief Move assignment is disabled while a mount token is registered.
				 * @par Parameters
				 * The unnamed parameter is the writer that cannot be moved.
				 * @return No value; this operation is deleted.
				 */
				BufferedRemoteFileWriter& operator=(BufferedRemoteFileWriter&&) = delete;

				/**
				 * @brief Release this leaf's token before base destruction.
				 * The DataPlane owns the heartbeat.
				 */
				~BufferedRemoteFileWriter() noexcept override;

			protected:
				/**
				 * @brief Network-interface snapshot for this connected origin.
				 * @return Connected origin device, or an empty pointer if unavailable.
				 */
				StormByte::Safe::Shared<StormByte::System::Device> OriginDevice() const override;

				/**
				 * @brief Accept the network device without probing its accessor as a filesystem path.
				 * @param device Origin device to check.
				 * @return Whether the device is usable as a network origin.
				 */
				bool OriginDeviceUsable(const StormByte::Safe::Shared<StormByte::System::Device>& device) const noexcept override;

				/**
				 * @brief Open this writer token on the peer plane.
				 * @return Result of opening the writer channel.
				 */
				Buffer::IO::Result OriginOpen() override;

				/**
				 * @brief Release this writer token without closing the peer plane.
				 * @return Result of releasing the writer channel.
				 */
				Buffer::IO::Result OriginClose() override;

				/**
				 * @brief Write a complete byte span at this leaf's absolute logical offset.
				 * @param data Bytes to write.
				 * @return Write result and transferred byte count, or an error.
				 */
				Buffer::IO::Result OriginPush(std::span<const std::byte> data) override;

				/**
				 * @brief Flush the server-side writer handle.
				 * @return Flush result, or an error.
				 */
				Buffer::IO::Result OriginFlush() override;

				/**
				 * @brief Truncate the server-side file to zero bytes.
				 * This API does not accept a target size.
				 * @return Truncation result, or an error.
				 */
				Buffer::IO::Result OriginTruncate() override;

				/**
				 * @brief Set this Buffer leaf's local writer offset.
				 * @param absolute Absolute byte offset from the start of the file.
				 * @return Seek result.
				 */
				Buffer::IO::Result OriginSeek(StormByte::ByteSize absolute) override;

				/**
				 * @brief Query the server-side file length.
				 * @return File length in bytes, or zero if unavailable.
				 */
				StormByte::ByteSize OriginSize() const noexcept override;

			private:
				/**
				 * @brief Construct a writer attached to a mounted peer channel.
				 * @param locator Logical origin locator, not a server filesystem path.
				 * @param plane Client-owned peer data plane.
				 * @param token Capability assigned to the writer.
				 */
				BufferedRemoteFileWriter(StormByte::Safe::String locator,
					std::weak_ptr<Detail::RemoteFile::DataPlane> plane, RemoteFileMount::ChannelToken token);

				/**
				 * @brief Mark the writer origin failed after a channel error.
				 */
				void MarkFailed() noexcept;

				/**
				 * @brief Release the mount token at most once.
				 * @return Whether the token was already released or its release succeeded.
				 */
				bool ReleaseToken() noexcept;

				/**
				 * @brief Client-owned peer data plane.
				 */
				std::weak_ptr<Detail::RemoteFile::DataPlane> m_plane;

				/**
				 * @brief Capability assigned to this writer.
				 */
				RemoteFileMount::ChannelToken m_token{};

				/**
				 * @brief Absolute logical write cursor.
				 */
				StormByte::ByteSize m_offset{0};

				/**
				 * @brief Whether token release has already been attempted.
				 */
				bool m_token_released{false};

				/**
				 * @brief Client may construct mounted writer leaves.
				 */
				friend class Client;

				/**
				 * @brief Allow the Base heap factory to construct this private Network leaf.
				 * @tparam T Type constructed on Base's heap.
				 * @tparam Args Constructor argument types.
				 * @par Parameters
				 * The unnamed argument pack supplies forwarded constructor arguments.
				 * @return Unique owner of the constructed leaf.
				 */
				template<class T, class... Args>
				friend StormByte::Safe::Unique<T> StormByte::Safe::Heap::MakeUnique(Args&&...);
		};

		/**
		 * @brief Base-heap owner movable to Unique<Buffer::IO::BufferedLocationReader> without slicing.
		 */
		using RemoteFileReaderHandle = StormByte::Safe::Unique<BufferedRemoteFileReader>;

		/**
		 * @brief Base-heap owner movable to Unique<Buffer::IO::BufferedLocationWriter> without slicing.
		 */
		using RemoteFileWriterHandle = StormByte::Safe::Unique<BufferedRemoteFileWriter>;
	}
}

/**
 * @brief Reader instances are constructed in Network on Base's heap and destroyed virtually.
 * Base, Buffer and Network must remain loaded with a compatible ABI until release.
 */
STORMBYTE_DECLARE_MAYBE_SAFE(StormByte::Network::BufferedRemoteFileReader);

/**
 * @brief Writer instances are constructed in Network on Base's heap and destroyed virtually.
 * Base, Buffer and Network must remain loaded with a compatible ABI until release.
 */
STORMBYTE_DECLARE_MAYBE_SAFE(StormByte::Network::BufferedRemoteFileWriter);
