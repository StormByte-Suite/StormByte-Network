#include <StormByte/logger/threaded_log.hxx>
#include <StormByte/network/client.hxx>
#include <StormByte/network/remote_file.hxx>
#include <StormByte/network/server.hxx>
#include <StormByte/serializable.hxx>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <optional>
#include <set>
#include <span>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#ifdef WINDOWS
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace RemoteFileTest {
	namespace Net = StormByte::Network;

	namespace Buf = StormByte::Buffer;

	using Log = StormByte::Logger::Log;

	using Mount = Net::RemoteFileMount;

	using Packet = Net::Transport::Packet;

	constexpr std::string_view address = "127.0.0.1";

	constexpr unsigned short port = 7183;

	constexpr std::uint64_t remote_frame_limit = 4ull * 1024ull * 1024ull;

	constexpr std::size_t remote_header_size = 2 + sizeof(std::uint64_t) * 3 + 32;

	/**
	 * @brief Require a test condition to hold.
	 * @param condition Condition to check.
	 * @param message Failure message.
	 */
	void Check(bool condition, std::string_view message);

#ifdef WINDOWS
	using RawSocket = SOCKET;

	constexpr RawSocket invalid_raw_socket = INVALID_SOCKET;
#else
	using RawSocket = int;

	constexpr RawSocket invalid_raw_socket = -1;
#endif

	/**
	 * @brief Raw socket peer for exercising the private remote-file protocol.
	 */
	class RawPeer final {
		public:
			/**
			 * @brief Connect to the private data-plane listener.
			 * @param peer_port Listener port.
			 */
			explicit RawPeer(const unsigned short peer_port) {
				m_socket = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
				if (m_socket == invalid_raw_socket)
					return;
				sockaddr_in remote{};
				remote.sin_family = AF_INET;
				remote.sin_port = htons(peer_port);
				if (inet_pton(AF_INET, address.data(), &remote.sin_addr) != 1
					|| ::connect(m_socket, reinterpret_cast<const sockaddr*>(&remote), sizeof(remote)) != 0) {
					Close();
					return;
				}
#ifdef WINDOWS
				DWORD timeout_ms = 5000;
				(void)setsockopt(m_socket, SOL_SOCKET, SO_RCVTIMEO,
					reinterpret_cast<const char*>(&timeout_ms), sizeof(timeout_ms));
#else
				timeval timeout_value{ .tv_sec = 5, .tv_usec = 0 };
				(void)setsockopt(m_socket, SOL_SOCKET, SO_RCVTIMEO, &timeout_value, sizeof(timeout_value));
#endif
			}

			/**
			 * @brief Close the owned socket.
			 */
			~RawPeer() {
				Close();
			}

			/**
			 * @brief Report whether the peer owns a valid socket.
			 * @return Whether the socket is valid.
			 */
			bool Connected() const noexcept {
				return m_socket != invalid_raw_socket;
			}

			/**
			 * @brief Close and invalidate the owned socket.
			 */
			void Close() noexcept {
				if (m_socket == invalid_raw_socket)
					return;
#ifdef WINDOWS
				closesocket(m_socket);
#else
				close(m_socket);
#endif
				m_socket = invalid_raw_socket;
			}

			/**
			 * @brief Encode and send a remote-file message.
			 * @param opcode Message opcode.
			 * @param request_id Request sequence identifier.
			 * @param token Mount capability token.
			 * @param offset File offset.
			 * @param value Operation-specific value.
			 * @param body Message body.
			 * @return Whether the complete frame was sent.
			 */
			bool SendMessage(const std::uint8_t opcode, const std::uint64_t request_id,
				const Mount::ChannelToken& token, const std::uint64_t offset = 0,
				const std::uint64_t value = 0, std::span<const std::byte> body = {}) {
				StormByte::BinaryData payload;
				payload.append(StormByte::Serializable<std::uint8_t>(opcode).Serialize());
				payload.append(StormByte::Serializable<std::uint8_t>(0).Serialize());
				payload.append(StormByte::Serializable<std::uint64_t>(request_id).Serialize());
				payload.append(StormByte::Serializable<std::uint64_t>(offset).Serialize());
				payload.append(StormByte::Serializable<std::uint64_t>(value).Serialize());
				payload.append(std::span<const std::byte>{token});
				payload.append(body);
				for (auto& byte: payload)
					byte ^= std::byte{0x67};
				StormByte::BinaryData frame = StormByte::Serializable<std::uint64_t>(payload.size()).Serialize();
				frame.append(std::move(payload));
				return SendAll(frame);
			}

			/**
			 * @brief Send a frame length with an optional partial payload.
			 * @param length Advertised payload length.
			 * @param partial Payload bytes to send after the prefix.
			 * @return Whether all supplied bytes were sent.
			 */
			bool SendRawFramePrefix(const std::uint64_t length, const std::span<const std::byte> partial = {}) {
				StormByte::BinaryData frame = StormByte::Serializable<std::uint64_t>(length).Serialize();
				frame.append(partial);
				return SendAll(frame);
			}

			/**
			 * @brief Send consecutive read requests in a single batch.
			 * @param token Mount capability token.
			 * @param reads File offsets and requested lengths.
			 * @param first_request_id Initial request sequence identifier.
			 * @return Whether the complete batch was sent.
			 */
			bool SendReadBurst(const Mount::ChannelToken& token,
				const std::vector<std::pair<std::uint64_t, std::uint64_t>>& reads,
				const std::uint64_t first_request_id) {
				StormByte::BinaryData batch;
				std::uint64_t request_id = first_request_id;
				for (const auto& [offset, length]: reads) {
					StormByte::BinaryData payload;
					payload.append(StormByte::Serializable<std::uint8_t>(4).Serialize());
					payload.append(StormByte::Serializable<std::uint8_t>(0).Serialize());
					payload.append(StormByte::Serializable<std::uint64_t>(request_id++).Serialize());
					payload.append(StormByte::Serializable<std::uint64_t>(offset).Serialize());
					payload.append(StormByte::Serializable<std::uint64_t>(length).Serialize());
					payload.append(std::span<const std::byte>{token});
					for (auto& byte: payload)
						byte ^= std::byte{0x67};
					batch.append(StormByte::Serializable<std::uint64_t>(payload.size()).Serialize());
					batch.append(std::move(payload));
				}
				return SendAll(batch);
			}

			/**
			 * @brief Receive and decode a complete response frame.
			 * @return Decoded response payload.
			 */
			StormByte::BinaryData ReceiveMessage() {
				std::array<std::byte, sizeof(std::uint64_t)> prefix{};
				Check(ReceiveExact(prefix), "data-plane response prefix failed");
				const auto length = StormByte::Serializable<std::uint64_t>::Deserialize(prefix);
				Check(length && *length > 0 && *length <= remote_frame_limit, "invalid data-plane response length");
				StormByte::BinaryData payload{StormByte::ByteSize{*length}};
				Check(ReceiveExact(std::span<std::byte>{payload.data(), payload.size()}), "data-plane response body failed");
				for (auto& byte: payload)
					byte ^= std::byte{0x67};
				return payload;
			}

			/**
			 * @brief Wait for the peer to close or fail the connection.
			 * @return Whether the connection closed within the receive deadline.
			 */
			bool WaitForClose() const noexcept {
				if (!Connected())
					return true;
				fd_set descriptors;
				FD_ZERO(&descriptors);
				FD_SET(m_socket, &descriptors);
				timeval timeout_value{ .tv_sec = 5, .tv_usec = 0 };
#ifdef WINDOWS
				const int ready = select(0, &descriptors, nullptr, nullptr, &timeout_value);
#else
				const int ready = select(m_socket + 1, &descriptors, nullptr, nullptr, &timeout_value);
#endif
				if (ready <= 0)
					return false;
				char byte = 0;
#ifdef WINDOWS
				return ::recv(m_socket, &byte, 1, 0) <= 0;
#else
				return ::recv(m_socket, &byte, 1, 0) <= 0;
#endif
			}

			/**
			 * @brief Shut down the socket's sending direction.
			 */
			void ShutdownSend() noexcept {
				if (m_socket == invalid_raw_socket)
					return;
#ifdef WINDOWS
				(void)::shutdown(m_socket, SD_SEND);
#else
				(void)::shutdown(m_socket, SHUT_WR);
#endif
			}

		private:
			/**
			 * @brief Send all bytes, handling partial socket writes.
			 * @param bytes Bytes to send.
			 * @return Whether every byte was sent.
			 */
			bool SendAll(std::span<const std::byte> bytes) {
				while (!bytes.empty()) {
#ifdef WINDOWS
					const int sent = ::send(m_socket, reinterpret_cast<const char*>(bytes.data()),
						static_cast<int>(bytes.size()), 0);
#else
					const ssize_t sent = ::send(m_socket, bytes.data(), bytes.size(), 0);
#endif
					if (sent <= 0)
						return false;
					bytes = bytes.subspan(static_cast<std::size_t>(sent));
				}
				return true;
			}

			/**
			 * @brief Fill a byte span, handling partial socket reads.
			 * @param bytes Destination bytes.
			 * @return Whether the entire span was filled.
			 */
			bool ReceiveExact(std::span<std::byte> bytes) {
				while (!bytes.empty()) {
#ifdef WINDOWS
					const int received = ::recv(m_socket, reinterpret_cast<char*>(bytes.data()),
						static_cast<int>(bytes.size()), 0);
#else
					const ssize_t received = ::recv(m_socket, bytes.data(), bytes.size(), 0);
#endif
					if (received <= 0)
						return false;
					bytes = bytes.subspan(static_cast<std::size_t>(received));
				}
				return true;
			}

			/**
			 * @brief Owned data-plane socket.
			 */
			RawSocket m_socket{invalid_raw_socket};
	};

	/**
	 * @brief Retrieve the shared test logger.
	 * @return Logger used by the test endpoints.
	 */
	StormByte::Safe::Shared<Log> Logger() {
		static auto logger = StormByte::Safe::Heap::MakeShared<StormByte::Logger::ThreadedLog>(
			std::cerr, StormByte::Logger::Level::Error, "[RemoteFileTest] %T:");
		return logger;
	}

	/**
	 * @brief Require a test condition to hold.
	 * @param condition Condition to check.
	 * @param message Failure message.
	 */
	void Check(const bool condition, const std::string_view message) {
		if (!condition)
			throw std::runtime_error(std::string{message});
	}

	/**
	 * @brief Application opcodes used by the test control protocol.
	 */
	enum class AppOpcode: unsigned short {
		ReadRequest = Packet::PROCESS_THRESHOLD,
		WriteRequest,
		Mount,
		Unauthorized
	};

	/**
	 * @brief Request a reader or writer mount from the test server.
	 */
	class Request final: public Packet {
		public:
			/**
			 * @brief Select a mount operation and fixture path.
			 * @param write Whether to request write access.
			 * @param missing Whether to select a missing read path.
			 * @param path_selection Fixture path selector.
			 */
			Request(const bool write, const bool missing = false, const std::uint16_t path_selection = 0):
				Packet(static_cast<OpcodeType>(write ? AppOpcode::WriteRequest : AppOpcode::ReadRequest)),
				m_missing(missing),
				m_path_selection(path_selection) {
			}

			/**
			 * @brief Report whether a missing read path was requested.
			 * @return Whether the missing-path flag is set.
			 */
			bool Missing() const noexcept {
				return m_missing;
			}

			/**
			 * @brief Retrieve the fixture path selector.
			 * @return Requested fixture path selector.
			 */
			std::uint16_t PathSelection() const noexcept {
				return m_path_selection;
			}

			/**
			 * @brief Serialize the request flags and path selector.
			 * @return Serialized request payload.
			 */
			StormByte::BinaryData DoSerialize() const noexcept override {
				StormByte::BinaryData data = StormByte::Serializable<std::uint8_t>(m_missing ? 1 : 0).Serialize();
				data.append(StormByte::Serializable<std::uint16_t>(m_path_selection).Serialize());
				return data;
			}

		private:
			/**
			 * @brief Whether to request a missing read path.
			 */
			bool m_missing;

			/**
			 * @brief Fixture path selector.
			 */
			std::uint16_t m_path_selection;
	};

	/**
	 * @brief Carry a remote-file mount response.
	 */
	class MountPacket final: public Packet {
		public:
			/**
			 * @brief Store a mount response.
			 * @param value Mount descriptor to carry.
			 */
			explicit MountPacket(Mount value):
				Packet(static_cast<OpcodeType>(AppOpcode::Mount)),
				m_value(std::move(value)) {
			}

			/**
			 * @brief Retrieve the carried mount descriptor.
			 * @return Stored mount descriptor.
			 */
			const Mount& Value() const noexcept {
				return m_value;
			}

			/**
			 * @brief Serialize the mount descriptor.
			 * @return Serialized mount payload.
			 */
			StormByte::BinaryData DoSerialize() const noexcept override {
				return StormByte::Serializable<Mount>(m_value).Serialize();
			}

		private:
			/**
			 * @brief Mount descriptor carried by this packet.
			 */
			Mount m_value;
	};

	/**
	 * @brief Report application-level mount authorization failure.
	 */
	class UnauthorizedPacket final: public Packet {
		public:
			/**
			 * @brief Construct an authorization failure response.
			 */
			UnauthorizedPacket():
				Packet(static_cast<OpcodeType>(AppOpcode::Unauthorized)) {
			}

			/**
			 * @brief Serialize the empty authorization failure payload.
			 * @return Empty payload.
			 */
			StormByte::BinaryData DoSerialize() const noexcept override {
				return {};
			}
	};

	/**
	 * @brief Create the test control packet deserializer.
	 * @return Deserializer for application test packets.
	 */
	Net::DeserializePacketFunction Factory() {
		return [](Packet::OpcodeType opcode, Buf::Consumer payload, StormByte::Safe::Shared<Log>) -> Net::PacketPointer {
			StormByte::BinaryData bytes;
			payload.ExtractUntilEoF(bytes);
			switch (static_cast<AppOpcode>(opcode)) {
				case AppOpcode::ReadRequest:
				case AppOpcode::WriteRequest: {
					if (bytes.size() != sizeof(std::uint8_t) + sizeof(std::uint16_t))
						return nullptr;
					auto flags = StormByte::Serializable<std::uint8_t>::Deserialize(
						std::span<const std::byte>{bytes.data(), sizeof(std::uint8_t)});
					auto path_selection = StormByte::Serializable<std::uint16_t>::Deserialize(
						std::span<const std::byte>{bytes.data() + sizeof(std::uint8_t), sizeof(std::uint16_t)});
					if (!flags || (*flags & 0xFE) != 0 || !path_selection)
						return nullptr;
					return StormByte::Network::PacketPointer::MakePointer<Request>(opcode == static_cast<Packet::OpcodeType>(AppOpcode::WriteRequest),
						(*flags & 1) != 0, *path_selection);
				}
				case AppOpcode::Mount: {
					auto mount = StormByte::Serializable<Mount>::Deserialize(bytes);
					return mount ? StormByte::Network::PacketPointer::MakePointer<MountPacket>(std::move(*mount)) : nullptr;
				}
				case AppOpcode::Unauthorized:
					return bytes.empty() ? StormByte::Network::PacketPointer::MakePointer<UnauthorizedPacket>() : nullptr;
			}
			return nullptr;
		};
	}

	/**
	 * @brief Accumulate the borrowed input until EOF or a read failure.
	 * @param input Borrowed input stream.
	 * @param bytes Destination for all successfully read bytes.
	 */
	void ReadPipeUntilEoF(const Buf::PipeInput& input, StormByte::BinaryData& bytes) {
		while (!input.EoF() && input.IsReadable()) {
			StormByte::BinaryData chunk;
			if (!input.Read(StormByte::ByteSize{1}, chunk))
				break;
			bytes.append(chunk);
			const auto available = input.Available();
			if (available > StormByte::ByteSize{0}) {
				StormByte::BinaryData rest;
				if (!input.Read(available, rest))
					break;
				bytes.append(rest);
			}
		}
	}

	/**
	 * @brief XOR transform callable for complete remote-file messages.
	 */
	struct XorPipe final {
		public:
			/**
			 * @brief Transform all input bytes and close the output.
			 * @param input Borrowed input stream.
			 * @param output Borrowed output stream.
			 * @param log Borrowed logger, unused by this transform.
			 */
			void operator()(const Buf::PipeInput& input, const Buf::PipeOutput& output,
				const StormByte::Safe::Shared<Log>& log) const {
				(void)log;
				StormByte::BinaryData bytes;
				ReadPipeUntilEoF(input, bytes);
				for (auto& byte: bytes)
					byte ^= std::byte{0x67};
				if (!bytes.empty())
					(void)output.Write(std::move(bytes));
				output.Close();
			}
	};

	/**
	 * @brief Encode or verify the reversible envelope used by remote-file tests.
	 */
	struct ReversibleEnvelopePipe final {
		public:
			/**
			 * @brief Select envelope encoding or decoding.
			 * @param encode Whether to encode rather than verify and decode.
			 */
			explicit ReversibleEnvelopePipe(bool encode):
				m_encode(encode) {
			}

			/**
			 * @brief Process a complete envelope, mark invalid input as failed, and close output.
			 * @param input Borrowed input stream.
			 * @param output Borrowed output stream.
			 * @param log Borrowed logger, unused by this transform.
			 */
			void operator()(const Buf::PipeInput& input, const Buf::PipeOutput& output,
				const StormByte::Safe::Shared<Log>& log) const {
				(void)log;
				StormByte::BinaryData bytes;
				ReadPipeUntilEoF(input, bytes);
				if (m_encode) {
					for (auto& byte: bytes)
						byte ^= std::byte{0x67};
					StormByte::BinaryData framed{std::byte{'A'}, std::byte{'E'}, std::byte{'A'}, std::byte{'D'}};
					framed.append(bytes);
					framed.append(StormByte::Serializable<std::uint64_t>(Tag(bytes)).Serialize());
					(void)output.Write(std::move(framed));
				}
				else if (bytes.size() >= 12 && bytes[0] == std::byte{'A'} && bytes[1] == std::byte{'E'}
					&& bytes[2] == std::byte{'A'} && bytes[3] == std::byte{'D'}) {
					const std::size_t body_size = bytes.size() - 12;
					const auto tag = StormByte::Serializable<std::uint64_t>::Deserialize(
						std::span<const std::byte>{bytes.data() + 4 + body_size, sizeof(std::uint64_t)});
					StormByte::BinaryData body(std::span<const std::byte>{bytes.data() + 4, body_size});
					if (tag && *tag == Tag(body)) {
						for (auto& byte: body)
							byte ^= std::byte{0x67};
						(void)output.Write(std::move(body));
					}
					else
						output.SetError();
				}
				else
					output.SetError();
				output.Close();
			}

		private:
			/**
			 * @brief Compute the test envelope integrity tag.
			 * @param bytes Encoded body bytes.
			 * @return Integrity tag for the supplied body.
			 */
			static std::uint64_t Tag(const StormByte::BinaryData& bytes) noexcept {
				std::uint64_t hash = 1469598103934665603ull;
				for (std::byte byte: bytes)
					hash = (hash ^ std::to_integer<std::uint8_t>(byte)) * 1099511628211ull;
				return hash;
			}

			/**
			 * @brief Whether this callable encodes rather than decodes envelopes.
			 */
			bool m_encode;
	};

	/**
	 * @brief Build the test input transformation pipeline.
	 * @param framed Whether to verify and decode test envelopes.
	 * @return Input pipeline for the selected encoding.
	 */
	Buf::Pipeline MakeInput(bool framed) {
		Buf::Pipeline pipeline;
		if (framed)
			pipeline.Add(Buf::Pipe{ReversibleEnvelopePipe{false}});
		else
			pipeline.Add(Buf::Pipe{XorPipe{}});
		return pipeline;
	}

	/**
	 * @brief Build the test output transformation pipeline.
	 * @param framed Whether to encode test envelopes.
	 * @return Output pipeline for the selected encoding.
	 */
	Buf::Pipeline MakeOutput(bool framed) {
		Buf::Pipeline pipeline;
		if (framed)
			pipeline.Add(Buf::Pipe{ReversibleEnvelopePipe{true}});
		else
			pipeline.Add(Buf::Pipe{XorPipe{}});
		return pipeline;
	}

	/**
	 * @brief Test control client with selectable transformation pipelines.
	 */
	class Client final: public Net::Client {
		public:
			/**
			 * @brief Construct a client using the test packet factory and logger.
			 */
			Client():
				Net::Client(Factory(), Logger()) {
			}

			/**
			 * @brief Install fixture transforms explicitly on the newly connected session.
			 * @param protocol Address family.
			 * @param address Remote address.
			 * @param port Remote port.
			 * @return Whether transport and one-time configuration succeeded.
			 */
			bool Connect(const Net::Connection::Protocol& protocol, std::string_view address,
				const unsigned short& port) override {
				if (!Net::Client::Connect(protocol, address, port))
					return false;
				if (ConfigurePipelines(InputPipeline(), OutputPipeline()))
					return true;
				Disconnect();
				return false;
			}

			/**
			 * @brief Build the selected input pipeline.
			 * @return Input transformation pipeline.
			 */
			Buf::Pipeline InputPipeline() const noexcept {
				return MakeInput(m_framed.load());
			}

			/**
			 * @brief Build the selected output pipeline.
			 * @return Output transformation pipeline.
			 */
			Buf::Pipeline OutputPipeline() const noexcept {
				return MakeOutput(m_framed.load());
			}

			/**
			 * @brief Select whether to use test envelopes.
			 * @param enabled Whether envelope framing is enabled.
			 */
			void UseFramedPipeline(bool enabled) noexcept {
				m_framed.store(enabled);
			}

			/**
			 * @brief Request a mount through the application control connection.
			 * @param write Whether to request write access.
			 * @param missing Whether to select a missing read path.
			 * @param path_selection Fixture path selector.
			 * @return Mount descriptor and application authorization rejection flag.
			 */
			std::pair<Mount, bool> RequestMount(bool write, bool missing = false, std::uint16_t path_selection = 0) {
				Request request{write, missing, path_selection};
				auto response = Send(request);
				if (auto result = StormByte::Safe::DynamicPointerCast<MountPacket>(response))
					return {result->Value(), false};
				return {Mount::Failed(), StormByte::Safe::DynamicPointerCast<UnauthorizedPacket>(response) != nullptr};
			}

			/**
			 * @brief Create a reader for a mount descriptor.
			 * @param mount Reader mount descriptor.
			 * @return Remote reader handle.
			 */
			Net::RemoteFileReaderHandle AttachReader(const Mount& mount) {
				return CreateRemoteFileReader(mount);
			}

			/**
			 * @brief Create a writer for a mount descriptor.
			 * @param mount Writer mount descriptor.
			 * @return Remote writer handle.
			 */
			Net::RemoteFileWriterHandle AttachWriter(const Mount& mount) {
				return CreateRemoteFileWriter(mount);
			}

		private:
			/**
			 * @brief Whether the client uses test envelopes.
			 */
			std::atomic<bool> m_framed{false};
	};

	/**
	 * @brief Test server that mounts fixture paths with configurable access.
	 */
	class Server final: public Net::Server {
		public:
			/**
			 * @brief Configure fixture transformations separately for each admitted UUID.
			 * @param uuid Application session identity.
			 * @return Whether configuration succeeded.
			 */
			bool OnClientConnected(std::string_view uuid) noexcept override {
				return ConfigureClientPipelines(uuid, InputPipeline(), OutputPipeline());
			}

			/**
			 * @brief Construct a server for the primary fixture paths.
			 * @param read_path Primary reader fixture path.
			 * @param write_path Primary writer fixture path.
			 */
			Server(std::filesystem::path read_path, std::filesystem::path write_path):
				Net::Server(Factory(), Logger()),
				m_read_path(std::move(read_path)),
				m_write_path(std::move(write_path)) {
			}

			/**
			 * @brief Whether reader mounts are authorized.
			 */
			std::atomic<bool> allow_read{true};

			/**
			 * @brief Whether writer mounts are authorized.
			 */
			std::atomic<bool> allow_write{true};

			/**
			 * @brief Select whether to use test envelopes.
			 * @param enabled Whether envelope framing is enabled.
			 */
			void UseFramedPipeline(bool enabled) noexcept {
				m_framed.store(enabled);
			}

			/**
			 * @brief Set additional reader fixture paths.
			 * @param paths Fixture paths selected by indices starting at three.
			 */
			void SetExtraPaths(std::vector<std::filesystem::path> paths) {
				m_extra_paths = std::move(paths);
			}

			/**
			 * @brief Build the selected input pipeline.
			 * @return Input transformation pipeline.
			 */
			Buf::Pipeline InputPipeline() const noexcept {
				return MakeInput(m_framed.load());
			}

			/**
			 * @brief Build the selected output pipeline.
			 * @return Output transformation pipeline.
			 */
			Buf::Pipeline OutputPipeline() const noexcept {
				return MakeOutput(m_framed.load());
			}

		private:
			/**
			 * @brief Authorize a request and mount its selected fixture path.
			 * @param uuid Requesting client identifier.
			 * @param packet Application request packet.
			 * @return Mount response, authorization rejection, or null for other packets.
			 */
			Net::PacketPointer ProcessClientPacket(std::string_view uuid, Net::PacketPointer packet) noexcept override {
				auto request = StormByte::Safe::DynamicPointerCast<Request>(packet);
				if (!request)
					return nullptr;
				const bool write = packet->Opcode() == static_cast<Packet::OpcodeType>(AppOpcode::WriteRequest);
				if ((write && !allow_write.load()) || (!write && !allow_read.load()))
					return StormByte::Network::PacketPointer::MakePointer<UnauthorizedPacket>();
				const std::filesystem::path* selected_path = nullptr;
				if (!write && request->PathSelection() >= 3
					&& static_cast<std::size_t>(request->PathSelection() - 3) < m_extra_paths.size())
					selected_path = &m_extra_paths[request->PathSelection() - 3];
				const auto& path = selected_path ? *selected_path : request->Missing() && !write
					? m_read_path.parent_path() / "absent-remote-file.bin"
					: request->PathSelection() == 1 ? m_read_path
					: request->PathSelection() == 2 ? m_write_path
					: (write ? m_write_path : m_read_path);
				Mount mount = write ? MountRemoteFileWriter(uuid, path, 3) : MountRemoteFileReader(uuid, path, 3);
				return StormByte::Network::PacketPointer::MakePointer<MountPacket>(std::move(mount));
			}

			/**
			 * @brief Primary reader fixture path.
			 */
			std::filesystem::path m_read_path;

			/**
			 * @brief Primary writer fixture path.
			 */
			std::filesystem::path m_write_path;

			/**
			 * @brief Additional reader fixture paths.
			 */
			std::vector<std::filesystem::path> m_extra_paths;

			/**
			 * @brief Whether the server uses test envelopes.
			 */
			std::atomic<bool> m_framed{false};
	};

	/**
	 * @brief Own a temporary directory for test fixtures.
	 */
	class TempDirectory final {
		public:
			/**
			 * @brief Create a temporary fixture directory.
			 */
			TempDirectory() {
				m_path = std::filesystem::temp_directory_path() / ("stormbyte-rfile-" + std::to_string(
					std::chrono::steady_clock::now().time_since_epoch().count()));
				std::filesystem::create_directories(m_path);
			}

			/**
			 * @brief Remove the fixture directory without reporting cleanup errors.
			 */
			~TempDirectory() {
				std::error_code ignored;
				std::filesystem::remove_all(m_path, ignored);
			}

			/**
			 * @brief Retrieve the fixture directory path.
			 * @return Owned directory path.
			 */
			const std::filesystem::path& Path() const noexcept {
				return m_path;
			}

		private:
			/**
			 * @brief Owned fixture directory path.
			 */
			std::filesystem::path m_path;
	};

	void WriteFile(const std::filesystem::path& path, const std::string_view contents) {
		std::ofstream file(path, std::ios::binary | std::ios::trunc);
		file.write(contents.data(), static_cast<std::streamsize>(contents.size()));
		Check(static_cast<bool>(file), "failed writing fixture");
	}

	std::string ReadFile(const std::filesystem::path& path) {
		std::ifstream file(path, std::ios::binary);
		return {std::istreambuf_iterator<char>{file}, std::istreambuf_iterator<char>{}};
	}

	StormByte::BinaryData MakePattern(const std::size_t size) {
		StormByte::BinaryData bytes{StormByte::ByteSize{size}};
		for (std::size_t index = 0; index < size; ++index)
			bytes[index] = static_cast<std::byte>(index % 251);
		return bytes;
	}

	void WriteBytes(const std::filesystem::path& path, const StormByte::BinaryData& bytes) {
		std::ofstream file(path, std::ios::binary | std::ios::trunc);
		file.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
		Check(static_cast<bool>(file), "failed writing binary fixture");
	}

	StormByte::BinaryData ReadBytes(const std::filesystem::path& path) {
		std::ifstream file(path, std::ios::binary | std::ios::ate);
		Check(static_cast<bool>(file), "failed opening binary fixture");
		const std::streamsize length = file.tellg();
		Check(length >= 0, "failed measuring binary fixture");
		file.seekg(0, std::ios::beg);
		StormByte::BinaryData bytes{StormByte::ByteSize{static_cast<std::size_t>(length)}};
		if (length > 0)
			file.read(reinterpret_cast<char*>(bytes.data()), length);
		Check(static_cast<bool>(file), "failed reading binary fixture");
		return bytes;
	}

	bool EqualBytes(const StormByte::BinaryData& actual, const StormByte::BinaryData& expected) {
		return actual.size() == expected.size() && std::equal(actual.begin(), actual.end(), expected.begin());
	}

	void CheckNetworkDevice(const StormByte::Safe::Shared<StormByte::System::Device>& device) {
		Check(device && device->Throughput().read_bps > StormByte::ByteSize{0}
			&& device->Throughput().write_bps > StormByte::ByteSize{0}
			&& device->Window().read > StormByte::ByteSize{0}
			&& device->Window().write > StormByte::ByteSize{0},
			"remote file must preserve its network device transfer characteristics");
	}

	void ExerciseReadAclAndCursors(Server& server, Client& client, const std::filesystem::path& read_path) {
		server.allow_read = false;
		auto [denied, unauthorized] = client.RequestMount(false);
		Check(unauthorized && denied.Result() == Mount::Status::Failed, "read ACL must return application Unauthorized");
		Check(denied.Port() == 0 && denied.Mode() == Mount::Access::None
			&& std::all_of(denied.Token().begin(), denied.Token().end(), [](std::byte byte) {
				return byte == std::byte{0};
			}),
			"ACL rejection exposed a port, capability, or access mode");
		server.allow_read = true;
		auto [missing, rejected_missing] = client.RequestMount(false, true);
		Check(!rejected_missing && missing.Result() == Mount::Status::Unavailable, "missing read path status mismatch");
		Check(missing.Port() == 0 && missing.Mode() == Mount::Access::None
			&& std::all_of(missing.Token().begin(), missing.Token().end(), [](std::byte byte) {
				return byte == std::byte{0};
			}),
			"Unavailable mount exposed a port, capability, or access mode");

		auto [mount, rejected] = client.RequestMount(false);
		Check(!rejected && mount.Result() == Mount::Status::Authorized, "reader mount was not authorized");
		auto remote_reader = client.AttachReader(mount);
		const auto* reader_address = remote_reader.get();
		auto consume_reader = [](StormByte::Safe::Unique<Buf::IO::BufferedLocationReader> reader) {
			return reader;
		};
		auto first = consume_reader(std::move(remote_reader));
		Check(!remote_reader && first.get() == reader_address
			&& dynamic_cast<Net::BufferedRemoteFileReader*>(first.get()) != nullptr,
			"reader ownership transfer must preserve the remote leaf and its address");
		Check(first && first->Open(), "first reader open failed");
		CheckNetworkDevice(first->Device());
		const auto first_size = first->Size();
		Check(first_size && *first_size == StormByte::ByteSize{16}, "reader size mismatch");
		std::array<std::byte, 5> start{};
		Check(first->Read(std::span<std::byte>{start}).count == 5, "first read failed");
		Check(std::string(reinterpret_cast<const char*>(start.data()), 5) == "01234", "first read mismatch");
		Check(first->Seek(2, Buf::Position::Absolute).status == Buf::IO::Status::Ok, "reader seek failed");
		std::array<std::byte, 4> sought{};
		Check(first->Read(std::span<std::byte>{sought}).count == 4
			&& std::string(reinterpret_cast<const char*>(sought.data()), 4) == "2345", "seek/read mismatch");

		auto [write_conflict, rejected_writer] = client.RequestMount(true, false, 1);
		Check(!rejected_writer && write_conflict.Result() == Mount::Status::FileBeingRead,
			"writer against active readers must report FileBeingRead");
		auto [second_mount, rejected_second] = client.RequestMount(false);
		Check(!rejected_second && second_mount.Result() == Mount::Status::Authorized, "second reader was not authorized");
		auto second = client.AttachReader(second_mount);
		Check(second && second->Open(), "second reader open failed");
		std::array<std::byte, 2> independent{};
		Check(second->Read(std::span<std::byte>{independent}).count == 2
			&& std::string(reinterpret_cast<const char*>(independent.data()), 2) == "01", "reader cursor was shared");

		std::array<std::byte, 2> a{};
		std::array<std::byte, 2> b{};
		std::thread one([&] {
			(void)first->Read(std::span<std::byte>{a});
		});
		std::thread two([&] {
			(void)second->Read(std::span<std::byte>{b});
		});
		one.join();
		two.join();
		Check(std::string(reinterpret_cast<const char*>(a.data()), 2) == "67"
			&& std::string(reinterpret_cast<const char*>(b.data()), 2) == "23", "concurrent reads mismatch");
		Check(second->Close().status == Buf::IO::Status::Ok, "reader close failed");
		first.reset();
		second.reset();
		auto [released_mount, released_rejected] = client.RequestMount(true, false, 1);
		Check(!released_rejected && released_mount.Result() == Mount::Status::Authorized,
			"reader destruction through the base owner must release the path reservation");
		auto released_writer = client.AttachWriter(released_mount);
		Check(released_writer && released_writer->Close(), "released reader path cleanup failed");
		Check(ReadFile(read_path) == "0123456789ABCDEF", "reader modified fixture file");
	}

	void ExerciseWriterAndConflict(Server& server, Client& client, const std::filesystem::path& path) {
		server.allow_write = false;
		auto [denied, unauthorized] = client.RequestMount(true);
		Check(unauthorized && denied.Result() == Mount::Status::Failed, "write ACL must return application Unauthorized");
		server.allow_write = true;
		auto [mount, rejected] = client.RequestMount(true);
		Check(!rejected && mount.Result() == Mount::Status::Authorized, "writer mount was not authorized");
		auto remote_writer = client.AttachWriter(mount);
		const auto* writer_address = remote_writer.get();
		auto consume_writer = [](StormByte::Safe::Unique<Buf::IO::BufferedLocationWriter> writer) {
			return writer;
		};
		auto writer = consume_writer(std::move(remote_writer));
		Check(!remote_writer && writer.get() == writer_address
			&& dynamic_cast<Net::BufferedRemoteFileWriter*>(writer.get()) != nullptr,
			"writer ownership transfer must preserve the remote leaf and its address");
		Check(writer && writer->Open(), "writer open failed");
		CheckNetworkDevice(writer->Device());
		auto [second_writer, second_rejected] = client.RequestMount(true);
		Check(!second_rejected && second_writer.Result() == Mount::Status::FileBeingWritten,
			"second writer must report FileBeingWritten");
		auto [reader_conflict, reader_rejected] = client.RequestMount(false, false, 2);
		Check(!reader_rejected && reader_conflict.Result() == Mount::Status::FileBeingWritten,
			"reader against writer must report FileBeingWritten");

		Check(writer->Truncate().status == Buf::IO::Status::Ok, "truncate failed");
		const std::string original = "ABCDEFGHIJ";
		Check(writer->Write(std::as_bytes(std::span{original.data(), original.size()})).status == Buf::IO::Status::Ok,
			"sequential write failed");
		Check(writer->Seek(4, Buf::Position::Absolute).status == Buf::IO::Status::Ok, "writer seek failed");
		const std::string patch = "xy";
		Check(writer->Write(std::as_bytes(std::span{patch.data(), patch.size()})).status == Buf::IO::Status::Ok,
			"overwrite failed");
		Check(writer->Flush().status == Buf::IO::Status::Ok, "flush failed");
		writer.reset();
		Check(ReadFile(path) == "ABCDxyGHIJ", "writer deterministic result mismatch after close");
		auto [released_mount, released_rejected] = client.RequestMount(true);
		Check(!released_rejected && released_mount.Result() == Mount::Status::Authorized,
			"writer destruction through the base owner must release the path reservation");
		auto released_writer = client.AttachWriter(released_mount);
		Check(released_writer && released_writer->Close(), "released writer path cleanup failed");
	}

	void ExerciseSharedPipelineAndDisconnect(Server&, Client& client,
		const std::filesystem::path& read_path, const std::filesystem::path& write_path) {
		auto [read_mount, read_rejected] = client.RequestMount(false);
		Check(!read_rejected && read_mount.Result() == Mount::Status::Authorized, "framed-pipeline read mount failed");
		auto reader = client.AttachReader(read_mount);
		Check(reader && reader->Open(), "private channel failed to clone the normal endpoint pipelines");
		std::array<std::byte, 3> bytes{};
		Check(reader->Read(std::span<std::byte>{bytes}).count == 3
			&& std::string(reinterpret_cast<const char*>(bytes.data()), 3) == "012", "expanded pipeline decode failed");

		auto [write_mount, write_rejected] = client.RequestMount(true);
		Check(!write_rejected && write_mount.Result() == Mount::Status::Authorized, "disconnect writer mount failed");
		auto writer = client.AttachWriter(write_mount);
		Check(writer && writer->Open(), "disconnect writer open failed");
		Check(writer->Truncate().status == Buf::IO::Status::Ok, "disconnect writer truncate failed");
		const std::string before = "persistent-";
		const std::string after = "after-disconnect";
		Check(writer->Write(std::as_bytes(std::span{before.data(), before.size()})).status == Buf::IO::Status::Ok,
			"pre-disconnect write failed");

		client.Disconnect();
		std::array<std::byte, 3> after_app_close{};
		Check(reader->Read(std::span<std::byte>{after_app_close}).count == 3
			&& std::string(reinterpret_cast<const char*>(after_app_close.data()), 3) == "345",
			"reader did not survive application connection disconnect");
		Check(writer->Write(std::as_bytes(std::span{after.data(), after.size()})).status == Buf::IO::Status::Ok,
			"writer did not survive application connection disconnect");
		Check(reader->Close().status == Buf::IO::Status::Ok, "reader close failed after app disconnect");
		Check(writer->Close(), "writer close failed after app disconnect");
		reader.reset();
		writer.reset();
		Check(ReadFile(write_path) == before + after, "file bytes after detached-handle close mismatch");
		Check(ReadFile(read_path) == "0123456789ABCDEF", "read file changed");
	}

	void ExerciseTransportFailureMarksReaderFault(const std::filesystem::path& read_path,
		const std::filesystem::path& write_path) {
		Server server(read_path, write_path);
		Check(server.Connect(Net::Connection::Protocol::IPv4, address, port), "timeout server start failed");
		Client client;
		Check(client.Connect(Net::Connection::Protocol::IPv4, address, port), "timeout client connect failed");
		auto [mount, rejected] = client.RequestMount(false);
		Check(!rejected && mount.Result() == Mount::Status::Authorized, "timeout mount failed");
		auto reader = client.AttachReader(mount);
		Check(reader && reader->Open(), "timeout reader open failed");
		server.Disconnect();
		std::array<std::byte, 1> byte{};
		(void)reader->Read(std::span<std::byte>{byte});
		Check(reader->State() == Buf::IO::State::Fault, "dead remote channel did not mark BufferedLocationReader Fault");
		reader.reset();
	}
	void ExercisePatternReader(Client& client, const std::filesystem::path& path) {
		const StormByte::BinaryData expected = ReadBytes(path);
		const StormByte::ByteSize length{std::filesystem::file_size(path)};
		auto [mount, rejected] = client.RequestMount(false, false, 1);
		Check(!rejected && mount.Result() == Mount::Status::Authorized, "pattern read mount failed");
		auto reader = client.AttachReader(mount);
		Check(reader && reader->Open(), "pattern reader open failed");
		const auto reader_size = reader->Size();
		Check(reader_size && *reader_size == length, "remote Size differs from filesystem::file_size");
		StormByte::BinaryData whole{length};
		const auto full = reader->Read(std::span<std::byte>{whole.data(), whole.size()});
		Check(full.count == length && EqualBytes(whole, expected), "full read differed from the deterministic fixture");

		constexpr std::size_t middle_offset = 456789;
		std::array<std::byte, 257> middle{};
		Check(reader->Seek(static_cast<std::ptrdiff_t>(middle_offset), Buf::Position::Absolute).status == Buf::IO::Status::Ok,
			"middle logical seek failed");
		const auto middle_read = reader->Read(std::span<std::byte>{middle});
		Check(middle_read.count == middle.size()
			&& std::equal(middle.begin(), middle.end(), expected.begin() + static_cast<std::ptrdiff_t>(middle_offset)),
			"middle block differed from the fixture");

		constexpr std::size_t final_size = 37;
		const std::size_t final_offset = expected.size() - final_size;
		std::array<std::byte, 64> final_block{};
		Check(reader->Seek(static_cast<std::ptrdiff_t>(final_offset), Buf::Position::Absolute).status == Buf::IO::Status::Ok,
			"final-block logical seek failed");
		const auto final_read = reader->Read(std::span<std::byte>{final_block});
		Check(final_read.count == StormByte::ByteSize{final_size} && final_read.status == Buf::IO::Status::End
			&& std::equal(final_block.begin(), final_block.begin() + static_cast<std::ptrdiff_t>(final_size),
				expected.begin() + static_cast<std::ptrdiff_t>(final_offset)),
			"short final block or End status was incorrect");

		Check(reader->Seek(static_cast<std::ptrdiff_t>(expected.size() + 8), Buf::Position::Absolute).status == Buf::IO::Status::Ok,
			"past-EOF logical seek failed");
		std::array<std::byte, 1> past_eof{};
		const auto eof_read = reader->Read(std::span<std::byte>{past_eof});
		Check(eof_read.count == StormByte::ByteSize{0} && eof_read.status == Buf::IO::Status::End
			&& reader->State() != Buf::IO::State::Fault, "past-EOF read faulted");
		Check(reader->Close().status == Buf::IO::Status::Ok, "pattern reader CloseToken failed");
	}

	void ExerciseEightReaders(Client& client, const std::filesystem::path& path) {
		const StormByte::BinaryData expected = ReadBytes(path);
		std::array<Net::RemoteFileReaderHandle, 8> readers;
		std::array<StormByte::Safe::Shared<StormByte::System::Device>, 8> devices;
		for (std::size_t index = 0; index < readers.size(); ++index) {
			auto [mount, rejected] = client.RequestMount(false, false, 1);
			Check(!rejected && mount.Result() == Mount::Status::Authorized, "interleaved reader mount failed");
			readers[index] = client.AttachReader(mount);
			Check(readers[index] && readers[index]->Open(), "interleaved reader open failed");
			devices[index] = readers[index]->Device();
			CheckNetworkDevice(devices[index]);
		}
		for (std::size_t index = 1; index < readers.size(); ++index)
			Check(devices[index]->Throughput().read_bps == devices[0]->Throughput().read_bps
				&& devices[index]->Throughput().write_bps == devices[0]->Throughput().write_bps
				&& devices[index]->Window().read == devices[0]->Window().read
				&& devices[index]->Window().write == devices[0]->Window().write,
				"one Client's leaves did not share the same device snapshot");
		for (std::size_t round = 0; round < 4; ++round) {
			for (std::size_t index = 0; index < readers.size(); ++index) {
				const std::size_t offset = index * 4096 + round * 16;
				std::array<std::byte, 16> bytes{};
				Check(readers[index]->Seek(static_cast<std::ptrdiff_t>(offset), Buf::Position::Absolute).status
					== Buf::IO::Status::Ok, "interleaved logical seek failed");
				const auto read = readers[index]->Read(std::span<std::byte>{bytes});
				Check(read.count == bytes.size()
					&& std::equal(bytes.begin(), bytes.end(), expected.begin() + static_cast<std::ptrdiff_t>(offset)),
					"interleaved reader got bytes from a different offset");
			}
		}
		for (auto& reader: readers)
			Check(reader->Close().status == Buf::IO::Status::Ok,
				"interleaved reader CloseToken failed");
	}

	void ExerciseTokenFaultIsolation(Client& client, const StormByte::BinaryData& expected) {
		auto [mount, rejected] = client.RequestMount(false);
		Check(!rejected && mount.Result() == Mount::Status::Authorized, "token-isolation mount failed");
		auto reader = client.AttachReader(mount);
		Check(reader && reader->Open(), "token-isolation reader open failed");

		StormByte::BinaryData forged_bytes = StormByte::Serializable<Mount>(mount).Serialize();
		forged_bytes.back() ^= std::byte{0x01};
		auto forged = StormByte::Serializable<Mount>::Deserialize(forged_bytes);
		Check(forged && !client.AttachReader(*forged), "unknown mount capability was registered");

		StormByte::BinaryData writer_bytes = StormByte::Serializable<Mount>(mount).Serialize();
		writer_bytes[11] = static_cast<std::byte>(Mount::Access::Write);
		auto forged_writer_mount = StormByte::Serializable<Mount>::Deserialize(writer_bytes);
		Check(static_cast<bool>(forged_writer_mount), "forged writer descriptor did not deserialize");
		auto writer = client.AttachWriter(*forged_writer_mount);
		Check(writer && writer->Open(), "forged writer leaf did not open");
		const std::array<std::byte, 1> attack{std::byte{0xFF}};
		const auto attack_write = writer->Write(std::span<const std::byte>{attack});
		if (writer->State() != Buf::IO::State::Fault && attack_write.status == Buf::IO::Status::Ok)
			(void)writer->Flush();
		Check(writer->State() == Buf::IO::State::Fault,
			"reader token write did not fail visibly when its buffered operation reached the host");
		writer.reset();
		reader.reset();

		auto [fresh_mount, fresh_rejected] = client.RequestMount(false);
		Check(!fresh_rejected && fresh_mount.Result() == Mount::Status::Authorized,
			"valid token could not be mounted after a token-scoped fault");
		auto fresh_reader = client.AttachReader(fresh_mount);
		Check(fresh_reader && fresh_reader->Open(), "fresh token failed on the existing plane");
		std::array<std::byte, 16> bytes{};
		const auto read = fresh_reader->Read(std::span<std::byte>{bytes});
		Check(read.count == bytes.size() && std::equal(bytes.begin(), bytes.end(), expected.begin()),
			"one token fault terminated other operations on the plane");
		Check(fresh_reader->Close().status == Buf::IO::Status::Ok, "fresh token CloseToken failed");
	}

	void ExercisePatternWriter(Client& client, const std::filesystem::path& path) {
		StormByte::BinaryData expected = MakePattern(1024 * 1024);
		WriteBytes(path, StormByte::BinaryData{});
		auto [mount, rejected] = client.RequestMount(true);
		Check(!rejected && mount.Result() == Mount::Status::Authorized, "pattern writer mount failed");
		auto writer = client.AttachWriter(mount);
		Check(writer && writer->Open(), "pattern writer open failed");
		Check(writer->Truncate().status == Buf::IO::Status::Ok, "pattern Truncate failed");
		const auto written = writer->Write(std::span<const std::byte>{expected.data(), expected.size()});
		Check(written.status == Buf::IO::Status::Ok && written.count == expected.size(),
			"full pattern write failed or accepted an unexpected byte count");
		const auto flush = writer->Flush();
		Check(flush.status == Buf::IO::Status::Ok, "pattern Flush returned status "
			+ std::to_string(static_cast<unsigned int>(flush.status)) + " with writer state "
			+ std::to_string(static_cast<unsigned int>(writer->State())));
		const StormByte::BinaryData flushed = ReadBytes(path);
		Check(flushed.size() == expected.size(), "pattern Flush left an unexpected file size: "
			+ std::to_string(static_cast<std::size_t>(flushed.size())) + " instead of "
			+ std::to_string(static_cast<std::size_t>(expected.size())));
		const auto mismatch = std::mismatch(flushed.begin(), flushed.end(), expected.begin());
		Check(mismatch.first == flushed.end(), "pattern Flush left incorrect bytes at offset "
			+ std::to_string(static_cast<std::size_t>(std::distance(flushed.begin(), mismatch.first))));

		const StormByte::BinaryData start_patch = MakePattern(23);
		Check(writer->Seek(0, Buf::Position::Absolute).status == Buf::IO::Status::Ok
			&& writer->Write(std::span<const std::byte>{start_patch.data(), start_patch.size()}).status == Buf::IO::Status::Ok,
			"offset-zero overwrite failed");
		std::copy(start_patch.begin(), start_patch.end(), expected.begin());
		constexpr std::size_t patch_offset = 500003;
		const StormByte::BinaryData patch = MakePattern(513);
		Check(writer->Seek(static_cast<StormByte::ByteSize>(patch_offset), Buf::Position::Absolute).status
			== Buf::IO::Status::Ok, "middle writer seek failed");
		Check(writer->Write(std::span<const std::byte>{patch.data(), patch.size()}).status == Buf::IO::Status::Ok,
			"middle overwrite failed");
		std::copy(patch.begin(), patch.end(), expected.begin() + static_cast<std::ptrdiff_t>(patch_offset));
		Check(writer->Flush().status == Buf::IO::Status::Ok && EqualBytes(ReadBytes(path), expected),
			"patched bytes differed from disk");
		Check(writer->Close(), "writer CloseToken failed");
		writer.reset();
		Check(EqualBytes(ReadBytes(path), expected), "closed writer contents differed from expected bytes");

		auto [truncate_mount, truncate_rejected] = client.RequestMount(true);
		Check(!truncate_rejected && truncate_mount.Result() == Mount::Status::Authorized, "truncate remount failed");
		auto truncate_writer = client.AttachWriter(truncate_mount);
		Check(truncate_writer && truncate_writer->Open(), "truncate writer open failed");
		Check(truncate_writer->Truncate().status == Buf::IO::Status::Ok
			&& std::filesystem::file_size(path) == 0, "Truncate did not reduce the host file to zero");
		Check(truncate_writer->Close(), "truncate CloseToken failed");
		truncate_writer.reset();
		Check(std::filesystem::remove(path), "host file could not be removed after CloseToken");
	}

	void ExerciseRemotePlaneScale(const std::filesystem::path& read_path,
		const std::filesystem::path& write_path, const std::filesystem::path& root) {
		constexpr std::size_t peer_count = 16;
		constexpr std::size_t readers_per_peer = 8;
		std::vector<std::filesystem::path> paths;
		std::vector<StormByte::BinaryData> expected;
		paths.reserve(peer_count * readers_per_peer);
		expected.reserve(peer_count * readers_per_peer);
		for (std::size_t index = 0; index < peer_count * readers_per_peer; ++index) {
			paths.push_back(root / ("scale-" + std::to_string(index) + ".bin"));
			expected.push_back(MakePattern(64));
			WriteBytes(paths.back(), expected.back());
		}

		Server server(read_path, write_path);
		server.SetExtraPaths(paths);
		Check(server.Connect(Net::Connection::Protocol::IPv4, address, port), "scale server connect failed");
		std::vector<std::unique_ptr<Client>> clients;
		std::vector<Net::RemoteFileReaderHandle> readers;
		std::set<unsigned short> plane_ports;
		clients.reserve(peer_count);
		readers.reserve(peer_count * readers_per_peer);
		for (std::size_t peer = 0; peer < peer_count; ++peer) {
			auto client = std::make_unique<Client>();
			Check(client->Connect(Net::Connection::Protocol::IPv4, address, port), "scale client connect failed");
			std::optional<unsigned short> peer_port;
			StormByte::ByteSize expected_read_bps{0};
			StormByte::ByteSize expected_write_bps{0};
			for (std::size_t index = 0; index < readers_per_peer; ++index) {
				const std::size_t path_index = peer * readers_per_peer + index;
				const auto selection = static_cast<std::uint16_t>(path_index + 3);
				auto [mount, rejected] = client->RequestMount(false, false, selection);
				Check(!rejected && mount.Result() == Mount::Status::Authorized, "scale reader mount failed");
					if (!peer_port)
					peer_port = mount.Port();
				Check(mount.Port() == *peer_port, "mounts from one Client did not share a data-plane port");
				auto reader = client->AttachReader(mount);
				Check(reader && reader->Open(), "scale reader open failed");
				const auto reader_size = reader->Size();
				Check(reader_size && *reader_size == StormByte::ByteSize{64}, "scale reader size mismatch");
				auto device = reader->Device();
				CheckNetworkDevice(device);
				if (expected_read_bps == StormByte::ByteSize{0}) {
					expected_read_bps = device->Throughput().read_bps;
					expected_write_bps = device->Throughput().write_bps;
				}
				Check(device->Throughput().read_bps == expected_read_bps
					&& device->Throughput().write_bps == expected_write_bps,
					"one Client's leaves did not share the same network snapshot");
				std::array<std::byte, 16> bytes{};
				const auto result = reader->Read(std::span<std::byte>{bytes});
				Check(result.count == bytes.size()
					&& std::equal(bytes.begin(), bytes.end(), expected[path_index].begin()),
					"scale reader did not read its distinct path bytes");
				readers.push_back(std::move(reader));
			}
			plane_ports.insert(*peer_port);
			clients.push_back(std::move(client));
		}
		Check(plane_ports.size() == peer_count, "expected one data-plane listener per peer");
		for (auto& reader: readers)
			Check(reader->Close().status == Buf::IO::Status::Ok,
				"scale CloseToken failed");
		readers.clear();
		clients.clear();
		server.Disconnect();
	}

	void SendRawAttach(RawPeer& peer, const Mount::ChannelToken& token) {
		Check(peer.SendMessage(1, 1, token), "raw Attach send failed");
		const StormByte::BinaryData response = peer.ReceiveMessage();
		Check(response.size() == remote_header_size && std::to_integer<std::uint8_t>(response[0]) == 1
			&& std::to_integer<std::uint8_t>(response[1]) == 0, "raw peer plane Attach failed");
	}

	void CheckRawResponse(const StormByte::BinaryData& response, const std::uint8_t opcode,
		const std::uint64_t request_id, const std::uint8_t status) {
		Check(response.size() >= remote_header_size, "short raw response header");
		const auto actual_opcode = StormByte::Serializable<std::uint8_t>::Deserialize(
			std::span<const std::byte>{response.data(), sizeof(std::uint8_t)});
		const auto actual_status = StormByte::Serializable<std::uint8_t>::Deserialize(
			std::span<const std::byte>{response.data() + 1, sizeof(std::uint8_t)});
		const auto actual_id = StormByte::Serializable<std::uint64_t>::Deserialize(
			std::span<const std::byte>{response.data() + 2, sizeof(std::uint64_t)});
		Check(actual_opcode && *actual_opcode == opcode && actual_status && *actual_status == status
			&& actual_id && *actual_id == request_id, "raw peer response fields mismatch");
	}

	enum class MalformedFrame {
		Opcode,
		RepeatedSequence,
		Truncated,
		Oversized
	};

	void ExerciseMalformedRawPlane(const MalformedFrame failure) {
		TempDirectory temporary;
		const auto read_path = temporary.Path() / "raw-read.bin";
		const auto write_path = temporary.Path() / "unused-write.bin";
		WriteFile(read_path, "0123456789ABCDEF");
		Server server(read_path, write_path);
		Check(server.Connect(Net::Connection::Protocol::IPv4, address, port), "raw-frame server connect failed");
		Client client;
		Check(client.Connect(Net::Connection::Protocol::IPv4, address, port), "raw-frame control connect failed");
		auto [mount, rejected] = client.RequestMount(false);
		Check(!rejected && mount.Result() == Mount::Status::Authorized, "raw-frame mount failed");
		RawPeer peer(mount.Port());
		Check(peer.Connected(), "raw data-plane connect failed");
		SendRawAttach(peer, mount.Token());

		switch (failure) {
			case MalformedFrame::Opcode:
				Check(peer.SendMessage(0xFF, 2, mount.Token()), "invalid-opcode send failed");
				break;
			case MalformedFrame::RepeatedSequence: {
				const Mount::ChannelToken heartbeat_token{};
				Check(peer.SendMessage(10, 2, heartbeat_token), "heartbeat send failed");
				CheckRawResponse(peer.ReceiveMessage(), 11, 2, 0);
				Check(peer.SendMessage(10, 2, heartbeat_token), "repeated sequence send failed");
				break;
			}
			case MalformedFrame::Truncated: {
				const std::array<std::byte, 2> partial{std::byte{0x67}, std::byte{0x67}};
				Check(peer.SendRawFramePrefix(100, partial), "truncated frame send failed");
				peer.ShutdownSend();
				break;
			}
			case MalformedFrame::Oversized:
				Check(peer.SendRawFramePrefix(remote_frame_limit + 1), "oversized frame prefix send failed");
				break;
		}
		Check(peer.WaitForClose(), "malformed peer frame did not close only its data plane");
		Check(client.Status() == Net::Connection::Status::Connected,
			"malformed private frame disconnected the application control session");
		auto [remount, remount_rejected] = client.RequestMount(false);
		Check(!remount_rejected && remount.Result() == Mount::Status::Failed,
			"a failed plane accepted new mounts before Client reconnect");
	}

	// -------------------
	// Access
	// -------------------
	void test_acl_and_independent_reader_cursors() {
		TempDirectory temporary;
		const auto read_path = temporary.Path() / "read.bin";
		const auto write_path = temporary.Path() / "write.bin";
		WriteFile(read_path, "0123456789ABCDEF");
		WriteFile(write_path, "old deterministic contents");
		Server server(read_path, write_path);
		Check(server.Connect(Net::Connection::Protocol::IPv4, address, port), "ACL server connect failed");
		Client client;
		Check(client.Connect(Net::Connection::Protocol::IPv4, address, port), "ACL client connect failed");
		ExerciseReadAclAndCursors(server, client, read_path);
	}

	void test_capability_isolation() {
		TempDirectory temporary;
		const auto read_path = temporary.Path() / "token-read.bin";
		const auto write_path = temporary.Path() / "unused-write.bin";
		const StormByte::BinaryData expected = MakePattern(4096);
		WriteBytes(read_path, expected);
		Server server(read_path, write_path);
		Check(server.Connect(Net::Connection::Protocol::IPv4, address, port), "token server connect failed");
		Client client;
		Check(client.Connect(Net::Connection::Protocol::IPv4, address, port), "token client connect failed");
		ExerciseTokenFaultIsolation(client, expected);
	}

	void test_foreign_and_forged_tokens_preserve_owner() {
		TempDirectory temporary;
		const auto read_path = temporary.Path() / "owner-read.bin";
		const auto write_path = temporary.Path() / "unused-write.bin";
		const auto expected = MakePattern(32);
		WriteBytes(read_path, expected);
		Server server(read_path, write_path);
		Check(server.Connect(Net::Connection::Protocol::IPv4, address, port), "owner server connect failed");
		Client owner;
		Client attacker;
		Check(owner.Connect(Net::Connection::Protocol::IPv4, address, port)
			&& attacker.Connect(Net::Connection::Protocol::IPv4, address, port), "owner/attacker control connect failed");
		auto [owned, owner_rejected] = owner.RequestMount(false);
		auto [allowed, attacker_rejected] = attacker.RequestMount(false);
		Check(!owner_rejected && !attacker_rejected && owned.Result() == Mount::Status::Authorized
			&& allowed.Result() == Mount::Status::Authorized && owned.Port() != allowed.Port(), "peer planes were not isolated");
		RawPeer peer(allowed.Port());
		Check(peer.Connected(), "attacker raw peer connect failed");
		auto forged = owned.Token();
		forged.front() ^= std::byte{0x80};
		std::uint64_t sequence = 1;
		for (const auto& token: std::array<Mount::ChannelToken, 3>{Mount::ChannelToken{}, forged, owned.Token()}) {
			Check(peer.SendMessage(1, sequence, token), "forged Attach send failed");
			const auto response = peer.ReceiveMessage();
			CheckRawResponse(response, 1, sequence++, 2);
			Check(response.size() == remote_header_size, "forged Attach leaked file data");
		}
		Check(peer.SendMessage(12, sequence, owned.Token()), "foreign CloseToken send failed");
		CheckRawResponse(peer.ReceiveMessage(), 12, sequence++, 2);
		auto reader = owner.AttachReader(owned);
		Check(reader && reader->Open(), "foreign-token attempts revoked the owner's capability");
		std::array<std::byte, 32> bytes{};
		const auto read = reader->Read(std::span<std::byte>{bytes});
		Check(read.count == bytes.size() && std::equal(bytes.begin(), bytes.end(), expected.begin()),
			"foreign-token attempts changed the owner's file bytes");
		Check(reader->Close().status == Buf::IO::Status::Ok, "owner Close failed after forged requests");
		Check(peer.SendMessage(1, sequence, allowed.Token()), "legitimate attacker-plane Attach send failed");
		CheckRawResponse(peer.ReceiveMessage(), 1, sequence++, 0);
		Check(peer.SendMessage(2, sequence, allowed.Token()), "legitimate attacker-plane Open send failed");
		CheckRawResponse(peer.ReceiveMessage(), 2, sequence++, 0);
		Check(peer.SendMessage(12, sequence, allowed.Token()), "attacker-plane CloseToken send failed");
		CheckRawResponse(peer.ReceiveMessage(), 12, sequence, 0);
	}

	void test_malformed_close_preserves_capability() {
		TempDirectory temporary;
		const auto read_path = temporary.Path() / "malformed-close-read.bin";
		const auto write_path = temporary.Path() / "unused-write.bin";
		WriteBytes(read_path, MakePattern(32));
		Server server(read_path, write_path);
		Check(server.Connect(Net::Connection::Protocol::IPv4, address, port), "malformed-close server connect failed");
		Client client;
		Check(client.Connect(Net::Connection::Protocol::IPv4, address, port), "malformed-close control connect failed");
		auto [mount, rejected] = client.RequestMount(false);
		Check(!rejected && mount.Result() == Mount::Status::Authorized, "malformed-close mount failed");
		RawPeer peer(mount.Port());
		Check(peer.Connected(), "malformed-close peer connect failed");
		SendRawAttach(peer, mount.Token());
		Check(peer.SendMessage(2, 2, mount.Token()), "malformed-close Open send failed");
		CheckRawResponse(peer.ReceiveMessage(), 2, 2, 0);
		const std::array<std::byte, 1> body{std::byte{0x5A}};
		std::uint64_t sequence = 3;
		for (std::size_t variant = 0; variant < 3; ++variant) {
			const auto data = variant == 2 ? std::span<const std::byte>{body} : std::span<const std::byte>{};
			Check(peer.SendMessage(12, sequence, mount.Token(), variant == 0 ? 1 : 0,
				variant == 1 ? 1 : 0, data), "malformed CloseToken send failed");
			CheckRawResponse(peer.ReceiveMessage(), 12, sequence++, 2);
			Check(peer.SendMessage(7, sequence, mount.Token()), "Size after malformed CloseToken send failed");
			const auto response = peer.ReceiveMessage();
			CheckRawResponse(response, 7, sequence++, 0);
			const auto size = StormByte::Serializable<std::uint64_t>::Deserialize(
				std::span<const std::byte>{response.data() + 18, sizeof(std::uint64_t)});
			Check(size && *size == 32, "malformed close changed file size");
		}
		Check(peer.SendMessage(12, sequence, mount.Token()), "valid CloseToken send failed");
		CheckRawResponse(peer.ReceiveMessage(), 12, sequence++, 0);
		auto [replacement, replacement_rejected] = client.RequestMount(true, false, 1);
		Check(!replacement_rejected && replacement.Result() == Mount::Status::Authorized,
			"malformed CloseToken leaked the reader reservation");
		Check(peer.SendMessage(1, sequence, replacement.Token()), "replacement Attach send failed");
		CheckRawResponse(peer.ReceiveMessage(), 1, sequence++, 0);
		Check(peer.SendMessage(12, sequence, replacement.Token()), "replacement CloseToken send failed");
		CheckRawResponse(peer.ReceiveMessage(), 12, sequence, 0);
		Check(std::filesystem::remove(read_path), "malformed CloseToken leaked an open file handle");
	}

	void test_read_capability_rejects_mutating_operations() {
		TempDirectory temporary;
		const auto read_path = temporary.Path() / "read-only.bin";
		const auto write_path = temporary.Path() / "unused-write.bin";
		const auto expected = MakePattern(32);
		WriteBytes(read_path, expected);
		Server server(read_path, write_path);
		Check(server.Connect(Net::Connection::Protocol::IPv4, address, port), "read-only server connect failed");
		Client client;
		Check(client.Connect(Net::Connection::Protocol::IPv4, address, port), "read-only control connect failed");
		auto [mount, rejected] = client.RequestMount(false);
		Check(!rejected && mount.Result() == Mount::Status::Authorized, "read-only mount failed");
		RawPeer peer(mount.Port());
		Check(peer.Connected(), "read-only raw peer connect failed");
		SendRawAttach(peer, mount.Token());
		Check(peer.SendMessage(2, 2, mount.Token()), "read-only Open send failed");
		CheckRawResponse(peer.ReceiveMessage(), 2, 2, 0);
		const std::array<std::byte, 3> patch{std::byte{0xFF}, std::byte{0xFF}, std::byte{0xFF}};
		Check(peer.SendMessage(5, 3, mount.Token(), 0, patch.size(), patch), "unauthorized Write send failed");
		CheckRawResponse(peer.ReceiveMessage(), 5, 3, 2);
		Check(peer.SendMessage(8, 4, mount.Token()), "unauthorized Flush send failed");
		CheckRawResponse(peer.ReceiveMessage(), 8, 4, 2);
		Check(peer.SendMessage(9, 5, mount.Token()), "unauthorized Truncate send failed");
		CheckRawResponse(peer.ReceiveMessage(), 9, 5, 2);
		Check(peer.SendMessage(4, 6, mount.Token(), 0, expected.size()), "Read after rejected mutation send failed");
		const auto response = peer.ReceiveMessage();
		CheckRawResponse(response, 4, 6, 0);
		Check(response.size() == remote_header_size + expected.size()
			&& std::equal(response.begin() + remote_header_size, response.end(), expected.begin())
			&& EqualBytes(ReadBytes(read_path), expected), "read capability allowed mutation or became unusable");
		Check(peer.SendMessage(12, 7, mount.Token()), "read-only CloseToken send failed");
		CheckRawResponse(peer.ReceiveMessage(), 12, 7, 0);
	}

	// -------------------
	// Lifecycle
	// -------------------
	void test_closed_token_cannot_be_reused() {
		TempDirectory temporary;
		const auto read_path = temporary.Path() / "revoked-read.bin";
		const auto write_path = temporary.Path() / "unused-write.bin";
		const auto expected = MakePattern(32);
		WriteBytes(read_path, expected);
		Server server(read_path, write_path);
		Check(server.Connect(Net::Connection::Protocol::IPv4, address, port), "revocation server connect failed");
		Client client;
		Check(client.Connect(Net::Connection::Protocol::IPv4, address, port), "revocation control connect failed");
		auto [mount, rejected] = client.RequestMount(false);
		Check(!rejected && mount.Result() == Mount::Status::Authorized, "revocation mount failed");
		RawPeer peer(mount.Port());
		Check(peer.Connected(), "revocation raw peer connect failed");
		SendRawAttach(peer, mount.Token());
		Check(peer.SendMessage(2, 2, mount.Token()), "revocation Open send failed");
		CheckRawResponse(peer.ReceiveMessage(), 2, 2, 0);
		Check(peer.SendMessage(12, 3, mount.Token()), "revocation CloseToken send failed");
		CheckRawResponse(peer.ReceiveMessage(), 12, 3, 0);
		for (const auto& [opcode, sequence]: std::array<std::pair<std::uint8_t, std::uint64_t>, 4>{
			{{1, 4}, {2, 5}, {7, 6}, {12, 7}}}) {
			Check(peer.SendMessage(opcode, sequence, mount.Token()), "revoked token request send failed");
			const auto response = peer.ReceiveMessage();
			CheckRawResponse(response, opcode, sequence, 2);
			Check(response.size() == remote_header_size, "revoked token leaked payload bytes");
		}
		auto [fresh, fresh_rejected] = client.RequestMount(false);
		Check(!fresh_rejected && fresh.Result() == Mount::Status::Authorized
			&& fresh.Token() != mount.Token() && fresh.Port() == mount.Port(), "revocation broke remount or reused a token");
		Check(peer.SendMessage(1, 8, fresh.Token()), "fresh Attach send failed");
		CheckRawResponse(peer.ReceiveMessage(), 1, 8, 0);
		Check(peer.SendMessage(2, 9, fresh.Token()), "fresh Open send failed");
		CheckRawResponse(peer.ReceiveMessage(), 2, 9, 0);
		Check(peer.SendMessage(4, 10, fresh.Token(), 0, expected.size()), "fresh Read send failed");
		const auto response = peer.ReceiveMessage();
		CheckRawResponse(response, 4, 10, 0);
		Check(response.size() == remote_header_size + expected.size()
			&& std::equal(response.begin() + remote_header_size, response.end(), expected.begin()),
			"fresh token failed after revoked-token requests");
		Check(peer.SendMessage(12, 11, fresh.Token()), "fresh CloseToken send failed");
		CheckRawResponse(peer.ReceiveMessage(), 12, 11, 0);
	}

	void test_peer_kill_during_large_read_releases_host_handle() {
		TempDirectory temporary;
		const auto read_path = temporary.Path() / "killed-peer-read.bin";
		const auto write_path = temporary.Path() / "unused-write.bin";
		WriteBytes(read_path, MakePattern(1024 * 1024));
		Server server(read_path, write_path);
		Check(server.Connect(Net::Connection::Protocol::IPv4, address, port), "peer-kill server connect failed");
		Client client;
		Check(client.Connect(Net::Connection::Protocol::IPv4, address, port), "peer-kill control connect failed");
		auto [mount, rejected] = client.RequestMount(false);
		Check(!rejected && mount.Result() == Mount::Status::Authorized, "peer-kill mount failed");
		{
			RawPeer peer(mount.Port());
			Check(peer.Connected(), "peer-kill raw data-plane connect failed");
			SendRawAttach(peer, mount.Token());
			Check(peer.SendMessage(2, 2, mount.Token()), "peer-kill Open send failed");
			CheckRawResponse(peer.ReceiveMessage(), 2, 2, 0);
			Check(peer.SendMessage(4, 3, mount.Token(), 0, 1024 * 1024), "large peer-kill Read send failed");
		}

		std::error_code remove_error;
		const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{5};
		while (std::chrono::steady_clock::now() < deadline) {
			remove_error.clear();
			if (std::filesystem::remove(read_path, remove_error))
				break;
			std::this_thread::sleep_for(std::chrono::milliseconds{10});
		}
		Check(!std::filesystem::exists(read_path), "peer kill during a large Read left the host handle open");
		Check(client.Status() == Net::Connection::Status::Connected,
			"private peer kill disconnected the application control session");
		client.Disconnect();
		Check(client.Connect(Net::Connection::Protocol::IPv4, address, port), "peer-kill client reconnect failed");
		auto [remount, remount_rejected] = client.RequestMount(false, true);
		Check(!remount_rejected && remount.Result() == Mount::Status::Unavailable,
			"missing path after peer kill did not return Unavailable");
	}

	void test_pipeline_and_control_disconnect() {
		TempDirectory temporary;
		const auto read_path = temporary.Path() / "pipeline-read.bin";
		const auto write_path = temporary.Path() / "pipeline-write.bin";
		WriteFile(read_path, "0123456789ABCDEF");
		WriteFile(write_path, "old");
		Server server(read_path, write_path);
		server.UseFramedPipeline(true);
		Check(server.Connect(Net::Connection::Protocol::IPv4, address, port), "pipeline server connect failed");
		Client client;
		client.UseFramedPipeline(true);
		Check(client.Connect(Net::Connection::Protocol::IPv4, address, port), "pipeline client connect failed");
		ExerciseSharedPipelineAndDisconnect(server, client, read_path, write_path);
	}

	void test_plane_failure_faults_every_leaf() {
		TempDirectory temporary;
		const auto read_path = temporary.Path() / "fault-read.bin";
		const auto write_path = temporary.Path() / "unused-write.bin";
		WriteBytes(read_path, MakePattern(1024 * 1024));
		ExerciseTransportFailureMarksReaderFault(read_path, write_path);
	}

	void test_server_heartbeat_timeout_releases_peer_plane() {
		TempDirectory temporary;
		const auto read_path = temporary.Path() / "heartbeat-read.bin";
		const auto write_path = temporary.Path() / "unused-write.bin";
		const StormByte::BinaryData expected = MakePattern(128);
		WriteBytes(read_path, expected);
		Server server(read_path, write_path);
		Check(server.Connect(Net::Connection::Protocol::IPv4, address, port), "heartbeat server connect failed");
		Client client;
		Check(client.Connect(Net::Connection::Protocol::IPv4, address, port), "heartbeat control connect failed");
		auto [mount, rejected] = client.RequestMount(false);
		Check(!rejected && mount.Result() == Mount::Status::Authorized, "heartbeat mount failed");
		{
			RawPeer peer(mount.Port());
			Check(peer.Connected(), "heartbeat raw data-plane connect failed");
			SendRawAttach(peer, mount.Token());
			Check(peer.WaitForClose(), "idle peer data plane did not expire its heartbeat deadline");
		}

		auto [same_session, same_session_rejected] = client.RequestMount(false);
		Check(!same_session_rejected && same_session.Result() == Mount::Status::Failed,
			"a timed-out Client remounted before reconnecting");
		client.Disconnect();
		Check(client.Connect(Net::Connection::Protocol::IPv4, address, port), "client reconnect after heartbeat timeout failed");
		auto [new_mount, new_rejected] = client.RequestMount(false);
		Check(!new_rejected && new_mount.Result() == Mount::Status::Authorized,
			"a new Client session could not mount after plane timeout");
		auto reader = client.AttachReader(new_mount);
		Check(reader && reader->Open(), "post-timeout reader open failed");
		std::array<std::byte, 16> bytes{};
		const auto read = reader->Read(std::span<std::byte>{bytes});
		Check(read.count == bytes.size() && std::equal(bytes.begin(), bytes.end(), expected.begin()),
			"post-timeout reconnect returned incorrect fixture bytes");
		Check(reader->Close().status == Buf::IO::Status::Ok, "post-timeout reader CloseToken failed");
		reader.reset();
		Check(std::filesystem::remove(read_path), "host file remained open after heartbeat token cleanup");
	}

	// -------------------
	// Protocol
	// -------------------
	void test_coalesced_operations_backpressure_keeps_plane_alive() {
		TempDirectory temporary;
		const auto read_path = temporary.Path() / "backpressure-read.bin";
		const auto write_path = temporary.Path() / "unused-write.bin";
		const StormByte::BinaryData expected = MakePattern(1024 * 1024);
		WriteBytes(read_path, expected);
		Server server(read_path, write_path);
		Check(server.Connect(Net::Connection::Protocol::IPv4, address, port), "backpressure server connect failed");
		Client client;
		Check(client.Connect(Net::Connection::Protocol::IPv4, address, port), "backpressure control connect failed");
		auto [mount, rejected] = client.RequestMount(false);
		Check(!rejected && mount.Result() == Mount::Status::Authorized, "backpressure mount failed");
		RawPeer peer(mount.Port());
		Check(peer.Connected(), "backpressure raw peer connect failed");
		SendRawAttach(peer, mount.Token());
		Check(peer.SendMessage(2, 2, mount.Token()), "backpressure Open send failed");
		CheckRawResponse(peer.ReceiveMessage(), 2, 2, 0);
		const std::vector<std::pair<std::uint64_t, std::uint64_t>> reads{{0, 384 * 1024}, {700123, 32}};
		Check(peer.SendReadBurst(mount.Token(), reads, 3), "coalesced reads send failed");
		for (std::size_t index = 0; index < reads.size(); ++index) {
			const StormByte::BinaryData response = peer.ReceiveMessage();
			const std::uint64_t request_id = 3 + index;
			const auto returned_length = StormByte::Serializable<std::uint64_t>::Deserialize(
				std::span<const std::byte>{response.data() + 18, sizeof(std::uint64_t)});
			const std::size_t body_offset = index == 0 ? 0 : 700123;
			Check(response.size() == remote_header_size + reads[index].second
				&& std::to_integer<std::uint8_t>(response[0]) == 4
				&& StormByte::Serializable<std::uint64_t>::Deserialize(
					std::span<const std::byte>{response.data() + 2, sizeof(std::uint64_t)}) == request_id
				&& returned_length && *returned_length == reads[index].second
				&& std::equal(response.begin() + static_cast<std::ptrdiff_t>(remote_header_size), response.end(),
					expected.begin() + static_cast<std::ptrdiff_t>(body_offset)),
				"coalesced operation was lost, reordered, or corrupted under backpressure");
		}
		Check(peer.SendMessage(10, 5, Mount::ChannelToken{}), "post-backpressure heartbeat send failed");
		CheckRawResponse(peer.ReceiveMessage(), 11, 5, 0);
	}

	void test_invalid_private_opcode() {
		ExerciseMalformedRawPlane(MalformedFrame::Opcode);
	}

	void test_mismatched_control_pipeline_denies_mount() {
		TempDirectory temporary;
		const auto read_path = temporary.Path() / "mismatch-control-read.bin";
		const auto write_path = temporary.Path() / "mismatch-control-write.bin";
		const auto expected = MakePattern(32);
		WriteBytes(read_path, expected);
		WriteBytes(write_path, expected);
		Server server(read_path, write_path);
		server.UseFramedPipeline(true);
		Check(server.Connect(Net::Connection::Protocol::IPv4, address, port), "mismatch control server connect failed");
		Client attacker;
		Check(attacker.Connect(Net::Connection::Protocol::IPv4, address, port), "mismatch control connect failed");
		auto [mount, rejected] = attacker.RequestMount(true);
		(void)rejected;
		Check(mount.Result() != Mount::Status::Authorized && mount.Port() == 0,
			"incompatible control pipeline obtained a mount capability");
		attacker.Disconnect();
		Check(EqualBytes(ReadBytes(write_path), expected), "incompatible control pipeline modified a file");
		Client legitimate;
		legitimate.UseFramedPipeline(true);
		Check(legitimate.Connect(Net::Connection::Protocol::IPv4, address, port), "legitimate control connect failed");
		auto [valid, valid_rejected] = legitimate.RequestMount(false);
		Check(!valid_rejected && valid.Result() == Mount::Status::Authorized, "mismatch broke legitimate mount handling");
		auto reader = legitimate.AttachReader(valid);
		Check(reader && reader->Open(), "legitimate reader Open failed after control mismatch");
		std::array<std::byte, 32> bytes{};
		const auto read = reader->Read(std::span<std::byte>{bytes});
		Check(read.count == bytes.size() && std::equal(bytes.begin(), bytes.end(), expected.begin()),
			"legitimate data changed after control mismatch");
		Check(reader->Close().status == Buf::IO::Status::Ok, "legitimate Close failed after control mismatch");
	}

	void test_mismatched_file_pipeline_rejects_attach() {
		TempDirectory temporary;
		const auto read_path = temporary.Path() / "mismatch-plane-read.bin";
		const auto write_path = temporary.Path() / "mismatch-plane-write.bin";
		const auto expected = MakePattern(32);
		WriteBytes(read_path, expected);
		WriteBytes(write_path, expected);
		Server server(read_path, write_path);
		server.UseFramedPipeline(true);
		Check(server.Connect(Net::Connection::Protocol::IPv4, address, port), "mismatch plane server connect failed");
		for (const bool write: {false, true}) {
			Client attacker;
			attacker.UseFramedPipeline(true);
			Check(attacker.Connect(Net::Connection::Protocol::IPv4, address, port), "mismatch plane control connect failed");
			auto [mount, rejected] = attacker.RequestMount(write);
			Check(!rejected && mount.Result() == Mount::Status::Authorized, "compatible control failed to authorize fixture");
			RawPeer untrusted(mount.Port());
			Check(untrusted.Connected(), "incompatible private peer connect failed");
			Check(untrusted.SendMessage(1, 1, mount.Token()), "incompatible private Attach send failed");
			Check(untrusted.WaitForClose(), "incompatible private pipeline Attach was not rejected");
			Check(attacker.Status() == Net::Connection::Status::Connected,
				"private pipeline failure disconnected the authorized control session");
			attacker.Disconnect();
		}
		Check(EqualBytes(ReadBytes(read_path), expected) && EqualBytes(ReadBytes(write_path), expected),
			"incompatible private pipelines modified fixture bytes");
		Client legitimate;
		legitimate.UseFramedPipeline(true);
		Check(legitimate.Connect(Net::Connection::Protocol::IPv4, address, port), "legitimate plane control connect failed");
		auto [valid, valid_rejected] = legitimate.RequestMount(true);
		Check(!valid_rejected && valid.Result() == Mount::Status::Authorized,
			"rejected private pipeline retained an exclusive file reservation");
		auto writer = legitimate.AttachWriter(valid);
		Check(writer && writer->Open(), "legitimate private pipeline writer Open failed");
		Check(writer->Close(), "legitimate private pipeline Close failed");
	}

	void test_oversized_private_frame() {
		ExerciseMalformedRawPlane(MalformedFrame::Oversized);
	}

	void test_repeated_private_sequence() {
		ExerciseMalformedRawPlane(MalformedFrame::RepeatedSequence);
	}

	void test_truncated_private_frame() {
		ExerciseMalformedRawPlane(MalformedFrame::Truncated);
	}

	// -------------------
	// Reader
	// -------------------
	void test_eight_interleaved_readers() {
		TempDirectory temporary;
		const auto read_path = temporary.Path() / "interleaved-read.bin";
		const auto write_path = temporary.Path() / "unused-write.bin";
		WriteBytes(read_path, MakePattern(1024 * 1024));
		Server server(read_path, write_path);
		Check(server.Connect(Net::Connection::Protocol::IPv4, address, port), "interleaved server connect failed");
		Client client;
		Check(client.Connect(Net::Connection::Protocol::IPv4, address, port), "interleaved client connect failed");
		ExerciseEightReaders(client, read_path);
	}

	void test_large_pattern_reader() {
		TempDirectory temporary;
		const auto read_path = temporary.Path() / "large-read.bin";
		const auto write_path = temporary.Path() / "unused-write.bin";
		WriteBytes(read_path, MakePattern(1024 * 1024));
		Server server(read_path, write_path);
		server.UseFramedPipeline(true);
		Check(server.Connect(Net::Connection::Protocol::IPv4, address, port), "large-read server connect failed");
		Client client;
		client.UseFramedPipeline(true);
		Check(client.Connect(Net::Connection::Protocol::IPv4, address, port), "large-read client connect failed");
		ExercisePatternReader(client, read_path);
	}

	void test_maximum_payload_read() {
		TempDirectory temporary;
		const auto read_path = temporary.Path() / "bound-read.bin";
		const auto write_path = temporary.Path() / "unused-write.bin";
		const std::size_t payload_size = static_cast<std::size_t>(remote_frame_limit - remote_header_size);
		const StormByte::BinaryData expected = MakePattern(payload_size);
		WriteBytes(read_path, expected);
		Server server(read_path, write_path);
		Check(server.Connect(Net::Connection::Protocol::IPv4, address, port), "maximum-payload server connect failed");
		Client client;
		Check(client.Connect(Net::Connection::Protocol::IPv4, address, port), "maximum-payload control connect failed");
		auto [mount, rejected] = client.RequestMount(false);
		Check(!rejected && mount.Result() == Mount::Status::Authorized, "maximum-payload mount failed");
		RawPeer peer(mount.Port());
		Check(peer.Connected(), "maximum-payload raw connect failed");
		SendRawAttach(peer, mount.Token());
		Check(peer.SendMessage(2, 2, mount.Token()), "maximum-payload Open send failed");
		CheckRawResponse(peer.ReceiveMessage(), 2, 2, 0);
		Check(peer.SendMessage(4, 3, mount.Token(), 0, payload_size), "maximum-payload Read send failed");
		const StormByte::BinaryData response = peer.ReceiveMessage();
		Check(response.size() == remote_frame_limit && std::to_integer<std::uint8_t>(response[1]) == 0,
			"read at the protocol bound did not return an exact-size successful frame");
		const auto returned_size = StormByte::Serializable<std::uint64_t>::Deserialize(
			std::span<const std::byte>{response.data() + 18, sizeof(std::uint64_t)});
		Check(returned_size && *returned_size == payload_size
			&& std::equal(response.begin() + static_cast<std::ptrdiff_t>(remote_header_size), response.end(), expected.begin()),
			"maximum-bound read bytes differed from the host fixture");
	}

	void test_sixteen_peers_with_eight_readers() {
		TempDirectory temporary;
		const auto read_path = temporary.Path() / "unused-read.bin";
		const auto write_path = temporary.Path() / "unused-write.bin";
		ExerciseRemotePlaneScale(read_path, write_path, temporary.Path());
	}

	// -------------------
	// Writer
	// -------------------
	void test_flush_after_size_queries() {
		TempDirectory temporary;
		const auto read_path = temporary.Path() / "unused-read.bin";
		const auto write_path = temporary.Path() / "flush-size-write.bin";
		Server server(read_path, write_path);
		Check(server.Connect(Net::Connection::Protocol::IPv4, address, port), "flush-size server connect failed");
		Client client;
		Check(client.Connect(Net::Connection::Protocol::IPv4, address, port), "flush-size control connect failed");
		auto [mount, rejected] = client.RequestMount(true);
		Check(!rejected && mount.Result() == Mount::Status::Authorized, "flush-size mount failed");
		RawPeer peer(mount.Port());
		Check(peer.Connected(), "flush-size peer connect failed");
		SendRawAttach(peer, mount.Token());
		Check(peer.SendMessage(2, 2, mount.Token()), "flush-size Open send failed");
		CheckRawResponse(peer.ReceiveMessage(), 2, 2, 0);
		const auto expected = MakePattern(1024 * 1024);
		std::uint64_t sequence = 3;
		for (std::uint64_t round = 0; round < 3; ++round) {
			Check(peer.SendMessage(5, sequence, mount.Token(), 0, expected.size(), expected), "flush-size Write send failed");
			CheckRawResponse(peer.ReceiveMessage(), 5, sequence++, 0);
			Check(peer.SendMessage(7, sequence, mount.Token()), "flush-size Size send failed");
			const auto response = peer.ReceiveMessage();
			CheckRawResponse(response, 7, sequence++, 0);
			const auto size = StormByte::Serializable<std::uint64_t>::Deserialize(
				std::span<const std::byte>{response.data() + 18, sizeof(std::uint64_t)});
			Check(size && *size == expected.size(), "remote Size reported an incorrect length");
			Check(peer.SendMessage(8, sequence, mount.Token()), "flush-size Flush send failed");
			CheckRawResponse(peer.ReceiveMessage(), 8, sequence++, 0);
			Check(EqualBytes(ReadBytes(write_path), expected), "flush after Size changed file bytes");
		}
		Check(peer.SendMessage(12, sequence, mount.Token()), "flush-size CloseToken send failed");
		CheckRawResponse(peer.ReceiveMessage(), 12, sequence, 0);
	}

	void test_pattern_writer_lifecycle() {
		TempDirectory temporary;
		const auto read_path = temporary.Path() / "unused-read.bin";
		const auto write_path = temporary.Path() / "pattern-write.bin";
		Server server(read_path, write_path);
		Check(server.Connect(Net::Connection::Protocol::IPv4, address, port), "writer server connect failed");
		Client client;
		Check(client.Connect(Net::Connection::Protocol::IPv4, address, port), "writer client connect failed");
		ExercisePatternWriter(client, write_path);
	}

	void test_writer_reader_conflicts() {
		TempDirectory temporary;
		const auto read_path = temporary.Path() / "conflict-read.bin";
		const auto write_path = temporary.Path() / "conflict-write.bin";
		WriteFile(read_path, "0123456789ABCDEF");
		WriteFile(write_path, "old deterministic contents");
		Server server(read_path, write_path);
		Check(server.Connect(Net::Connection::Protocol::IPv4, address, port), "conflict server connect failed");
		Client client;
		Check(client.Connect(Net::Connection::Protocol::IPv4, address, port), "conflict client connect failed");
		ExerciseWriterAndConflict(server, client, write_path);
		Check(ReadFile(write_path) == "ABCDxyGHIJ", "writer conflict fixture differed on disk");
	}

	template<typename TestFunction>
	int RunOne(const std::string_view name, TestFunction&& test) {
		try {
			test();
			std::cout << name << " passed" << std::endl;
			return 0;
		} catch (const std::exception& exception) {
			std::cerr << name << " failed: " << exception.what() << std::endl;
			return 1;
		}
	}
}

