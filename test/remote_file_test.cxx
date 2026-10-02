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

	void Check(bool condition, std::string_view message);

#ifdef WINDOWS
	using RawSocket = SOCKET;
	constexpr RawSocket invalid_raw_socket = INVALID_SOCKET;
#else
	using RawSocket = int;
	constexpr RawSocket invalid_raw_socket = -1;
#endif

	class RawPeer final {
		public:
			explicit RawPeer(const unsigned short peer_port) {
				m_socket = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
				if (m_socket == invalid_raw_socket) return;
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

			~RawPeer() { Close(); }

			bool Connected() const noexcept { return m_socket != invalid_raw_socket; }

			void Close() noexcept {
				if (m_socket == invalid_raw_socket) return;
#ifdef WINDOWS
				closesocket(m_socket);
#else
				close(m_socket);
#endif
				m_socket = invalid_raw_socket;
			}

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
				for (auto& byte: payload) byte ^= std::byte{0x67};
				StormByte::BinaryData frame = StormByte::Serializable<std::uint64_t>(payload.size()).Serialize();
				frame.append(std::move(payload));
				return SendAll(frame);
			}

			bool SendRawFramePrefix(const std::uint64_t length, const std::span<const std::byte> partial = {}) {
				StormByte::BinaryData frame = StormByte::Serializable<std::uint64_t>(length).Serialize();
				frame.append(partial);
				return SendAll(frame);
			}

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
					for (auto& byte: payload) byte ^= std::byte{0x67};
					batch.append(StormByte::Serializable<std::uint64_t>(payload.size()).Serialize());
					batch.append(std::move(payload));
				}
				return SendAll(batch);
			}

			StormByte::BinaryData ReceiveMessage() {
				std::array<std::byte, sizeof(std::uint64_t)> prefix{};
				Check(ReceiveExact(prefix), "data-plane response prefix failed");
				const auto length = StormByte::Serializable<std::uint64_t>::Deserialize(prefix);
				Check(length && *length > 0 && *length <= remote_frame_limit, "invalid data-plane response length");
				StormByte::BinaryData payload{StormByte::ByteSize{*length}};
				Check(ReceiveExact(std::span<std::byte>{payload.data(), payload.size()}), "data-plane response body failed");
				for (auto& byte: payload) byte ^= std::byte{0x67};
				return payload;
			}

			bool WaitForClose() const noexcept {
				if (!Connected()) return true;
				fd_set descriptors;
				FD_ZERO(&descriptors);
				FD_SET(m_socket, &descriptors);
				timeval timeout_value{ .tv_sec = 5, .tv_usec = 0 };
#ifdef WINDOWS
				const int ready = select(0, &descriptors, nullptr, nullptr, &timeout_value);
#else
				const int ready = select(m_socket + 1, &descriptors, nullptr, nullptr, &timeout_value);
#endif
				if (ready <= 0) return false;
				char byte = 0;
#ifdef WINDOWS
				return ::recv(m_socket, &byte, 1, 0) <= 0;
#else
				return ::recv(m_socket, &byte, 1, 0) <= 0;
#endif
			}

			void ShutdownSend() noexcept {
				if (m_socket == invalid_raw_socket) return;
#ifdef WINDOWS
				(void)::shutdown(m_socket, SD_SEND);
#else
				(void)::shutdown(m_socket, SHUT_WR);
#endif
			}

		private:
			bool SendAll(std::span<const std::byte> bytes) {
				while (!bytes.empty()) {
#ifdef WINDOWS
					const int sent = ::send(m_socket, reinterpret_cast<const char*>(bytes.data()),
						static_cast<int>(bytes.size()), 0);
#else
					const ssize_t sent = ::send(m_socket, bytes.data(), bytes.size(), 0);
#endif
					if (sent <= 0) return false;
					bytes = bytes.subspan(static_cast<std::size_t>(sent));
				}
				return true;
			}

			bool ReceiveExact(std::span<std::byte> bytes) {
				while (!bytes.empty()) {
#ifdef WINDOWS
					const int received = ::recv(m_socket, reinterpret_cast<char*>(bytes.data()),
						static_cast<int>(bytes.size()), 0);
#else
					const ssize_t received = ::recv(m_socket, bytes.data(), bytes.size(), 0);
#endif
					if (received <= 0) return false;
					bytes = bytes.subspan(static_cast<std::size_t>(received));
				}
				return true;
			}

			RawSocket m_socket{invalid_raw_socket};
	};

	StormByte::Safe::Shared<Log> Logger() {
		static auto logger = StormByte::Safe::Heap::MakeShared<StormByte::Logger::ThreadedLog>(
			std::cerr, StormByte::Logger::Level::Error, "[RemoteFileTest] %T:");
		return logger;
	}

	void Check(const bool condition, const std::string_view message) {
		if (!condition) throw std::runtime_error(std::string{message});
	}

	enum class AppOpcode: unsigned short { ReadRequest = Packet::PROCESS_THRESHOLD, WriteRequest, Mount, Unauthorized };

	class Request final: public Packet {
		public:
			Request(const bool write, const bool missing = false, const std::uint16_t path_selection = 0):
				Packet(static_cast<OpcodeType>(write ? AppOpcode::WriteRequest : AppOpcode::ReadRequest)),
					m_missing(missing), m_path_selection(path_selection) {}
			bool Missing() const noexcept { return m_missing; }
			std::uint16_t PathSelection() const noexcept { return m_path_selection; }
			StormByte::BinaryData DoSerialize() const noexcept override {
				StormByte::BinaryData data = StormByte::Serializable<std::uint8_t>(m_missing ? 1 : 0).Serialize();
				data.append(StormByte::Serializable<std::uint16_t>(m_path_selection).Serialize());
				return data;
			}
		private:
			bool m_missing;
			std::uint16_t m_path_selection;
	};

	class MountPacket final: public Packet {
		public:
			explicit MountPacket(Mount value): Packet(static_cast<OpcodeType>(AppOpcode::Mount)), m_value(std::move(value)) {}
			const Mount& Value() const noexcept { return m_value; }
			StormByte::BinaryData DoSerialize() const noexcept override {
				return StormByte::Serializable<Mount>(m_value).Serialize();
			}
		private:
			Mount m_value;
	};

	class UnauthorizedPacket final: public Packet {
		public:
			UnauthorizedPacket(): Packet(static_cast<OpcodeType>(AppOpcode::Unauthorized)) {}
			StormByte::BinaryData DoSerialize() const noexcept override { return {}; }
	};

	Net::DeserializePacketFunction Factory() {
		return [](Packet::OpcodeType opcode, Buf::Consumer payload, StormByte::Safe::Shared<Log>) -> Net::PacketPointer {
			StormByte::BinaryData bytes;
			payload.ExtractUntilEoF(bytes);
			switch (static_cast<AppOpcode>(opcode)) {
				case AppOpcode::ReadRequest:
				case AppOpcode::WriteRequest: {
					if (bytes.size() != sizeof(std::uint8_t) + sizeof(std::uint16_t)) return nullptr;
					auto flags = StormByte::Serializable<std::uint8_t>::Deserialize(
						std::span<const std::byte>{bytes.data(), sizeof(std::uint8_t)});
					auto path_selection = StormByte::Serializable<std::uint16_t>::Deserialize(
						std::span<const std::byte>{bytes.data() + sizeof(std::uint8_t), sizeof(std::uint16_t)});
					if (!flags || (*flags & 0xFE) != 0 || !path_selection) return nullptr;
					return std::make_shared<Request>(opcode == static_cast<Packet::OpcodeType>(AppOpcode::WriteRequest),
						(*flags & 1) != 0, *path_selection);
				}
				case AppOpcode::Mount: {
					auto mount = StormByte::Serializable<Mount>::Deserialize(bytes);
					return mount ? std::make_shared<MountPacket>(std::move(*mount)) : nullptr;
				}
				case AppOpcode::Unauthorized:
					return bytes.empty() ? std::make_shared<UnauthorizedPacket>() : nullptr;
			}
			return nullptr;
		};
	}

	class XorPipe final: public Buf::Pipe {
		public:
			void Run(Buf::ReadOnly& input, Buf::WriteOnly& output, const StormByte::Safe::Shared<Log>&) override {
				StormByte::BinaryData bytes;
				input.ExtractUntilEoF(bytes);
				for (auto& byte: bytes) byte ^= std::byte{0x67};
				if (!bytes.empty()) (void)output.Write(std::move(bytes));
				output.Close();
			}
			PointerType Clone() const noexcept override { return MakePointer<XorPipe>(); }
			PointerType Move() noexcept override { return MakePointer<XorPipe>(); }
	};

	class ReversibleEnvelopePipe final: public Buf::Pipe {
		public:
			explicit ReversibleEnvelopePipe(bool encode): m_encode(encode) {}
			void Run(Buf::ReadOnly& input, Buf::WriteOnly& output, const StormByte::Safe::Shared<Log>&) override {
				StormByte::BinaryData bytes;
				input.ExtractUntilEoF(bytes);
				if (m_encode) {
					for (auto& byte: bytes) byte ^= std::byte{0x67};
					StormByte::BinaryData framed{std::byte{'A'}, std::byte{'E'}, std::byte{'A'}, std::byte{'D'}};
					framed.append(bytes);
					framed.append(StormByte::Serializable<std::uint64_t>(Tag(bytes)).Serialize());
					(void)output.Write(std::move(framed));
				} else if (bytes.size() >= 12 && bytes[0] == std::byte{'A'} && bytes[1] == std::byte{'E'}
					&& bytes[2] == std::byte{'A'} && bytes[3] == std::byte{'D'}) {
					const std::size_t body_size = bytes.size() - 12;
					const auto tag = StormByte::Serializable<std::uint64_t>::Deserialize(
						std::span<const std::byte>{bytes.data() + 4 + body_size, sizeof(std::uint64_t)});
					StormByte::BinaryData body(std::span<const std::byte>{bytes.data() + 4, body_size});
					if (tag && *tag == Tag(body)) {
						for (auto& byte: body) byte ^= std::byte{0x67};
						(void)output.Write(std::move(body));
					} else output.SetError();
				} else output.SetError();
				output.Close();
			}
			PointerType Clone() const noexcept override { return MakePointer<ReversibleEnvelopePipe>(m_encode); }
			PointerType Move() noexcept override { return MakePointer<ReversibleEnvelopePipe>(m_encode); }
		private:
			static std::uint64_t Tag(const StormByte::BinaryData& bytes) noexcept {
				std::uint64_t hash = 1469598103934665603ull;
				for (std::byte byte: bytes) hash = (hash ^ std::to_integer<std::uint8_t>(byte)) * 1099511628211ull;
				return hash;
			}
			bool m_encode;
	};

	Buf::Pipeline MakeInput(bool framed) {
		Buf::Pipeline pipeline;
		if (framed) pipeline.Add(ReversibleEnvelopePipe{false});
		else pipeline.Add(XorPipe{});
		return pipeline;
	}

	Buf::Pipeline MakeOutput(bool framed) {
		Buf::Pipeline pipeline;
		if (framed) pipeline.Add(ReversibleEnvelopePipe{true});
		else pipeline.Add(XorPipe{});
		return pipeline;
	}

	class Client final: public Net::Client {
		public:
			Client(): Net::Client(Factory(), Logger()) {}
			Buf::Pipeline InputPipeline() const noexcept override { return MakeInput(m_framed.load()); }
			Buf::Pipeline OutputPipeline() const noexcept override { return MakeOutput(m_framed.load()); }
			void UseFramedPipeline(bool enabled) noexcept { m_framed.store(enabled); }
			std::pair<Mount, bool> RequestMount(bool write, bool missing = false, std::uint16_t path_selection = 0) {
				Request request{write, missing, path_selection};
				auto response = Send(request);
				if (auto result = std::dynamic_pointer_cast<MountPacket>(response)) return {result->Value(), false};
				return {Mount::Failed(), std::dynamic_pointer_cast<UnauthorizedPacket>(response) != nullptr};
			}
			Net::RemoteFileReaderHandle AttachReader(const Mount& mount) {
				return CreateRemoteFileReader(mount);
			}
			Net::RemoteFileWriterHandle AttachWriter(const Mount& mount) {
				return CreateRemoteFileWriter(mount);
			}
		private:
			std::atomic<bool> m_framed{false};
	};

	class Server final: public Net::Server {
		public:
			Server(std::filesystem::path read_path, std::filesystem::path write_path):
				Net::Server(Factory(), Logger()), m_read_path(std::move(read_path)), m_write_path(std::move(write_path)) {}
			std::atomic<bool> allow_read{true};
			std::atomic<bool> allow_write{true};
			void UseFramedPipeline(bool enabled) noexcept { m_framed.store(enabled); }
			void SetExtraPaths(std::vector<std::filesystem::path> paths) { m_extra_paths = std::move(paths); }
			Buf::Pipeline InputPipeline() const noexcept override { return MakeInput(m_framed.load()); }
			Buf::Pipeline OutputPipeline() const noexcept override { return MakeOutput(m_framed.load()); }
		private:
			Net::PacketPointer ProcessClientPacket(std::string_view uuid, Net::PacketPointer packet) noexcept override {
				auto request = std::dynamic_pointer_cast<Request>(packet);
				if (!request) return nullptr;
				const bool write = packet->Opcode() == static_cast<Packet::OpcodeType>(AppOpcode::WriteRequest);
				if ((write && !allow_write.load()) || (!write && !allow_read.load())) return std::make_shared<UnauthorizedPacket>();
				const std::filesystem::path* selected_path = nullptr;
				if (!write && request->PathSelection() >= 3
					&& static_cast<std::size_t>(request->PathSelection() - 3) < m_extra_paths.size()) {
					selected_path = &m_extra_paths[request->PathSelection() - 3];
				}
				const auto& path = selected_path ? *selected_path : request->Missing() && !write
					? m_read_path.parent_path() / "absent-remote-file.bin"
					: request->PathSelection() == 1 ? m_read_path
					: request->PathSelection() == 2 ? m_write_path
					: (write ? m_write_path : m_read_path);
				Mount mount = write ? MountRemoteFileWriter(uuid, path, 3) : MountRemoteFileReader(uuid, path, 3);
				return std::make_shared<MountPacket>(std::move(mount));
			}
			std::filesystem::path m_read_path;
			std::filesystem::path m_write_path;
			std::vector<std::filesystem::path> m_extra_paths;
			std::atomic<bool> m_framed{false};
	};

	class TempDirectory final {
		public:
			TempDirectory() {
				m_path = std::filesystem::temp_directory_path() / ("stormbyte-rfile-" + std::to_string(
					std::chrono::steady_clock::now().time_since_epoch().count()));
				std::filesystem::create_directories(m_path);
			}
			~TempDirectory() { std::error_code ignored; std::filesystem::remove_all(m_path, ignored); }
			const std::filesystem::path& Path() const noexcept { return m_path; }
		private:
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
		for (std::size_t index = 0; index < size; ++index) {
			bytes[index] = static_cast<std::byte>(index % 251);
		}
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
		if (length > 0) file.read(reinterpret_cast<char*>(bytes.data()), length);
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

	void TestReadAclAndCursors(Server& server, Client& client, const std::filesystem::path& read_path) {
		server.allow_read = false;
		auto [denied, unauthorized] = client.RequestMount(false);
		Check(unauthorized && denied.Result() == Mount::Status::Failed, "read ACL must return application Unauthorized");
		Check(denied.Port() == 0 && denied.Mode() == Mount::Access::None
			&& std::all_of(denied.Token().begin(), denied.Token().end(), [](std::byte byte) { return byte == std::byte{0}; }),
			"ACL rejection exposed a port, capability, or access mode");
		server.allow_read = true;
		auto [missing, rejected_missing] = client.RequestMount(false, true);
		Check(!rejected_missing && missing.Result() == Mount::Status::Unavailable, "missing read path status mismatch");
		Check(missing.Port() == 0 && missing.Mode() == Mount::Access::None
			&& std::all_of(missing.Token().begin(), missing.Token().end(), [](std::byte byte) { return byte == std::byte{0}; }),
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
		Check(first->Size() == StormByte::ByteSize{16}, "reader size mismatch");
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
		std::thread one([&] { (void)first->Read(std::span<std::byte>{a}); });
		std::thread two([&] { (void)second->Read(std::span<std::byte>{b}); });
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

	void TestWriterAndConflict(Server& server, Client& client, const std::filesystem::path& path) {
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

	void TestSharedPipelineAndDisconnect(Server& server, Client& client,
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

	void TestTransportFailureMarksReaderFault(const std::filesystem::path& read_path,
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

}

namespace RemoteFileTest {
	void ExercisePatternReader(Client& client, const std::filesystem::path& path) {
		const StormByte::BinaryData expected = ReadBytes(path);
		const StormByte::ByteSize length{std::filesystem::file_size(path)};
		auto [mount, rejected] = client.RequestMount(false, false, 1);
		Check(!rejected && mount.Result() == Mount::Status::Authorized, "pattern read mount failed");
		auto reader = client.AttachReader(mount);
		Check(reader && reader->Open(), "pattern reader open failed");
		Check(reader->Size() == length, "remote Size differs from filesystem::file_size");
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
		for (std::size_t index = 1; index < readers.size(); ++index) {
			Check(devices[index]->Throughput().read_bps == devices[0]->Throughput().read_bps
				&& devices[index]->Throughput().write_bps == devices[0]->Throughput().write_bps
				&& devices[index]->Window().read == devices[0]->Window().read
				&& devices[index]->Window().write == devices[0]->Window().write,
				"one Client's leaves did not share the same device snapshot");
		}
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
		for (auto& reader: readers) Check(reader->Close().status == Buf::IO::Status::Ok,
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
		if (writer->State() != Buf::IO::State::Fault && attack_write.status == Buf::IO::Status::Ok) {
			(void)writer->Flush();
		}
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

	void TestLargePatternReader() {
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

	void TestEightInterleavedReaders() {
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

	void TestCapabilityIsolation() {
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

	void TestPatternWriterLifecycle() {
		TempDirectory temporary;
		const auto read_path = temporary.Path() / "unused-read.bin";
		const auto write_path = temporary.Path() / "pattern-write.bin";
		Server server(read_path, write_path);
		Check(server.Connect(Net::Connection::Protocol::IPv4, address, port), "writer server connect failed");
		Client client;
		Check(client.Connect(Net::Connection::Protocol::IPv4, address, port), "writer client connect failed");
		ExercisePatternWriter(client, write_path);
	}

	void TestWriterReaderConflicts() {
		TempDirectory temporary;
		const auto read_path = temporary.Path() / "conflict-read.bin";
		const auto write_path = temporary.Path() / "conflict-write.bin";
		WriteFile(read_path, "0123456789ABCDEF");
		WriteFile(write_path, "old deterministic contents");
		Server server(read_path, write_path);
		Check(server.Connect(Net::Connection::Protocol::IPv4, address, port), "conflict server connect failed");
		Client client;
		Check(client.Connect(Net::Connection::Protocol::IPv4, address, port), "conflict client connect failed");
		TestWriterAndConflict(server, client, write_path);
		Check(ReadFile(write_path) == "ABCDxyGHIJ", "writer conflict fixture differed on disk");
	}

	void TestPipelineAndControlDisconnect() {
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
		TestSharedPipelineAndDisconnect(server, client, read_path, write_path);
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
				if (!peer_port) peer_port = mount.Port();
				Check(mount.Port() == *peer_port, "mounts from one Client did not share a data-plane port");
				auto reader = client->AttachReader(mount);
				Check(reader && reader->Open(), "scale reader open failed");
				Check(reader->Size() == StormByte::ByteSize{64}, "scale reader size mismatch");
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
		for (auto& reader: readers) Check(reader->Close().status == Buf::IO::Status::Ok,
			"scale CloseToken failed");
		readers.clear();
		clients.clear();
		server.Disconnect();
	}

	void TestSixteenPeersWithEightReaders() {
		TempDirectory temporary;
		const auto read_path = temporary.Path() / "unused-read.bin";
		const auto write_path = temporary.Path() / "unused-write.bin";
		ExerciseRemotePlaneScale(read_path, write_path, temporary.Path());
	}

	void TestPlaneFailureFaultsEveryLeaf() {
		TempDirectory temporary;
		const auto read_path = temporary.Path() / "fault-read.bin";
		const auto write_path = temporary.Path() / "unused-write.bin";
		WriteBytes(read_path, MakePattern(1024 * 1024));
		TestTransportFailureMarksReaderFault(read_path, write_path);
	}

	void TestAclAndIndependentReaderCursors() {
		TempDirectory temporary;
		const auto read_path = temporary.Path() / "read.bin";
		const auto write_path = temporary.Path() / "write.bin";
		WriteFile(read_path, "0123456789ABCDEF");
		WriteFile(write_path, "old deterministic contents");
		Server server(read_path, write_path);
		Check(server.Connect(Net::Connection::Protocol::IPv4, address, port), "ACL server connect failed");
		Client client;
		Check(client.Connect(Net::Connection::Protocol::IPv4, address, port), "ACL client connect failed");
		TestReadAclAndCursors(server, client, read_path);
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

	void TestInvalidPrivateOpcode() { ExerciseMalformedRawPlane(MalformedFrame::Opcode); }
	void TestRepeatedPrivateSequence() { ExerciseMalformedRawPlane(MalformedFrame::RepeatedSequence); }
	void TestTruncatedPrivateFrame() { ExerciseMalformedRawPlane(MalformedFrame::Truncated); }
	void TestOversizedPrivateFrame() { ExerciseMalformedRawPlane(MalformedFrame::Oversized); }

	void TestMaximumPayloadRead() {
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

	void TestServerHeartbeatTimeoutReleasesPeerPlane() {
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

	void TestCoalescedOperationsBackpressureKeepsPlaneAlive() {
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

	void TestPeerKillDuringLargeReadReleasesHostHandle() {
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
			if (std::filesystem::remove(read_path, remove_error)) break;
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
	result += RunOne("TestAclAndIndependentReaderCursors", TestAclAndIndependentReaderCursors);
	result += RunOne("TestLargePatternReader", TestLargePatternReader);
	result += RunOne("TestEightInterleavedReaders", TestEightInterleavedReaders);
	result += RunOne("TestCapabilityIsolation", TestCapabilityIsolation);
	result += RunOne("TestPatternWriterLifecycle", TestPatternWriterLifecycle);
	result += RunOne("TestWriterReaderConflicts", TestWriterReaderConflicts);
	result += RunOne("TestPipelineAndControlDisconnect", TestPipelineAndControlDisconnect);
	result += RunOne("TestSixteenPeersWithEightReaders", TestSixteenPeersWithEightReaders);
	result += RunOne("TestPlaneFailureFaultsEveryLeaf", TestPlaneFailureFaultsEveryLeaf);
	result += RunOne("TestInvalidPrivateOpcode", TestInvalidPrivateOpcode);
	result += RunOne("TestRepeatedPrivateSequence", TestRepeatedPrivateSequence);
	result += RunOne("TestTruncatedPrivateFrame", TestTruncatedPrivateFrame);
	result += RunOne("TestOversizedPrivateFrame", TestOversizedPrivateFrame);
	result += RunOne("TestMaximumPayloadRead", TestMaximumPayloadRead);
	result += RunOne("TestServerHeartbeatTimeoutReleasesPeerPlane", TestServerHeartbeatTimeoutReleasesPeerPlane);
	result += RunOne("TestCoalescedOperationsBackpressureKeepsPlaneAlive", TestCoalescedOperationsBackpressureKeepsPlaneAlive);
	result += RunOne("TestPeerKillDuringLargeReadReleasesHostHandle", TestPeerKillDuringLargeReadReleasesHostHandle);
	return result == 0 ? 0 : 1;
}
