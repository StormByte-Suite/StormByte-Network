#include <StormByte/network/client.hxx>
#include <StormByte/network/remote_file.hxx>
#include <StormByte/network/server.hxx>
#include <StormByte/serializable.hxx>

#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <memory>
#include <mutex>
#include <random>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#ifdef WINDOWS
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

/**
 * @namespace HandshakeExample
 * @brief Application-defined negotiation example, not cryptographic security.
 * @details Public keys are labels, the random secret is returned in plaintext,
 * and XOR is reversible without authentication. Production implementations must
 * authenticate key establishment and use authenticated encryption with independent
 * keys or nonce domains for each connection, direction and file plane.
 */
namespace HandshakeExample {
	using namespace StormByte;
	using namespace StormByte::Network;

	/**
	 * @brief Loopback port used only by this executable.
	 */
	constexpr unsigned short port = 7194;

	/**
	 * @brief Consumer-owned handshake and protected-message identifiers.
	 */
	enum class Operation: Transport::Packet::OpcodeType {
		Hello = 1,
		Welcome = 2,
		Rejected = 3,
		Echo = Transport::Packet::PROCESS_THRESHOLD,
		EchoReply,
		MountRead,
		MountWrite,
		MountReply
	};

	/**
	 * @brief Assert a fixture condition with a domain exception.
	 * @param condition Expected condition.
	 * @param message Diagnostic text, never credentials or a secret.
	 */
	void Check(bool condition, std::string_view message) {
		if (!condition)
			throw Network::Exception(message);
	}

	/**
	 * @brief Application packet whose serialization is independent of Network.
	 */
	class Packet final: public Transport::Packet {
		public:
			/**
			 * @brief Construct a negotiation or application message.
			 * @param operation Wire opcode.
			 * @param fields Application-owned fields.
			 */
			Packet(Operation operation, std::vector<std::string> fields):
				Transport::Packet(static_cast<OpcodeType>(operation)), m_fields(std::move(fields)) {
			}

			/**
			 * @brief Inspect validated fields.
			 * @return Fields borrowed for the packet lifetime.
			 */
			const std::vector<std::string>& Fields() const noexcept {
				return m_fields;
			}

		private:
			/**
			 * @brief Serialize message fields.
			 * @return Provider-independent owned bytes.
			 */
			BinaryData DoSerialize() const noexcept override {
				return Serializable<std::vector<std::string>>(m_fields).Serialize();
			}

			/**
			 * @brief Fields allocated and destroyed by this test provider.
			 */
			std::vector<std::string> m_fields;
	};

	/**
	 * @brief Count factory calls for messages that must never be decoded after rejection.
	 */
	std::atomic<unsigned int> protected_decodes{0};

	/**
	 * @brief Count nonempty transformed bytes, proving that negotiated stages run.
	 */
	std::atomic<std::uint64_t> transformed_bytes{0};

	/**
	 * @brief Count handlers actually dispatched for protected requests.
	 */
	std::atomic<unsigned int> protected_handlers{0};

	/**
	 * @brief Build the consumer's opcode factory with strict field validation.
	 * @return Copyable application decoder.
	 */
	DeserializePacketFunction Factory() {
		return [](Transport::Packet::OpcodeType opcode, Buffer::Consumer payload,
			Safe::Shared<Logger::Log>) -> PacketPointer {
			if (opcode != static_cast<unsigned short>(Operation::Hello)
				&& opcode != static_cast<unsigned short>(Operation::Welcome)
				&& opcode != static_cast<unsigned short>(Operation::Rejected)
				&& opcode != static_cast<unsigned short>(Operation::Echo)
				&& opcode != static_cast<unsigned short>(Operation::EchoReply)
				&& opcode != static_cast<unsigned short>(Operation::MountRead)
				&& opcode != static_cast<unsigned short>(Operation::MountWrite)
				&& opcode != static_cast<unsigned short>(Operation::MountReply))
				return {};
			if (opcode == static_cast<unsigned short>(Operation::Echo))
				++protected_decodes;
			BinaryData bytes;
			payload.ReadUntilEoF(bytes);
			auto fields = Serializable<std::vector<std::string>>::Deserialize(bytes);
			const std::size_t count = opcode == static_cast<unsigned short>(Operation::Welcome) ? 2 : 1;
			if (!fields || fields->size() != count)
				return {};
			return PacketPointer::MakePointer<Packet>(static_cast<Operation>(opcode), std::move(*fields));
		};
	}