int main() {
	using namespace RemoteFileTest;
	int result = 0;
	// -------------------
	// Access
	// -------------------
	result += RunOne("test_acl_and_independent_reader_cursors", test_acl_and_independent_reader_cursors);
	result += RunOne("test_capability_isolation", test_capability_isolation);
	result += RunOne("test_foreign_and_forged_tokens_preserve_owner", test_foreign_and_forged_tokens_preserve_owner);
	result += RunOne("test_malformed_close_preserves_capability", test_malformed_close_preserves_capability);
	result += RunOne("test_read_capability_rejects_mutating_operations", test_read_capability_rejects_mutating_operations);

	// -------------------
	// Lifecycle
	// -------------------
	result += RunOne("test_closed_token_cannot_be_reused", test_closed_token_cannot_be_reused);
	result += RunOne("test_peer_kill_during_large_read_releases_host_handle", test_peer_kill_during_large_read_releases_host_handle);
	result += RunOne("test_pipeline_and_control_disconnect", test_pipeline_and_control_disconnect);
	result += RunOne("test_plane_failure_faults_every_leaf", test_plane_failure_faults_every_leaf);
	result += RunOne("test_server_heartbeat_timeout_releases_peer_plane", test_server_heartbeat_timeout_releases_peer_plane);

	// -------------------
	// Protocol
	// -------------------
	result += RunOne("test_coalesced_operations_backpressure_keeps_plane_alive", test_coalesced_operations_backpressure_keeps_plane_alive);
	result += RunOne("test_invalid_private_opcode", test_invalid_private_opcode);
	result += RunOne("test_mismatched_control_pipeline_denies_mount", test_mismatched_control_pipeline_denies_mount);
	result += RunOne("test_mismatched_file_pipeline_rejects_attach", test_mismatched_file_pipeline_rejects_attach);
	result += RunOne("test_oversized_private_frame", test_oversized_private_frame);
	result += RunOne("test_repeated_private_sequence", test_repeated_private_sequence);
	result += RunOne("test_truncated_private_frame", test_truncated_private_frame);

	// -------------------
	// Reader
	// -------------------
	result += RunOne("test_eight_interleaved_readers", test_eight_interleaved_readers);
	result += RunOne("test_large_pattern_reader", test_large_pattern_reader);
	result += RunOne("test_maximum_payload_read", test_maximum_payload_read);
	result += RunOne("test_sixteen_peers_with_eight_readers", test_sixteen_peers_with_eight_readers);

	// -------------------
	// Writer
	// -------------------
	result += RunOne("test_flush_after_size_queries", test_flush_after_size_queries);
	result += RunOne("test_pattern_writer_lifecycle", test_pattern_writer_lifecycle);
	result += RunOne("test_writer_reader_conflicts", test_writer_reader_conflicts);
	return result == 0 ? 0 : 1;
}
