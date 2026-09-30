#pragma once

#include <StormByte/network/remote_file_protocol.hxx>
#include <StormByte/network/remote_file_mount.hxx>
#include <StormByte/network/socket/server.hxx>

#include <filesystem>
#include <fstream>
#include <chrono>
#include <atomic>
#include <array>
#include <cstddef>
#include <deque>
#include <map>
#include <mutex>
#include <optional>
#include <set>
#include <string>
#include <unordered_map>

/**
 * @brief Root namespace of the StormByte C++ suite.
 */
namespace StormByte {
	/**
	 * @brief Network module of the StormByte suite.
	 */
	namespace Network {
		/**
		 * @brief Private server endpoint for one application-authorized file mount.
		 */
		namespace Detail::RemoteFile {
			/**
			 * @class MountRegistry
			 * @brief Server-wide token and one-handle-per-path registry.
			 */
			class MountRegistry final {
			public:
				/** @brief Capability type used by remote mounts. */
				using Token = RemoteFileMount::ChannelToken;

				/**
				 * @brief Register one authorized token and acquire its shared path handle.
				 * @param token Random mount capability.
				 * @param path Server-local path selected by application policy.
				 * @param access Reader or exclusive writer access.
				 * @return true when registered.
				 */
				bool AddMount(const Token& token, const std::filesystem::path& path,
					RemoteFileMount::Access access) noexcept;

				/** @brief Whether this registry still owns one token. */
				bool HasToken(const Token& token) const noexcept;

				/** @brief Release one token and close its path handle when no token remains. */
				bool ReleaseToken(const Token& token) noexcept;

				/** @brief Execute one authorized file operation on the worker pool. */
				Message Execute(const Message& request) noexcept;

			private:
				struct TokenHash {
					/** @brief Hash one fixed-width capability. */
					std::size_t operator()(const Token& token) const noexcept;
				};

				struct FileHandle {
					std::filesystem::path path; ///< Host path for this shared handle.
					RemoteFileMount::Access access; ///< Shared read or exclusive write mode.
					std::fstream stream; ///< One synchronized handle for this path.
					std::mutex mutex; ///< Serializes operations on the C++ stream.
				};

				struct TokenEntry {
					RemoteFileMount::Access access; ///< Access granted to this token.
					std::shared_ptr<FileHandle> file; ///< Shared path handle.
					bool opened{false}; ///< Whether this leaf successfully opened its token.
				};

				mutable std::mutex m_mutex; ///< Protects path and token tables.
				std::unordered_map<std::string, std::shared_ptr<FileHandle>> m_files; ///< One handle per canonical path.
				std::unordered_map<Token, TokenEntry, TokenHash> m_tokens; ///< Live capabilities.
			};

			/**
			 * @class Host
			 * @brief One multiplexed server data-plane endpoint for one application peer.
			 */
			class Host final {
			public:
				/** @brief Fixed-size mount capability used on this peer plane. */
				using Token = RemoteFileMount::ChannelToken;

				/**
				 * @brief Create one host endpoint for a peer, sharing the server mount registry.
				 */
				Host(Connection::Protocol protocol, std::string bind_address, Buffer::Pipeline input,
					Buffer::Pipeline output, std::uint16_t timeout_seconds,
					std::shared_ptr<MountRegistry> registry,
					StormByte::Shared<StormByte::Logger::Log> logger);

				/** @brief Stop accepting and release this peer's mounts. */
				~Host() noexcept;

				/** @brief Bind one ephemeral peer data-plane listener. */
				bool Start() noexcept;

				/** @brief Port assigned to this peer plane. */
				std::uint16_t Port() const noexcept;

				/** @brief Heartbeat timeout selected when the plane was mounted. */
				std::uint16_t TimeoutSeconds() const noexcept;

				/** @brief Whether the plane has ended. */
				bool Finished() const noexcept;

				/** @brief Stop the peer plane and release all tokens owned by it. */
				void Stop() noexcept;