	/**
	 * @brief Build a copyable reversible test transformation, never a real cipher.
	 * @param secret Nonzero XOR mask agreed by the application.
	 * @return Independently owned pipeline.
	 */
	Buffer::Pipeline XorPipeline(unsigned char secret) {
		Buffer::Pipeline result;
		result.Add(Buffer::Pipe{[secret](const Buffer::PipeInput& input,
			const Buffer::PipeOutput& output, const Safe::Shared<Logger::Log>&) {
			while (!input.EoF()) {
				BinaryData bytes;
				if (!input.Read(1, bytes)) {
					output.SetError();
					return;
				}
				const auto available = input.Available();
				if (available > ByteSize{0}) {
					BinaryData remaining;
					if (!input.Read(available, remaining)) {
						output.SetError();
						return;
					}
					bytes.append(std::move(remaining));
				}
				for (auto& byte: bytes)
					byte ^= static_cast<std::byte>(secret);
				transformed_bytes.fetch_add(static_cast<std::uint64_t>(bytes.size()));
				if (!output.Write(std::move(bytes))) {
					output.SetError();
					return;
				}
			}
			output.Close();
		}});
		return result;
	}

	/**
	 * @brief Build a fixture whose provider refuses copying, without production fault hooks.
	 * @return Moveable pipeline that fails when configuration clones its templates.
	 */
	Buffer::Pipeline UnclonablePipeline() {
		std::unique_ptr<unsigned char, Safe::Heap::ObjectDeleter> context = Safe::Heap::MakeUnique<unsigned char>(1);
		Buffer::Pipe::Callback callback(context.get(),
			[](void*, const Buffer::PipeInput&, const Buffer::PipeOutput& output,
				const Safe::Shared<Logger::Log>&) {
				output.Close();
				return Safe::Status::Success;
			},
			[](const void*) noexcept -> void* {
				return nullptr;
			},
			[](void* state) noexcept {
				Safe::Heap::ObjectDeleter{}(static_cast<unsigned char*>(state));
			});
		(void)context.release();
		Buffer::Pipeline pipeline;
		pipeline.Add(Buffer::Pipe{std::move(callback)});
		return pipeline;
	}

	/**
	 * @brief Server policy with per-UUID state and explicit rejection followed by closure.
	 */
	class Server final: public Network::Server {
		public:
			/**
			 * @brief Start with no-op pipelines and the consumer's packet factory.
			 */
			Server(std::filesystem::path read_path = {}, std::filesystem::path write_path = {}):
				Network::Server(Factory(), {}), m_read_path(std::move(read_path)), m_write_path(std::move(write_path)) {
			}

			/**
			 * @brief Join callbacks before destroying their session state.
			 */
			~Server() noexcept override {
				Disconnect();
			}

			/**
			 * @brief Count actual handlers, including the rejected Hello.
			 * @return Number of application dispatches.
			 */
			unsigned int Handled() const noexcept {
				return m_handled.load();
			}

			/**
			 * @brief Inspect whether disconnect hooks reclaimed all policy entries.
			 * @return Number of live application sessions.
			 */
			std::size_t Sessions() const noexcept {
				std::scoped_lock lock(m_mutex);
				return m_sessions.size();
			}

		protected:
			/**
			 * @brief Register a live session before packet workers may use it.
			 * @param uuid Server-generated identity.
			 * @return True to admit the transport, without authenticating it.
			 */
			bool OnClientConnected(std::string_view uuid) noexcept override {
				try {
					std::scoped_lock lock(m_mutex);
					return m_sessions.emplace(std::string{uuid}, false).second;
				} catch (...) {
					return false;
				}
			}

			/**
			 * @brief Remove state without letting late handlers recreate authorization.
			 * @param uuid Disconnected session.
			 */
			void OnClientDisconnected(std::string_view uuid) noexcept override {
				std::scoped_lock lock(m_mutex);
				m_sessions.erase(std::string{uuid});
			}

			/**
			 * @brief Gate every raw frame before transformation and factory invocation.
			 * @param uuid Session identity.
			 * @param opcode Wire opcode.
			 * @return Hello before negotiation, Echo after acceptance, false otherwise.
			 */
			bool AllowIncomingOpcode(std::string_view uuid, Transport::Packet::OpcodeType opcode) const noexcept override {
				std::scoped_lock lock(m_mutex);
				const auto session = m_sessions.find(std::string{uuid});
				if (session == m_sessions.end())
					return false;
				if (!session->second)
					return opcode == static_cast<unsigned short>(Operation::Hello);
				return opcode == static_cast<unsigned short>(Operation::Echo)
					|| opcode == static_cast<unsigned short>(Operation::MountRead)
					|| opcode == static_cast<unsigned short>(Operation::MountWrite);
			}

