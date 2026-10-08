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

#include <StormByte/logger/threaded_log.hxx>
#include <StormByte/network/client.hxx>
#include <StormByte/network/remote_file.hxx>
#include <StormByte/network/server.hxx>
#include <StormByte/safe/binary.hxx>
#include <StormByte/serializable.hxx>
#include <StormByte/test_handlers.h>

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

namespace RemoteFileTest {
	namespace Net = StormByte::Network;

	namespace Buf = StormByte::Buffer;

	using Log = StormByte::Logger::Log;

	using Mount = Net::RemoteFileMount;

	using Packet = Net::Transport::Packet;

	constexpr std::string_view address = "127.0.0.1";

	constexpr unsigned short port = 7183;

	/**
	 * @brief Retrieve the shared test logger.
	 * @return Logger used by the test endpoints.
	 */
	StormByte::Safe::Shared<Log> Logger() {
		static auto logger = StormByte::Safe::MakeShared<StormByte::Logger::ThreadedLog>(
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
			StormByte::Safe::Binary DoSerialize() const noexcept override {
				StormByte::Safe::Binary data = StormByte::Serializable<std::uint8_t>(m_missing ? 1 : 0).Serialize();
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
			StormByte::Safe::Binary DoSerialize() const noexcept override {
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
			StormByte::Safe::Binary DoSerialize() const noexcept override {
				return {};
			}
	};

}

STORMBYTE_DECLARE_MAYBE_SAFE(RemoteFileTest::Request);
STORMBYTE_DECLARE_MAYBE_SAFE(RemoteFileTest::MountPacket);
STORMBYTE_DECLARE_MAYBE_SAFE(RemoteFileTest::UnauthorizedPacket);

namespace RemoteFileTest {

	/**
	 * @brief Create the test control packet deserializer.
	 * @return Deserializer for application test packets.
	 */
	Net::DeserializePacketFunction Factory() {
		return [](Packet::OpcodeType opcode, Buf::Consumer payload, StormByte::Safe::Shared<Log>) -> Net::PacketPointer {
			StormByte::Safe::Binary bytes;
			payload.ExtractUntilEoF(bytes);
			switch (static_cast<AppOpcode>(opcode)) {
				case AppOpcode::ReadRequest:
				case AppOpcode::WriteRequest: {
					if (bytes.size() != StormByte::ByteSize{sizeof(std::uint8_t) + sizeof(std::uint16_t)})
						return nullptr;
					auto flags = StormByte::Serializable<std::uint8_t>::Deserialize(
						bytes.span().first(sizeof(std::uint8_t)));
					auto path_selection = StormByte::Serializable<std::uint16_t>::Deserialize(
						bytes.span().subspan(sizeof(std::uint8_t), sizeof(std::uint16_t)));
					if (!flags || (*flags & 0xFE) != 0 || !path_selection)
						return nullptr;
					return StormByte::Network::PacketPointer::MakePointer<Request>(opcode == static_cast<Packet::OpcodeType>(AppOpcode::WriteRequest),
						(*flags & 1) != 0, *path_selection);
				}
				case AppOpcode::Mount: {
					auto mount = StormByte::Serializable<Mount>::Deserialize(bytes.span());
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
	void ReadPipeUntilEoF(const Buf::PipeInput& input, StormByte::Safe::Binary& bytes) {
		while (!input.EoF() && input.IsReadable()) {
			StormByte::Safe::Binary chunk;
			if (!input.Read(StormByte::ByteSize{1}, chunk))
				break;
			bytes.append(chunk);
			const auto available = input.Available();
			if (available > StormByte::ByteSize{0}) {
				StormByte::Safe::Binary rest;
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
				StormByte::Safe::Binary bytes;
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
				StormByte::Safe::Binary bytes;
				ReadPipeUntilEoF(input, bytes);
				if (m_encode) {
					for (auto& byte: bytes)
						byte ^= std::byte{0x67};
					StormByte::Safe::Binary framed{std::byte{'A'}, std::byte{'E'}, std::byte{'A'}, std::byte{'D'}};
					framed.append(bytes);
					framed.append(StormByte::Serializable<std::uint64_t>(Tag(bytes)).Serialize());
					(void)output.Write(std::move(framed));
				}
				else if (bytes.size() >= StormByte::ByteSize{12} && bytes[0] == std::byte{'A'} && bytes[1] == std::byte{'E'}
					&& bytes[2] == std::byte{'A'} && bytes[3] == std::byte{'D'}) {
					const std::size_t body_size = static_cast<std::size_t>(bytes.size()) - 12;
					const auto tag = StormByte::Serializable<std::uint64_t>::Deserialize(
						bytes.span().subspan(4 + body_size, sizeof(std::uint64_t)));
					StormByte::Safe::Binary body(bytes.span().subspan(4, body_size));
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
			static std::uint64_t Tag(const StormByte::Safe::Binary& bytes) noexcept {
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

}

STORMBYTE_DECLARE_MAYBE_SAFE(RemoteFileTest::XorPipe);
STORMBYTE_DECLARE_MAYBE_SAFE(RemoteFileTest::ReversibleEnvelopePipe);

namespace RemoteFileTest {

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
			 * @brief Disconnect before destroying fixture paths used by packet handlers.
			 */
			~Server() noexcept override {
				Disconnect();
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
				const auto path_text = path.string();
				Mount mount = write ? MountRemoteFileWriter(uuid, path_text, 3) : MountRemoteFileReader(uuid, path_text, 3);
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

}

STORMBYTE_DECLARE_MAYBE_SAFE(RemoteFileTest::Client);
STORMBYTE_DECLARE_MAYBE_SAFE(RemoteFileTest::Server);

namespace RemoteFileTest {

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

	StormByte::Safe::Binary MakePattern(const std::size_t size) {
		StormByte::Safe::Binary bytes{StormByte::ByteSize{size}};
		for (std::size_t index = 0; index < size; ++index)
			bytes[index] = static_cast<std::byte>(index % 251);
		return bytes;
	}

	void WriteBytes(const std::filesystem::path& path, const StormByte::Safe::Binary& bytes) {
		std::ofstream file(path, std::ios::binary | std::ios::trunc);
		file.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
		Check(static_cast<bool>(file), "failed writing binary fixture");
	}

	StormByte::Safe::Binary ReadBytes(const std::filesystem::path& path) {
		std::ifstream file(path, std::ios::binary | std::ios::ate);
		Check(static_cast<bool>(file), "failed opening binary fixture");
		const std::streamsize length = file.tellg();
		Check(length >= 0, "failed measuring binary fixture");
		file.seekg(0, std::ios::beg);
		StormByte::Safe::Binary bytes{StormByte::ByteSize{static_cast<std::size_t>(length)}};
		if (length > 0)
			file.read(reinterpret_cast<char*>(bytes.data()), length);
		Check(static_cast<bool>(file), "failed reading binary fixture");
		return bytes;
	}

	bool EqualBytes(const StormByte::Safe::Binary& actual, const StormByte::Safe::Binary& expected) {
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
		std::array<Net::RemoteFileReaderHandle, 3> readers;
		std::optional<unsigned short> plane_port;
		for (auto& reader: readers) {
			auto [mount, rejected] = client.RequestMount(false);
			Check(!rejected && mount.Result() == Mount::Status::Authorized, "timeout mount failed");
			if (!plane_port)
				plane_port = mount.Port();
			Check(mount.Port() == *plane_port, "shutdown readers must share one plane");
			reader = client.AttachReader(mount);
			Check(reader && reader->Open(), "timeout reader open failed");
		}
		auto [writer_mount, writer_rejected] = client.RequestMount(true);
		Check(!writer_rejected && writer_mount.Result() == Mount::Status::Authorized
			&& writer_mount.Port() == *plane_port, "shutdown writer must share the readers' plane");
		auto writer = client.AttachWriter(writer_mount);
		Check(writer && writer->Open(), "shutdown writer open failed");
		server.Disconnect();
		std::array<std::byte, 1> byte{};
		(void)readers.front()->Read(std::span<std::byte>{byte});
		for (auto& reader: readers) {
			Check(!reader->Size(), "dead remote channel still reported a reader size");
			Check(reader->State() == Buf::IO::State::Fault, "dead remote channel did not mark BufferedLocationReader Fault");
			reader.reset();
		}
		Check(writer->Size() == StormByte::ByteSize{0}, "dead remote channel still reported a writer size");
		Check(writer->State() == Buf::IO::State::Fault, "dead remote channel did not mark BufferedLocationWriter Fault");
		writer.reset();
	}

	void ExercisePatternReader(Client& client, const std::filesystem::path& path) {
		const StormByte::Safe::Binary expected = ReadBytes(path);
		const StormByte::ByteSize length{std::filesystem::file_size(path)};
		auto [mount, rejected] = client.RequestMount(false, false, 1);
		Check(!rejected && mount.Result() == Mount::Status::Authorized, "pattern read mount failed");
		auto reader = client.AttachReader(mount);
		Check(reader && reader->Open(), "pattern reader open failed");
		const auto reader_size = reader->Size();
		Check(reader_size && *reader_size == length, "remote Size differs from filesystem::file_size");
		StormByte::Safe::Binary whole{length};
		const auto full = reader->Read(whole.span());
		Check(full.count == length && EqualBytes(whole, expected), "full read differed from the deterministic fixture");

		constexpr std::size_t middle_offset = 456789;
		std::array<std::byte, 257> middle{};
		Check(reader->Seek(static_cast<std::ptrdiff_t>(middle_offset), Buf::Position::Absolute).status == Buf::IO::Status::Ok,
			"middle logical seek failed");
		const auto middle_read = reader->Read(std::span<std::byte>{middle});
		Check(middle_read.count == StormByte::ByteSize{middle.size()}
			&& std::equal(middle.begin(), middle.end(), expected.begin() + static_cast<std::ptrdiff_t>(middle_offset)),
			"middle block differed from the fixture");

		constexpr std::size_t final_size = 37;
		const std::size_t final_offset = static_cast<std::size_t>(expected.size()) - final_size;
		std::array<std::byte, 64> final_block{};
		Check(reader->Seek(static_cast<std::ptrdiff_t>(final_offset), Buf::Position::Absolute).status == Buf::IO::Status::Ok,
			"final-block logical seek failed");
		const auto final_read = reader->Read(std::span<std::byte>{final_block});
		Check(final_read.count == StormByte::ByteSize{final_size} && final_read.status == Buf::IO::Status::End
			&& std::equal(final_block.begin(), final_block.begin() + static_cast<std::ptrdiff_t>(final_size),
				expected.begin() + static_cast<std::ptrdiff_t>(final_offset)),
			"short final block or End status was incorrect");

		Check(reader->Seek(static_cast<std::ptrdiff_t>(expected.size()) + 8, Buf::Position::Absolute).status == Buf::IO::Status::Ok,
			"past-EOF logical seek failed");
		std::array<std::byte, 1> past_eof{};
		const auto eof_read = reader->Read(std::span<std::byte>{past_eof});
		Check(eof_read.count == StormByte::ByteSize{0} && eof_read.status == Buf::IO::Status::End
			&& reader->State() != Buf::IO::State::Fault, "past-EOF read faulted");
		Check(reader->Close().status == Buf::IO::Status::Ok, "pattern reader CloseToken failed");
	}

	void ExerciseEightReaders(Client& client, const std::filesystem::path& path) {
		const StormByte::Safe::Binary expected = ReadBytes(path);
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
				Check(read.count == StormByte::ByteSize{bytes.size()}
					&& std::equal(bytes.begin(), bytes.end(), expected.begin() + static_cast<std::ptrdiff_t>(offset)),
					"interleaved reader got bytes from a different offset");
			}
		}
		for (auto& reader: readers)
			Check(reader->Close().status == Buf::IO::Status::Ok,
				"interleaved reader CloseToken failed");
	}

	void ExerciseTokenFaultIsolation(Client& client, const StormByte::Safe::Binary& expected) {
		auto [mount, rejected] = client.RequestMount(false);
		Check(!rejected && mount.Result() == Mount::Status::Authorized, "token-isolation mount failed");
		auto reader = client.AttachReader(mount);
		Check(reader && reader->Open(), "token-isolation reader open failed");

		StormByte::Safe::Binary forged_bytes = StormByte::Serializable<Mount>(mount).Serialize();
		forged_bytes.back() ^= std::byte{0x01};
		auto forged = StormByte::Serializable<Mount>::Deserialize(forged_bytes.span());
		Check(forged && !client.AttachReader(*forged), "unknown mount capability was registered");

		StormByte::Safe::Binary writer_bytes = StormByte::Serializable<Mount>(mount).Serialize();
		writer_bytes[11] = static_cast<std::byte>(Mount::Access::Write);
		auto forged_writer_mount = StormByte::Serializable<Mount>::Deserialize(writer_bytes.span());
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
		Check(read.count == StormByte::ByteSize{bytes.size()} && std::equal(bytes.begin(), bytes.end(), expected.begin()),
			"one token fault terminated other operations on the plane");
		Check(fresh_reader->Close().status == Buf::IO::Status::Ok, "fresh token CloseToken failed");
	}

	void ExercisePatternWriter(Client& client, const std::filesystem::path& path) {
		StormByte::Safe::Binary expected = MakePattern(1024 * 1024);
		WriteBytes(path, StormByte::Safe::Binary{});
		auto [mount, rejected] = client.RequestMount(true);
		Check(!rejected && mount.Result() == Mount::Status::Authorized, "pattern writer mount failed");
		auto writer = client.AttachWriter(mount);
		Check(writer && writer->Open(), "pattern writer open failed");
		Check(writer->Truncate().status == Buf::IO::Status::Ok, "pattern Truncate failed");
		const auto written = writer->Write(std::span<const std::byte>{expected.span()});
		Check(written.status == Buf::IO::Status::Ok && written.count == expected.size(),
			"full pattern write failed or accepted an unexpected byte count");
		const auto flush = writer->Flush();
		Check(flush.status == Buf::IO::Status::Ok, "pattern Flush returned status "
			+ std::to_string(static_cast<unsigned int>(flush.status)) + " with writer state "
			+ std::to_string(static_cast<unsigned int>(writer->State())));
		const StormByte::Safe::Binary flushed = ReadBytes(path);
		Check(flushed.size() == expected.size(), "pattern Flush left an unexpected file size: "
			+ std::to_string(static_cast<std::size_t>(flushed.size())) + " instead of "
			+ std::to_string(static_cast<std::size_t>(expected.size())));
		const auto mismatch = std::mismatch(flushed.begin(), flushed.end(), expected.begin());
		Check(mismatch.first == flushed.end(), "pattern Flush left incorrect bytes at offset "
			+ std::to_string(static_cast<std::size_t>(std::distance(flushed.begin(), mismatch.first))));

		const StormByte::Safe::Binary start_patch = MakePattern(23);
		Check(writer->Seek(0, Buf::Position::Absolute).status == Buf::IO::Status::Ok
			&& writer->Write(start_patch.span()).status == Buf::IO::Status::Ok,
			"offset-zero overwrite failed");
		std::copy(start_patch.begin(), start_patch.end(), expected.begin());
		constexpr std::size_t patch_offset = 500003;
		const StormByte::Safe::Binary patch = MakePattern(513);
		Check(writer->Seek(static_cast<StormByte::ByteSize>(patch_offset), Buf::Position::Absolute).status
			== Buf::IO::Status::Ok, "middle writer seek failed");
		Check(writer->Write(patch.span()).status == Buf::IO::Status::Ok,
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
		std::vector<StormByte::Safe::Binary> expected;
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
		std::vector<StormByte::Safe::Shared<Client>> clients;
		std::vector<Net::RemoteFileReaderHandle> readers;
		std::set<unsigned short> plane_ports;
		clients.reserve(peer_count);
		readers.reserve(peer_count * readers_per_peer);
		for (std::size_t peer = 0; peer < peer_count; ++peer) {
			auto client = StormByte::Safe::MakeShared<Client>();
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
				Check(result.count == StormByte::ByteSize{bytes.size()}
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

	template<typename TestFunction>
	int RunOne(const std::string_view name, TestFunction&& test) {
		try {
			if constexpr (requires { static_cast<int>(test()); }) {
				const int result = test();
				if (result != 0)
					return result;
			}
			else
				test();
			std::cout << name << " passed" << std::endl;
			return 0;
		} catch (const std::exception& exception) {
			std::cerr << name << " failed: " << exception.what() << std::endl;
			return 1;
		}
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
		const StormByte::Safe::Binary expected = MakePattern(4096);
		WriteBytes(read_path, expected);
		Server server(read_path, write_path);
		Check(server.Connect(Net::Connection::Protocol::IPv4, address, port), "token server connect failed");
		Client client;
		Check(client.Connect(Net::Connection::Protocol::IPv4, address, port), "token client connect failed");
		ExerciseTokenFaultIsolation(client, expected);
	}

	int test_foreign_mount_rejection_preserves_owner() {
		TempDirectory temporary;
		const auto read_path = temporary.Path() / "foreign-read.bin";
		const auto write_path = temporary.Path() / "unused-write.bin";
		const auto expected = MakePattern(32);
		WriteBytes(read_path, expected);
		Server server(read_path, write_path);
		ASSERT_TRUE(server.Connect(Net::Connection::Protocol::IPv4, address, port));
		Client owner;
		Client other;
		ASSERT_TRUE(owner.Connect(Net::Connection::Protocol::IPv4, address, port));
		ASSERT_TRUE(other.Connect(Net::Connection::Protocol::IPv4, address, port));
		auto [owned_mount, owner_rejected] = owner.RequestMount(false);
		auto [other_mount, other_rejected] = other.RequestMount(false);
		ASSERT_FALSE(owner_rejected);
		ASSERT_FALSE(other_rejected);
		ASSERT_EQUAL(Mount::Status::Authorized, owned_mount.Result());
		ASSERT_EQUAL(Mount::Status::Authorized, other_mount.Result());
		ASSERT_NOT_EQUAL(owned_mount.Port(), other_mount.Port());
		auto other_reader = other.AttachReader(other_mount);
		ASSERT_NOT_NULL(other_reader.get());
		ASSERT_TRUE(other_reader->Open());
		ASSERT_FALSE(other.AttachReader(owned_mount));
		auto owner_reader = owner.AttachReader(owned_mount);
		ASSERT_NOT_NULL(owner_reader.get());
		ASSERT_TRUE(owner_reader->Open());
		for (auto* reader: {owner_reader.get(), other_reader.get()}) {
			StormByte::Safe::Binary bytes{expected.size()};
			const auto read = reader->Read(bytes.span());
			ASSERT_EQUAL(Buf::IO::Status::Ok, read.status);
			ASSERT_EQUAL(StormByte::ByteSize{expected.size()}, read.count);
			ASSERT_SIZE(bytes, StormByte::ByteSize{expected.size()});
			ASSERT_EQUAL(expected, bytes);
			ASSERT_EQUAL(Buf::IO::Status::Ok, reader->Close().status);
		}
		ASSERT_EQUAL(expected, ReadBytes(read_path));
		RETURN_TEST(0);
	}

	int test_rejected_descriptors_and_wrong_access_factories() {
		TempDirectory temporary;
		const auto read_path = temporary.Path() / "factory-read.bin";
		const auto write_path = temporary.Path() / "factory-write.bin";
		const auto expected = MakePattern(32);
		WriteBytes(read_path, expected);
		WriteBytes(write_path, expected);
		Server server(read_path, write_path);
		ASSERT_TRUE(server.Connect(Net::Connection::Protocol::IPv4, address, port));
		Client client;
		ASSERT_TRUE(client.Connect(Net::Connection::Protocol::IPv4, address, port));
		for (const auto& rejected: {Mount::NotAuthorized(), Mount::Unavailable(), Mount::FileBeingRead(),
			Mount::FileBeingWritten(), Mount::Failed()}) {
			ASSERT_FALSE(client.AttachReader(rejected));
			ASSERT_FALSE(client.AttachWriter(rejected));
		}
		auto [read_mount, read_rejected] = client.RequestMount(false);
		auto [write_mount, write_rejected] = client.RequestMount(true);
		ASSERT_FALSE(read_rejected);
		ASSERT_FALSE(write_rejected);
		ASSERT_EQUAL(Mount::Status::Authorized, read_mount.Result());
		ASSERT_EQUAL(Mount::Status::Authorized, write_mount.Result());
		ASSERT_FALSE(client.AttachWriter(read_mount));
		ASSERT_FALSE(client.AttachReader(write_mount));
		auto reader = client.AttachReader(read_mount);
		auto writer = client.AttachWriter(write_mount);
		ASSERT_NOT_NULL(reader.get());
		ASSERT_NOT_NULL(writer.get());
		ASSERT_TRUE(reader->Open());
		ASSERT_TRUE(writer->Open());
		StormByte::Safe::Binary bytes{expected.size()};
		const auto read = reader->Read(bytes.span());
		ASSERT_EQUAL(Buf::IO::Status::Ok, read.status);
		ASSERT_EQUAL(StormByte::ByteSize{expected.size()}, read.count);
		ASSERT_EQUAL(expected, bytes);
		ASSERT_EQUAL(StormByte::ByteSize{expected.size()}, writer->Size());
		ASSERT_EQUAL(Buf::IO::Status::Ok, reader->Close().status);
		ASSERT_TRUE(writer->Close());
		ASSERT_EQUAL(expected, ReadBytes(read_path));
		ASSERT_EQUAL(expected, ReadBytes(write_path));
		RETURN_TEST(0);
	}

	// -------------------
	// Lifecycle
	// -------------------
	int test_closed_mount_rejected_and_fresh_token_usable() {
		TempDirectory temporary;
		const auto read_path = temporary.Path() / "closed-read.bin";
		const auto write_path = temporary.Path() / "unused-write.bin";
		const auto expected = MakePattern(32);
		WriteBytes(read_path, expected);
		Server server(read_path, write_path);
		ASSERT_TRUE(server.Connect(Net::Connection::Protocol::IPv4, address, port));
		Client client;
		ASSERT_TRUE(client.Connect(Net::Connection::Protocol::IPv4, address, port));
		auto [mount, rejected] = client.RequestMount(false);
		ASSERT_FALSE(rejected);
		ASSERT_EQUAL(Mount::Status::Authorized, mount.Result());
		auto reader = client.AttachReader(mount);
		ASSERT_NOT_NULL(reader.get());
		ASSERT_TRUE(reader->Open());
		ASSERT_EQUAL(Buf::IO::Status::Ok, reader->Close().status);
		reader.reset();
		ASSERT_FALSE(client.AttachReader(mount));
		auto [fresh, fresh_rejected] = client.RequestMount(false);
		ASSERT_FALSE(fresh_rejected);
		ASSERT_EQUAL(Mount::Status::Authorized, fresh.Result());
		ASSERT_NOT_EQUAL(mount.Token(), fresh.Token());
		ASSERT_EQUAL(mount.Port(), fresh.Port());
		auto fresh_reader = client.AttachReader(fresh);
		ASSERT_NOT_NULL(fresh_reader.get());
		ASSERT_TRUE(fresh_reader->Open());
		StormByte::Safe::Binary bytes{expected.size()};
		const auto read = fresh_reader->Read(bytes.span());
		ASSERT_EQUAL(Buf::IO::Status::Ok, read.status);
		ASSERT_EQUAL(StormByte::ByteSize{expected.size()}, read.count);
		ASSERT_EQUAL(expected, bytes);
		ASSERT_EQUAL(Buf::IO::Status::Ok, fresh_reader->Close().status);
		RETURN_TEST(0);
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

	// -------------------
	// Protocol
	// -------------------
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
		Check(read.count == StormByte::ByteSize{bytes.size()} && std::equal(bytes.begin(), bytes.end(), expected.begin()),
			"legitimate data changed after control mismatch");
		Check(reader->Close().status == Buf::IO::Status::Ok, "legitimate Close failed after control mismatch");
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

	int test_empty_file_eof() {
		TempDirectory temporary;
		const auto read_path = temporary.Path() / "empty-read.bin";
		const auto write_path = temporary.Path() / "unused-write.bin";
		WriteBytes(read_path, StormByte::Safe::Binary{});
		Server server(read_path, write_path);
		ASSERT_TRUE(server.Connect(Net::Connection::Protocol::IPv4, address, port));
		Client client;
		ASSERT_TRUE(client.Connect(Net::Connection::Protocol::IPv4, address, port));
		auto [mount, rejected] = client.RequestMount(false);
		ASSERT_FALSE(rejected);
		ASSERT_EQUAL(Mount::Status::Authorized, mount.Result());
		auto reader = client.AttachReader(mount);
		ASSERT_NOT_NULL(reader.get());
		ASSERT_TRUE(reader->Open());
		const auto size = reader->Size();
		ASSERT_TRUE(size);
		ASSERT_EQUAL(StormByte::ByteSize{0}, *size);
		StormByte::Safe::Binary bytes{std::byte{0x5A}};
		const auto read = reader->Read(bytes.span());
		ASSERT_EQUAL(Buf::IO::Status::End, read.status);
		ASSERT_EQUAL(StormByte::ByteSize{0}, read.count);
		ASSERT_SIZE(bytes, StormByte::ByteSize{1});
		ASSERT_EQUAL(std::byte{0x5A}, bytes[0]);
		ASSERT_TRUE(reader->EoF());
		ASSERT_NOT_EQUAL(Buf::IO::State::Fault, reader->State());
		ASSERT_EQUAL(Buf::IO::Status::Ok, reader->Close().status);
		ASSERT_EMPTY(ReadBytes(read_path));
		RETURN_TEST(0);
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

	void test_sixteen_peers_with_eight_readers() {
		TempDirectory temporary;
		const auto read_path = temporary.Path() / "unused-read.bin";
		const auto write_path = temporary.Path() / "unused-write.bin";
		ExerciseRemotePlaneScale(read_path, write_path, temporary.Path());
	}

	// -------------------
	// Writer
	// -------------------
	int test_flush_after_size_queries() {
		TempDirectory temporary;
		const auto read_path = temporary.Path() / "unused-read.bin";
		const auto write_path = temporary.Path() / "flush-size-write.bin";
		Server server(read_path, write_path);
		ASSERT_TRUE(server.Connect(Net::Connection::Protocol::IPv4, address, port));
		Client client;
		ASSERT_TRUE(client.Connect(Net::Connection::Protocol::IPv4, address, port));
		auto [mount, rejected] = client.RequestMount(true);
		ASSERT_FALSE(rejected);
		ASSERT_EQUAL(Mount::Status::Authorized, mount.Result());
		auto writer = client.AttachWriter(mount);
		ASSERT_NOT_NULL(writer.get());
		ASSERT_TRUE(writer->Open());
		writer->WriteChunk(StormByte::ByteSize{0});
		ASSERT_EQUAL(Buf::IO::Status::Ok, writer->Truncate().status);
		const auto expected = MakePattern(1024 * 1024);
		for (std::size_t round = 0; round < 3; ++round) {
			ASSERT_EQUAL(Buf::IO::Status::Ok, writer->Seek(StormByte::ByteSize{0}, Buf::Position::Absolute).status);
			const auto written = writer->Write(expected.span());
			ASSERT_EQUAL(Buf::IO::Status::Ok, written.status);
			ASSERT_EQUAL(StormByte::ByteSize{expected.size()}, written.count);
			ASSERT_EQUAL(StormByte::ByteSize{expected.size()}, writer->Size());
			ASSERT_EQUAL(StormByte::ByteSize{expected.size()}, writer->Size());
			ASSERT_EQUAL(Buf::IO::Status::Ok, writer->Flush().status);
			ASSERT_EQUAL(StormByte::ByteSize{expected.size()}, writer->Size());
			const auto actual = ReadBytes(write_path);
			ASSERT_SIZE(actual, StormByte::ByteSize{expected.size()});
			ASSERT_EQUAL(expected, actual);
		}
		ASSERT_TRUE(writer->Close());
		writer.reset();
		ASSERT_EQUAL(expected, ReadBytes(write_path));
		RETURN_TEST(0);
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
}

int main() {
	using namespace RemoteFileTest;
	int result = 0;
	// -------------------
	// Access
	// -------------------
	result += RunOne("test_acl_and_independent_reader_cursors", test_acl_and_independent_reader_cursors);
	result += RunOne("test_capability_isolation", test_capability_isolation);
	result += RunOne("test_foreign_mount_rejection_preserves_owner", test_foreign_mount_rejection_preserves_owner);
	result += RunOne("test_rejected_descriptors_and_wrong_access_factories", test_rejected_descriptors_and_wrong_access_factories);

	// -------------------
	// Lifecycle
	// -------------------
	result += RunOne("test_closed_mount_rejected_and_fresh_token_usable", test_closed_mount_rejected_and_fresh_token_usable);
	result += RunOne("test_pipeline_and_control_disconnect", test_pipeline_and_control_disconnect);
	result += RunOne("test_plane_failure_faults_every_leaf", test_plane_failure_faults_every_leaf);

	// -------------------
	// Protocol
	// -------------------
	result += RunOne("test_mismatched_control_pipeline_denies_mount", test_mismatched_control_pipeline_denies_mount);

	// -------------------
	// Reader
	// -------------------
	result += RunOne("test_eight_interleaved_readers", test_eight_interleaved_readers);
	result += RunOne("test_empty_file_eof", test_empty_file_eof);
	result += RunOne("test_large_pattern_reader", test_large_pattern_reader);
	result += RunOne("test_sixteen_peers_with_eight_readers", test_sixteen_peers_with_eight_readers);

	// -------------------
	// Writer
	// -------------------
	result += RunOne("test_flush_after_size_queries", test_flush_after_size_queries);
	result += RunOne("test_pattern_writer_lifecycle", test_pattern_writer_lifecycle);
	result += RunOne("test_writer_reader_conflicts", test_writer_reader_conflicts);
	return result == 0 ? 0 : 1;
}
