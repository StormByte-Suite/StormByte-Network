#pragma once

#include <StormByte/buffer/pipeline.hxx>
#include <StormByte/network/exception.hxx>
#include <StormByte/network/remote_file_mount.hxx>
#include <StormByte/network/socket/client.hxx>
#include <StormByte/system/device.hxx>
#include <StormByte/serializable.hxx>

#include <array>
#include <cstdint>
#include <span>
#include <atomic>
#include <condition_variable>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <thread>

/**
 * @brief Root namespace of the StormByte C++ suite.
 */
namespace StormByte {
	/**
	 * @brief Network module of the StormByte suite.
	 */
	namespace Network {
		/**
		 * @brief Private remote-file channel protocol, inaccessible to application dispatch.
		 */
		namespace Detail::RemoteFile {
			/** @brief Maximum transformed channel message accepted from the wire. */
			inline constexpr std::uint64_t max_message_size = 4ull * 1024ull * 1024ull;
			/** @brief Serialized fixed header size including the capability token. */
			inline constexpr std::size_t message_header_size = sizeof(std::uint8_t) * 2
				+ sizeof(std::uint64_t) * 3 + 32;
			/** @brief Maximum file bytes carried in one untransformed message. */
			inline constexpr std::uint64_t max_data_size = max_message_size - message_header_size;

			/**
			 * @enum Opcode
			 * @brief Private commands understood only by the Network channel endpoints.
			 */
			enum class Opcode: std::uint8_t {
				Attach = 1, ///< Present the one-shot mount capability.
				Open = 2, ///< Open the authorized server-side file handle.
				Close = 3, ///< Close the server-side file handle.
				Read = 4, ///< Read from the server-side file cursor.
				Write = 5, ///< Write at the server-side file cursor.
				Seek = 6, ///< Seek the server-side file cursor.
				Size = 7, ///< Query the server-side file length.
				Flush = 8, ///< Flush the server-side file handle.
				Truncate = 9, ///< Truncate the server-side file to zero.
				Ping = 10, ///< Heartbeat request.
				Pong = 11, ///< Heartbeat response.
				CloseToken = 12 ///< Release one mount token without closing the peer plane.
			};

			/**
			 * @enum Status
			 * @brief Result encoded in a private channel message.
			 */
			enum class Status: std::uint8_t {
				Ok = 0, ///< Operation completed.
				End = 1, ///< Reader reached the file end.
				Failed = 2 ///< Operation failed or was not authorized.
			};

			/**
			 * @struct Message
			 * @brief One decoded private channel message.
			 */
			struct Message {
				Opcode opcode{Opcode::Ping}; ///< Private command identifier.
				Status status{Status::Ok}; ///< Operation result.
				std::uint64_t request_id{0}; ///< Request/reply correlation identifier.
				std::uint64_t offset{0}; ///< Absolute file offset for read/write operations.
				std::uint64_t value{0}; ///< Command length or numeric result.
				std::array<std::byte, 32> token{}; ///< Capability required for every mount operation.
				StormByte::BinaryData data; ///< Remaining command data.
			};

			/**
			 * @class DataPlane
			 * @brief One serialized, multiplexed remote-file transport for a Client peer.
		 */
		class DataPlane final {
			public:
				using Token = RemoteFileMount::ChannelToken; ///< Mount capability registered on this plane.

				/**
				 * @brief Bind the peer's single connected data socket and shared pipelines.
				 * @param socket Connected peer data-plane socket.
				 * @param input Pipeline applied after receive and before decode.
				 * @param output Pipeline applied after encode and before send.
				 * @param timeout_seconds Maximum heartbeat silence interval.
				 * @param logger Diagnostic logger.
				 * @param local_address Local interface address selected by the socket.
				 * @param device Shared polymorphic device snapshot for this plane.
				 * @param port Server data-plane port published by the mount.
				 */
				DataPlane(std::shared_ptr<Socket::Client> socket, Buffer::Pipeline input,
					Buffer::Pipeline output, std::uint16_t timeout_seconds,
					StormByte::Shared<StormByte::Logger::Log> logger, std::string local_address,
					StormByte::Shared<StormByte::System::Device> device, std::uint16_t port) noexcept;

				/** @brief Stop heartbeat and close the shared peer plane. */
				~DataPlane() noexcept;

				/** @brief Register one mount capability on the established plane. */
				bool RegisterToken(const Token& token, std::function<void()> on_failure) noexcept;

				/** @brief Release one mount without closing the peer plane. */
				bool ReleaseToken(const Token& token) noexcept;

				/** @brief Start the plane's single ping/pong monitor. */
				bool StartHeartbeat() noexcept;

				/** @brief Send one token-authorized operation and validate its matching response. */
				StormByte::Expected<Message, ConnectionError> Exchange(Message request) noexcept;

				/** @brief Whether the peer plane has failed or been stopped. */
				bool Failed() const noexcept;

				/** @brief Stop the heartbeat thread and close the plane. */
				void Stop() noexcept;

				/** @brief Local address selected by the plane socket. */
				const std::string& LocalAddress() const noexcept;

				/** @brief Shared device snapshot for every leaf on this plane. */
				StormByte::Shared<StormByte::System::Device> Device() const noexcept;

				/** @brief Server port assigned to this peer plane. */
				std::uint16_t Port() const noexcept;

			private:
				StormByte::Expected<Message, ConnectionError> ExchangeLocked(const Message& request) noexcept;
				void RunHeartbeat() noexcept;
				void MarkFailed() noexcept;
				void NotifyTokenFailures() noexcept;

				std::shared_ptr<Socket::Client> m_socket;
				Buffer::Pipeline m_input;
				Buffer::Pipeline m_output;
				const std::uint16_t m_timeout_seconds;
				StormByte::Shared<StormByte::Logger::Log> m_logger;
				std::string m_local_address;
				StormByte::Shared<StormByte::System::Device> m_device;
				const std::uint16_t m_port;
				mutable std::mutex m_mutex;
				std::mutex m_heartbeat_mutex;
				std::condition_variable m_heartbeat_condition;
				std::mutex m_token_mutex;
				std::map<Token, std::function<void()>> m_token_failures;
				std::thread m_heartbeat_thread;
				std::atomic<std::uint64_t> m_next_request_id{1};
				std::atomic<bool> m_failed{false};
				bool m_stopping{false};
			};

			/** @brief Build a polymorphic network-interface snapshot for a connected socket. */
			StormByte::Shared<StormByte::System::Device> CreateNetworkDevice(const std::string& local_address);

			/** @brief Serialize a message field-by-field. */
			StormByte::BinaryData Serialize(const Message& message);

			/** @brief Decode one exact message and reject unknown opcode/status values. */
			StormByte::Expected<Message, StormByte::DeserializeError> Deserialize(
				std::span<const std::byte> payload) noexcept;

			/** @brief Run bytes through the supplied pipeline synchronously. */
			StormByte::Expected<StormByte::BinaryData, ConnectionError> Process(
				Buffer::Pipeline& pipeline, StormByte::BinaryData input,
				StormByte::Shared<StormByte::Logger::Log> logger);

			/** @brief Read a fixed-width message size and bounded payload from the socket. */
			StormByte::Expected<StormByte::BinaryData, ConnectionError> ReceivePayload(
				Socket::Client& socket, std::uint16_t timeout_seconds) noexcept;

			/** @brief Send payload with a fixed-width size prefix. */
			StormByte::Network::ExpectedVoid SendPayload(Socket::Client& socket,
				std::span<const std::byte> payload) noexcept;
		}
	}
}