			/**
			 * @brief Negotiate the example algorithm or reject an untrusted key label.
			 * @param uuid Live session requesting negotiation.
			 * @param incoming Consumer-decoded message.
			 * @return Welcome, Rejected or a protected echo response.
			 */
			PacketPointer ProcessClientPacket(std::string_view uuid, PacketPointer incoming) noexcept override {
				try {
					++m_handled;
					auto packet = Safe::DynamicPointerCast<Packet>(incoming);
					if (!packet)
						return {};
					if (packet->Opcode() == static_cast<unsigned short>(Operation::Hello)) {
						if (packet->Fields()[0] != "pubkey_contents") {
							DisconnectClientAfterReply(uuid);
							return PacketPointer::MakePointer<Packet>(Operation::Rejected,
								std::vector<std::string>{"client_not_trusted"});
						}
						std::random_device random;
						unsigned char secret;
						{
							std::scoped_lock lock(m_mutex);
							if (m_used_secrets.size() >= 255)
								return {};
							do {
								secret = static_cast<unsigned char>(std::uniform_int_distribution<unsigned int>(1, 255)(random));
							} while (!m_used_secrets.insert(secret).second);
						}
						if (ConfigureClientPipelines(uuid, XorPipeline(secret), UnclonablePipeline()))
							return {};
						if (!ConfigureClientPipelines(uuid, XorPipeline(secret), XorPipeline(secret))
							|| ConfigureClientPipelines(uuid, XorPipeline(secret), XorPipeline(secret)))
							return {};
						{
							std::scoped_lock lock(m_mutex);
							const auto session = m_sessions.find(std::string{uuid});
							if (session == m_sessions.end())
								return {};
							session->second = true;
						}
						return PacketPointer::MakePointer<Packet>(Operation::Welcome,
							std::vector<std::string>{"xor", std::to_string(secret)});
					}
					++protected_handlers;
					if (packet->Opcode() == static_cast<unsigned short>(Operation::MountRead)
						|| packet->Opcode() == static_cast<unsigned short>(Operation::MountWrite)) {
						const auto mount = packet->Opcode() == static_cast<unsigned short>(Operation::MountRead)
							? MountRemoteFileReader(uuid, m_read_path, 3) : MountRemoteFileWriter(uuid, m_write_path, 3);
						const auto encoded = Serializable<RemoteFileMount>(mount).Serialize();
						return PacketPointer::MakePointer<Packet>(Operation::MountReply,
							std::vector<std::string>{std::string(reinterpret_cast<const char*>(encoded.data()),
								static_cast<std::size_t>(encoded.size()))});
					}
					return PacketPointer::MakePointer<Packet>(Operation::EchoReply, packet->Fields());
				} catch (...) {
					DisconnectClient(uuid);
					return {};
				}
			}

		private:
			/**
			 * @brief Protect policy read by the event loop and updated by workers.
			 */
			mutable std::mutex m_mutex;

			/**
			 * @brief Live session identities and their negotiated state.
			 */
			std::map<std::string, bool> m_sessions;

			/**
			 * @brief Count handler entry independently of wire bytes.
			 */
			std::atomic<unsigned int> m_handled{0};

			/**
			 * @brief Distinct random masks allow the fixture to verify per-client isolation.
			 */
			std::set<unsigned char> m_used_secrets;

			/**
			 * @brief Application-authorized read fixture, never received from the peer.
			 */
			std::filesystem::path m_read_path;

			/**
			 * @brief Application-authorized write fixture.
			 */
			std::filesystem::path m_write_path;
	};

	/**
	 * @brief Consumer client installing its negotiated pipelines exactly once.
	 */
	class Client final: public Network::Client {
		public:
			/**
			 * @brief Construct with no negotiated pipeline state.
			 */
			Client(): Network::Client(Factory(), {}) {
			}

			/**
			 * @brief Send the simulated public key and configure XOR after Welcome.
			 * @return Whether algorithm validation and one-time configuration succeeded.
			 */
			bool Handshake() {
				Packet hello(Operation::Hello, {"pubkey_contents"});
				auto response = Safe::DynamicPointerCast<Packet>(Send(hello));
				if (!response || response->Opcode() != static_cast<unsigned short>(Operation::Welcome)
					|| response->Fields()[0] != "xor")
					return false;
				unsigned int secret = 0;
				for (const char character: response->Fields()[1]) {
					if (character < '0' || character > '9' || secret > 25)
						return false;
					secret = secret * 10 + static_cast<unsigned int>(character - '0');
				}
				if (secret == 0 || secret > 255)
					return false;
				m_secret = static_cast<unsigned char>(secret);
				m_ready = ConfigurePipelines(XorPipeline(static_cast<unsigned char>(secret)),
					XorPipeline(static_cast<unsigned char>(secret)));
				return m_ready && !ConfigurePipelines({}, {});
			}