				/** @brief Add one registered token to this peer's authorized token set. */
				bool RegisterToken(const Token& token) noexcept;

				/** @brief Roll back one token when server-side mount registration fails. */
				void UnregisterToken(const Token& token) noexcept;

				/** @brief Current native listener or accepted-client handle. */
				Connection::HandlerType Handle() const noexcept;

				/** @brief Whether this peer is still waiting for its single data-plane accept. */
				bool WaitingForAccept() const noexcept;

				/** @brief Whether the next peer operation can be read without exceeding its queue bound. */
				bool CanRead() const noexcept;

				/** @brief Whether a frame is waiting for worker-pool submission. */
				bool ReadyForProcessing() const noexcept;

				/** @brief Whether a complete bounded frame is already buffered in user space. */
				bool HasBufferedFrame() const noexcept;

				/** @brief Whether non-blocking output is queued. */
				bool HasOutput() const noexcept;

				/** @brief Block reads while one operation waits for worker-pool capacity. */
				void SetTaskBlocked(bool blocked) noexcept;

				/** @brief Whether the peer heartbeat/auth deadline expired. */
				bool Expired() const noexcept;

				/** @brief Accept the one client connection on the peer listener. */
				bool AcceptReady() noexcept;

				/** @brief Read and parse frames from the ready peer socket. */
				StormByte::Network::ExpectedVoid ReadReady() noexcept;

				/** @brief Take the single queued file operation for the worker pool. */
				Message TakeRequest() noexcept;

				/** @brief Restore an operation when the bounded pool loses a Submit race. */
				void RequeueRequest(Message request) noexcept;

				/** @brief Process one disk operation on a worker. */
				Message ProcessRequest(const Message& request) noexcept;

				/** @brief Queue one response for non-blocking event-loop output. */
				bool QueueResponse(const Message& response) noexcept;

				/** @brief Flush queued output once without waiting. */
				StormByte::Expected<bool, ConnectionError> FlushOutput() noexcept;

			private:
				bool QueueEncodedResponse(const Message& response) noexcept;
				void Fail() noexcept;
				void ReleaseTokens() noexcept;
				bool IsRegisteredToken(const RemoteFileMount::ChannelToken& token) const noexcept;

				Connection::Protocol m_protocol;
				std::string m_bind_address;
				Buffer::Pipeline m_input;
				Buffer::Pipeline m_output;
				std::uint16_t m_timeout_seconds;
				std::shared_ptr<MountRegistry> m_registry;
				StormByte::Shared<StormByte::Logger::Log> m_logger;
				std::unique_ptr<Socket::Server> m_listener;
				std::shared_ptr<Socket::Client> m_active_client;
				mutable std::mutex m_mutex; ///< Protects queued operation/output from worker completions.
				std::set<RemoteFileMount::ChannelToken> m_registered_tokens; ///< Mounts belonging to this peer.
				std::set<RemoteFileMount::ChannelToken> m_attached_tokens; ///< Tokens attached on the data socket.
				StormByte::BinaryData m_input_buffer; ///< Partial framed input bytes.
				std::optional<Message> m_pending_request; ///< At most one queued file operation per peer.
				StormByte::BinaryData m_output_buffer; ///< One bounded transformed response frame.
				std::size_t m_output_offset{0}; ///< Bytes already sent from the current response.
				bool m_in_flight{false}; ///< A file operation is executing in WorkerPool.
				bool m_task_blocked{false}; ///< WorkerPool had no free bounded queue slot.
				bool m_accepted{false}; ///< Exactly one socket may attach to this peer listener.
				std::atomic<bool> m_stopping{false};
				std::atomic<bool> m_finished{false};
				std::uint16_t m_port{0};
				std::uint64_t m_last_request_id{0}; ///< Monotonic sequence on this plane.
				std::chrono::steady_clock::time_point m_last_activity; ///< Last valid frame or plane start.
			};
		}
	}
}