			/**
			 * @brief Round-trip a transformed application payload.
			 * @return Whether the decoded echo matches.
			 */
			bool Echo() {
				Packet request(Operation::Echo, {"protected_test_payload"});
				auto response = Safe::DynamicPointerCast<Packet>(Send(request));
				return response && response->Opcode() == static_cast<unsigned short>(Operation::EchoReply)
					&& response->Fields()[0] == "protected_test_payload";
			}

			/**
			 * @brief Inspect the negotiated fictitious mask for isolation assertions.
			 * @return Test secret; never a production key.
			 */
			unsigned char Secret() const noexcept {
				return m_secret;
			}

			/**
			 * @brief Attempt a forbidden second installation without changing existing state.
			 * @return Whether Network incorrectly accepted replacement.
			 */
			bool Reconfigure() {
				return ConfigurePipelines(XorPipeline(0xFF), XorPipeline(0xFF));
			}

			/**
			 * @brief Exercise copy failure before negotiation without configuring half a pair.
			 * @return False when the provider refuses its template clone.
			 */
			bool FailConfiguration() {
				return ConfigurePipelines(XorPipeline(0xFF), UnclonablePipeline());
			}

			/**
			 * @brief Reconnect the same object with unnegotiated application policy.
			 * @return Whether a new transport session connected.
			 */
			bool Reconnect() {
				Disconnect();
				m_ready = false;
				m_secret = 0;
				return Connect(Connection::Protocol::IPv4, "127.0.0.1", port);
			}

			/**
			 * @brief Obtain an application-authorized mount over the negotiated control plane.
			 * @param write Whether to request the writer fixture.
			 * @return Validated public descriptor.
			 */
			RemoteFileMount Mount(bool write) {
				Packet request(write ? Operation::MountWrite : Operation::MountRead, {"fixture"});
				auto response = Safe::DynamicPointerCast<Packet>(Send(request));
				Check(response && response->Opcode() == static_cast<unsigned short>(Operation::MountReply), "mount reply missing");
				const auto& bytes = response->Fields()[0];
				auto mount = Serializable<RemoteFileMount>::Deserialize(std::span<const std::byte>{
					reinterpret_cast<const std::byte*>(bytes.data()), bytes.size()});
				Check(mount && mount->Result() == RemoteFileMount::Status::Authorized, "fixture mount was not authorized");
				return std::move(*mount);
			}

			/**
			 * @brief Attach a reader using independent copies of negotiated pipeline state.
			 * @param mount Authorized read descriptor.
			 * @return Remote reader owner.
			 */
			RemoteFileReaderHandle Reader(const RemoteFileMount& mount) {
				return CreateRemoteFileReader(mount);
			}

			/**
			 * @brief Attach a writer using the same negotiated configuration as control.
			 * @param mount Authorized write descriptor.
			 * @return Remote writer owner.
			 */
			RemoteFileWriterHandle Writer(const RemoteFileMount& mount) {
				return CreateRemoteFileWriter(mount);
			}

		protected:
			/**
			 * @brief Reject responses outside the client's negotiation state.
			 * @param uuid Local transport identity.
			 * @param opcode Response opcode before decoding.
			 * @return Whether this response is expected in the current state.
			 */
			bool AllowIncomingOpcode(std::string_view uuid, Transport::Packet::OpcodeType opcode) const noexcept override {
				(void)uuid;
				return m_ready ? opcode == static_cast<unsigned short>(Operation::EchoReply)
						|| opcode == static_cast<unsigned short>(Operation::MountReply)
					: opcode == static_cast<unsigned short>(Operation::Welcome)
						|| opcode == static_cast<unsigned short>(Operation::Rejected);
			}

		private:
			/**
			 * @brief Configuration is reset by constructing a new connection fixture.
			 */
			bool m_ready{false};

			/**
			 * @brief Fictitious mask installed by the last successful negotiation.
			 */
			unsigned char m_secret{0};
	};

	/**
	 * @brief Encode raw application framing without a pipeline.
	 * @param operation Opcode to frame.
	 * @param field One application field.
	 * @return Complete frame bytes for a coalesced TCP write.
	 */
	BinaryData Wire(Operation operation, std::string field) {
		BinaryData payload = Serializable<std::vector<std::string>>(std::vector<std::string>{std::move(field)}).Serialize();
		BinaryData frame = Serializable<Transport::Packet::OpcodeType>(static_cast<unsigned short>(operation)).Serialize();
		frame.append(Serializable<std::size_t>(payload.size()).Serialize());
		frame.append(std::move(payload));
		return frame;
	}

	/**
	 * @brief Own a short-lived directory for negotiated file-plane checks.
	 */
	class Files final {
		public:
			/**
			 * @brief Create a uniquely named fixture directory.
			 */
			Files(): m_path(std::filesystem::temp_directory_path() / ("stormbyte-handshake-"
				+ std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()))) {
				std::filesystem::create_directories(m_path);
			}

			/**
			 * @brief Remove fixture paths without throwing during teardown.
			 */
			~Files() {
				std::error_code error;
				std::filesystem::remove_all(m_path, error);
			}

			/**
			 * @brief Obtain one server-local fixture path.
			 * @param name Fixture name.
			 * @return Full path.
			 */
			std::filesystem::path Path(std::string_view name) const {
				return m_path / name;
			}

		private:
			/**
			 * @brief Directory exclusively owned by the fixture.
			 */
			std::filesystem::path m_path;
	};

	/**
	 * @brief OS TCP peer used to coalesce frames without accessing private library ABI.
	 */
	class RawPeer final {
		public:
			/**
			 * @brief Connect to the fixture and bound socket waits to two seconds.
			 */
			RawPeer() {
				m_socket = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
#ifdef WINDOWS
				Check(m_socket != INVALID_SOCKET, "raw socket creation failed");
				DWORD timeout = 2000;
				setsockopt(m_socket, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&timeout), sizeof(timeout));
#else
				Check(m_socket >= 0, "raw socket creation failed");
				timeval timeout{2, 0};
				setsockopt(m_socket, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
#endif
				sockaddr_in address{};
				address.sin_family = AF_INET;
				address.sin_port = htons(port);
				inet_pton(AF_INET, "127.0.0.1", &address.sin_addr);
				if (::connect(m_socket, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) != 0) {
					Close();
					throw Network::Exception("raw peer Connect failed");
				}
			}

			/**
			 * @brief Release the socket after the test, never before testing server EOF.
			 */
			~RawPeer() {
				Close();
			}

			/**
			 * @brief Prevent duplicate socket ownership.
			 */
			RawPeer(const RawPeer&) = delete;

			/**
			 * @brief Prevent duplicate socket assignment.
			 */
			RawPeer& operator=(const RawPeer&) = delete;

			/**
			 * @brief Send all framed bytes, handling partial writes.
			 * @param bytes Complete coalesced input.
			 */
			void Send(std::span<const std::byte> bytes) {
				while (!bytes.empty()) {
#ifdef LINUX
					constexpr int flags = MSG_NOSIGNAL;
#else
					constexpr int flags = 0;
#endif
					const auto sent = ::send(m_socket, reinterpret_cast<const char*>(bytes.data()),
						static_cast<int>(bytes.size()), flags);
					Check(sent > 0, "coalesced Send failed");
					bytes = bytes.subspan(static_cast<std::size_t>(sent));
				}
			}

			/**
			 * @brief Receive an exact frame portion within the socket deadline.
			 * @param size Number of bytes expected.
			 * @return Complete received bytes.
			 */
			BinaryData Receive(std::size_t size) {
				BinaryData bytes{ByteSize{size}};
				std::size_t offset = 0;
				while (offset < size) {
					const auto received = ::recv(m_socket, reinterpret_cast<char*>(bytes.data() + offset),
						static_cast<int>(size - offset), 0);
					Check(received > 0, "Rejected frame missing or truncated");
					offset += static_cast<std::size_t>(received);
				}
				return bytes;
			}

			/**
			 * @brief Require EOF rather than a timeout or another response byte.
			 * @return Whether the server closed its sending side after Rejected.
			 */
			bool ServerClosed() {
				char byte{};
				return ::recv(m_socket, &byte, 1, 0) == 0;
			}

			/**
			 * @brief Receive a validated application frame and undo the negotiated test mask.
			 * @param expected Expected response opcode.
			 * @param secret Zero for an untransformed handshake response.
			 * @return Decoded field vector.
			 */
			std::vector<std::string> Fields(Operation expected, unsigned char secret = 0) {
				const auto header = Receive(sizeof(Transport::Packet::OpcodeType) + sizeof(std::size_t));
				const auto opcode = Serializable<Transport::Packet::OpcodeType>::Deserialize(
					std::span<const std::byte>{header.data(), sizeof(Transport::Packet::OpcodeType)});
				const auto size = Serializable<std::size_t>::Deserialize(std::span<const std::byte>{
					header.data() + sizeof(Transport::Packet::OpcodeType), sizeof(std::size_t)});
				Check(opcode && *opcode == static_cast<unsigned short>(expected) && size
					&& *size > 0 && *size < 4096, "unexpected raw response framing");
				auto payload = Receive(*size);
				if (*opcode >= Transport::Packet::PROCESS_THRESHOLD)
					for (auto& byte: payload)
						byte ^= static_cast<std::byte>(secret);
				auto fields = Serializable<std::vector<std::string>>::Deserialize(payload);
				Check(static_cast<bool>(fields), "raw response fields invalid");
				return std::move(*fields);
			}

		private:
			/**
			 * @brief Release the native socket exactly once.
			 */
			void Close() noexcept {
#ifdef WINDOWS
				if (m_socket != INVALID_SOCKET) {
					closesocket(m_socket);
					m_socket = INVALID_SOCKET;
				}
#else
				if (m_socket >= 0) {
					::close(m_socket);
					m_socket = -1;
				}
#endif
			}

			/**
			 * @brief Native handle owned exclusively by this fixture.
			 */
#ifdef WINDOWS
			SOCKET m_socket{INVALID_SOCKET};
#else
			int m_socket{-1};
#endif
	};
}

// -------------------
// Handshake
// -------------------
int test_handshake_accepts_and_configures_once() {
	using namespace HandshakeExample;
	HandshakeExample::Server server;
	Check(server.Connect(Connection::Protocol::IPv4, "127.0.0.1", port), "server Connect failed");
	HandshakeExample::Client client;
	Check(client.Connect(Connection::Protocol::IPv4, "127.0.0.1", port), "client Connect failed");
	Check(client.Handshake(), "trusted Hello did not configure both endpoints");
	transformed_bytes.store(0);
	Check(client.Echo() && client.Echo(), "negotiated XOR messages failed");
	Check(transformed_bytes.load() > 0 && !client.Reconfigure() && client.Echo(),
		"pipelines were not executed or a second configuration changed them");
	client.Disconnect();
	server.Disconnect();
	return 0;
}

int test_handshake_failed_clone_preserves_noop_connection() {
	using namespace HandshakeExample;
	HandshakeExample::Server server;
	Check(server.Connect(Connection::Protocol::IPv4, "127.0.0.1", port), "server Connect failed");
	HandshakeExample::Client client;
	Check(client.Connect(Connection::Protocol::IPv4, "127.0.0.1", port), "client Connect failed");
	Check(!client.FailConfiguration(), "clone failure incorrectly accepted configuration");
	Check(client.Handshake() && client.Echo(), "failed clone replaced pipelines or sealed an unconfigured connection");
	client.Disconnect();
	server.Disconnect();
	return 0;
}

int test_handshake_keeps_client_secrets_isolated() {
	using namespace HandshakeExample;
	HandshakeExample::Server server;
	Check(server.Connect(Connection::Protocol::IPv4, "127.0.0.1", port), "server Connect failed");
	HandshakeExample::Client first;
	HandshakeExample::Client second;
	Check(first.Connect(Connection::Protocol::IPv4, "127.0.0.1", port) && first.Handshake(), "first handshake failed");
	Check(second.Connect(Connection::Protocol::IPv4, "127.0.0.1", port) && second.Handshake(), "second handshake failed");
	Check(first.Secret() != second.Secret(), "fixture did not negotiate independent secrets");
	Check(first.Echo() && second.Echo() && first.Echo() && second.Echo(), "one UUID configuration changed another session");
	first.Disconnect();
	second.Disconnect();
	server.Disconnect();
	Check(server.Sessions() == 0, "disconnect hooks retained negotiated session state");
	return 0;
}

int test_handshake_pipelines_apply_to_file_planes() {
	using namespace HandshakeExample;
	Files files;
	const auto read_path = files.Path("read.bin");
	const auto write_path = files.Path("write.bin");
	const std::string expected = [] {
		std::string bytes(512, '\0');
		for (std::size_t index = 0; index < bytes.size(); ++index)
			bytes[index] = static_cast<char>(index % 256);
		return bytes;
	}();
	{
		std::ofstream fixture(read_path, std::ios::binary);
		fixture.write(expected.data(), static_cast<std::streamsize>(expected.size()));
		Check(static_cast<bool>(fixture), "read fixture creation failed");
	}
	HandshakeExample::Server server(read_path, write_path);
	Check(server.Connect(Connection::Protocol::IPv4, "127.0.0.1", port), "server Connect failed");
	HandshakeExample::Client client;
	Check(client.Connect(Connection::Protocol::IPv4, "127.0.0.1", port) && client.Handshake(), "file handshake failed");
	const auto read_mount = client.Mount(false);
	transformed_bytes.store(0);
	auto reader = client.Reader(read_mount);
	Check(reader && reader->Open(), "negotiated reader Attach/Open failed");
	BinaryData received{ByteSize{expected.size()}};
	const auto result = reader->Read(std::span<std::byte>{received.data(), static_cast<std::size_t>(received.size())});
	Check(result.count == ByteSize{expected.size()} && std::string_view{
		reinterpret_cast<const char*>(received.data()), static_cast<std::size_t>(received.size())} == expected,
		"file-plane decode did not preserve bytes");
	Check(transformed_bytes.load() > 0, "file plane bypassed negotiated pipelines");
	Check(!client.Reconfigure(), "configuration was replaced after creating a file plane");
	Check(reader->Close().status == Buffer::IO::Status::Ok, "negotiated reader Close failed");
	reader.reset();
	const auto write_mount = client.Mount(true);
	transformed_bytes.store(0);
	auto writer = client.Writer(write_mount);
	Check(writer && writer->Open(), "negotiated writer Attach/Open failed");
	Check(writer->Write(std::span<const std::byte>{reinterpret_cast<const std::byte*>(expected.data()), expected.size()}).status
		== Buffer::IO::Status::Ok && writer->Flush().status == Buffer::IO::Status::Ok, "negotiated file write failed");
	Check(writer->Close(), "negotiated writer Close failed");
	writer.reset();
	Check(transformed_bytes.load() > 0, "writer file plane bypassed negotiated transformations");
	Check(std::filesystem::file_size(write_path) == expected.size(), "transport framing or transformed bytes leaked into the disk file");
	std::ifstream written(write_path, std::ios::binary);
	std::string bytes(expected.size(), '\0');
	written.read(bytes.data(), static_cast<std::streamsize>(bytes.size()));
	Check(written && bytes == expected, "negotiated writer changed disk bytes");
	Check(client.Echo(), "file traffic changed the control-plane pipeline state");
	client.Disconnect();
	server.Disconnect();
	return 0;
}

int test_handshake_reconnect_requires_new_negotiation() {
	using namespace HandshakeExample;
	HandshakeExample::Server server;
	Check(server.Connect(Connection::Protocol::IPv4, "127.0.0.1", port), "server Connect failed");
	HandshakeExample::Client client;
	Check(client.Connect(Connection::Protocol::IPv4, "127.0.0.1", port) && client.Handshake() && client.Echo(),
		"initial negotiated session failed");
	const auto previous_secret = client.Secret();
	Check(client.Reconnect(), "reconnect failed");
	Check(client.Handshake() && client.Secret() != previous_secret && client.Echo(), "new connection inherited negotiated state");
	client.Disconnect();
	server.Disconnect();
	Check(server.Sessions() == 0, "reconnected session state leaked");
	return 0;
}

int test_handshake_rejection_discards_coalesced_messages() {
	using namespace HandshakeExample;
	HandshakeExample::Server server;
	Check(server.Connect(Connection::Protocol::IPv4, "127.0.0.1", port), "server Connect failed");
	RawPeer peer;
	protected_decodes.store(0);
	BinaryData batch = Wire(Operation::Hello, "valid_but_untrusted_pubkey_contents");
	for (unsigned int index = 0; index < 4; ++index)
		batch.append(Wire(Operation::Echo, "must_not_be_processed"));
	peer.Send(std::span<const std::byte>{batch});
	constexpr std::size_t header_size = sizeof(Transport::Packet::OpcodeType) + sizeof(std::size_t);
	const auto header = peer.Receive(header_size);
	const auto opcode = Serializable<Transport::Packet::OpcodeType>::Deserialize(
		std::span<const std::byte>{header.data(), sizeof(Transport::Packet::OpcodeType)});
	const auto size = Serializable<std::size_t>::Deserialize(
		std::span<const std::byte>{header.data() + sizeof(Transport::Packet::OpcodeType), sizeof(std::size_t)});
	Check(opcode && *opcode == static_cast<unsigned short>(Operation::Rejected)
		&& size && *size > 0 && *size < 1024, "unexpected rejection frame");
	const auto payload = peer.Receive(*size);
	const auto fields = Serializable<std::vector<std::string>>::Deserialize(payload);
	Check(fields && fields->size() == 1 && fields->front() == "client_not_trusted", "policy rejection reason missing");
	Check(peer.ServerClosed(), "server did not close after Rejected or sent a pending response");
	Check(server.Handled() == 1 && protected_decodes.load() == 0, "pending messages reached factory or handler");
	HandshakeExample::Client legitimate;
	Check(legitimate.Connect(Connection::Protocol::IPv4, "127.0.0.1", port)
		&& legitimate.Handshake() && legitimate.Echo(), "rejected peer prevented a trusted session");
	legitimate.Disconnect();
	server.Disconnect();
	return 0;
}

int test_handshake_rejects_opcode_before_factory() {
	using namespace HandshakeExample;
	HandshakeExample::Server server;
	Check(server.Connect(Connection::Protocol::IPv4, "127.0.0.1", port), "server Connect failed");
	RawPeer peer;
	protected_decodes.store(0);
	transformed_bytes.store(0);
	peer.Send(std::span<const std::byte>{Wire(Operation::Echo, "not_authorized")});
	Check(peer.ServerClosed(), "protected opcode before Hello was not rejected");
	Check(protected_decodes.load() == 0 && transformed_bytes.load() == 0 && server.Handled() == 0,
		"rejected opcode reached pipeline, factory or handler");
	server.Disconnect();
	return 0;
}

int test_handshake_rejects_plaintext_coalesced_after_hello() {
	using namespace HandshakeExample;
	HandshakeExample::Server server;
	Check(server.Connect(Connection::Protocol::IPv4, "127.0.0.1", port), "server Connect failed");
	RawPeer peer;
	protected_decodes.store(0);
	transformed_bytes.store(0);
	BinaryData batch = Wire(Operation::Hello, "pubkey_contents");
	batch.append(Wire(Operation::Echo, "plaintext_after_handshake"));
	peer.Send(std::span<const std::byte>{batch});
	const auto welcome = peer.Fields(Operation::Welcome);
	Check(welcome.size() == 2 && welcome[0] == "xor", "trusted Hello was not accepted");
	Check(peer.ServerClosed(), "post-Hello plaintext bypassed newly installed pipeline");
	Check(server.Handled() == 1 && protected_decodes.load() == 1 && transformed_bytes.load() > 0,
		"coalesced frame was processed before negotiation or reached its handler");
	server.Disconnect();
	return 0;
}

int test_handshake_transforms_coalesced_accepted_messages() {
	using namespace HandshakeExample;
	HandshakeExample::Server server;
	Check(server.Connect(Connection::Protocol::IPv4, "127.0.0.1", port), "server Connect failed");
	RawPeer peer;
	peer.Send(std::span<const std::byte>{Wire(Operation::Hello, "pubkey_contents")});
	const auto welcome = peer.Fields(Operation::Welcome);
	Check(welcome.size() == 2 && welcome[0] == "xor", "negotiation algorithm missing");
	const auto secret = static_cast<unsigned char>(std::stoul(welcome[1]));
	Check(secret != 0, "invalid fixture secret");
	BinaryData batch;
	for (unsigned int index = 0; index < 4; ++index) {
		auto frame = Wire(Operation::Echo, "coalesced_protected_payload");
		constexpr std::size_t header_size = sizeof(Transport::Packet::OpcodeType) + sizeof(std::size_t);
		for (std::size_t offset = header_size; offset < static_cast<std::size_t>(frame.size()); ++offset)
			frame[offset] ^= static_cast<std::byte>(secret);
		batch.append(std::move(frame));
	}
	protected_decodes.store(0);
	peer.Send(std::span<const std::byte>{batch});
	for (unsigned int index = 0; index < 4; ++index) {
		const auto reply = peer.Fields(Operation::EchoReply, secret);
		Check(reply.size() == 1 && reply[0] == "coalesced_protected_payload", "coalesced payload decrypted out of order");
	}
	Check(protected_decodes.load() == 4 && server.Handled() == 5, "accepted pending messages were lost or duplicated");
	server.Disconnect();
	return 0;
}

int main() {
	int result = 0;
	try {
		// -------------------
		// Handshake
		// -------------------
		result += test_handshake_accepts_and_configures_once();
		result += test_handshake_failed_clone_preserves_noop_connection();
		result += test_handshake_keeps_client_secrets_isolated();
		result += test_handshake_pipelines_apply_to_file_planes();
		result += test_handshake_reconnect_requires_new_negotiation();
		result += test_handshake_rejection_discards_coalesced_messages();
		result += test_handshake_rejects_opcode_before_factory();
		result += test_handshake_rejects_plaintext_coalesced_after_hello();
		result += test_handshake_transforms_coalesced_accepted_messages();
	} catch (const StormByte::Exception& error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
	std::cout << "Handshake tests " << (result == 0 ? "passed" : "failed") << '\n';
	return result;
}