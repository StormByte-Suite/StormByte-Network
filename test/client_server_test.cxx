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
#include <StormByte/network/remote_file_mount.hxx>
#include <StormByte/network/server.hxx>
#include <StormByte/network/telemetry.hxx>
#include <StormByte/safe/binary.hxx>
#include <StormByte/serializable.hxx>
#include <StormByte/system/this_thread.hxx>
#include <StormByte/test_handlers.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <format>
#include <iostream>
#include <latch>
#include <mutex>
#include <numeric>
#include <random>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>
// Namespace aliases and commonly used types to reduce verbosity
namespace SB = StormByte;
namespace Net = SB::Network;
namespace Buf = SB::Buffer;
namespace SBLog = SB::Logger;
namespace Transport = Net::Transport;
namespace Connection = Net::Connection;
using SB::Serializable;
using Net::DeserializePacketFunction;
using Net::PacketPointer;
template<typename T>
using NetExpected = SB::Expected<T, Net::Exception>;
using Buf::Consumer;
using Buf::Producer;
using Buf::ExecutionMode;
using SBLog::Log;
using SBLog::Level;
using SBLog::ThreadedLog;
using SBLog::humanreadable_bytes;
using SBLog::nohumanreadable;
using Buf::Pipeline;

static_assert(SB::Type::MaybeSafe<Net::Client>);
static_assert(SB::Type::MaybeSafe<Net::Server>);
static_assert(SB::Type::MaybeSafe<Net::Endpoint>);
static_assert(SB::Type::MaybeSafe<Transport::Packet>);
static_assert(SB::Type::MaybeSafe<Net::Telemetry>);
static_assert(SB::Type::MaybeSafe<Net::ClientTelemetry>);
static_assert(SB::Type::MaybeSafe<Net::ServerTelemetry>);
static_assert(SB::Type::MaybeSafe<Net::RemoteFileMount>);
static_assert(SB::Type::MaybeSafe<Net::BufferedRemoteFileReader>);
static_assert(SB::Type::MaybeSafe<Net::BufferedRemoteFileWriter>);
static_assert(SB::Type::MaybeSafe<DeserializePacketFunction>);

StormByte::Safe::Shared<Log> logger = StormByte::Safe::Shared<Log>::MakePointer<ThreadedLog>(std::cout, Level::Info, "[%L] [T%i] %T:");
constexpr const std::size_t large_data_size = 20 * 1024 * 1024; // 20 MB
constexpr const char large_data_repeat_char = 'x';
constexpr const char* HOST = "localhost";
constexpr const unsigned short PORT = 7080;
namespace Test {
	/**
	 * @brief Expose timing samples and clock values for telemetry tests.
	 */
	class Telemetry final: public Net::Telemetry {
		public:
			/**
			 * @brief Expose the base telemetry measurement helpers.
			 */
			using Net::Telemetry::Measure;

			/**
			 * @brief Expose the base telemetry sample type.
			 */
			using Net::Telemetry::Sample;

			/**
			 * @brief Render the test telemetry label.
			 * @return Test telemetry label.
			 */
			operator SB::Safe::String() const override {
				return SB::Safe::String{"test telemetry"};
			}

			/**
			 * @brief Read the accumulated values for a named clock.
			 * @param name Clock name.
			 * @return Current clock values.
			 */
			SB::Clock::Values Values(std::string_view name) const {
				return Clock(name).GetValues();
			}
	};

	namespace Packet {
		enum class Opcode: unsigned short {
			C_MSG_ASKNAMELIST = Net::Transport::Packet::PROCESS_THRESHOLD,
			S_MSG_RESPONDNAMELIST,
			C_MSG_ASKRANDOMNUMBER,
			S_MSG_RESPONDRANDOMNUMBER,
			C_MSG_SENDLARGEDATA,
			S_MSG_REPLYLARGEDATAECHOED,
			C_MSG_PING,
			S_MSG_PONG,
			C_MSG_ECHOTEXT,
			S_MSG_REPLYTEXT,
			C_MSG_SUMNUMBERS,
			S_MSG_REPLYSUM,
			C_MSG_DISCONNECT,
			C_MSG_SLOW,
			S_MSG_SLOW,
			C_MSG_STOPSERVER
		};

		class Generic: public Transport::Packet {
			public:
				Generic(const enum Opcode& opcode):
					Transport::Packet(static_cast<Transport::Packet::OpcodeType>(opcode)) {
				}
		};

		class AskNameList: public Generic {
			public:
				AskNameList(const std::size_t& amount):
					Generic(Opcode::C_MSG_ASKNAMELIST), m_amount(amount) {
				}

				StormByte::Safe::Binary DoSerialize() const noexcept override {
					return Serializable<std::size_t>(m_amount).Serialize();
				}

				std::size_t GetAmount() const noexcept {
					return m_amount;
				}

			private:
				std::size_t m_amount;
		};

		class AnswerNameList: public Generic {
			public:
				AnswerNameList(const std::vector<std::string>& names):
					Generic(Opcode::S_MSG_RESPONDNAMELIST), m_names(names) {
				}

				StormByte::Safe::Binary DoSerialize() const noexcept override {
					return Serializable<std::vector<std::string>>(m_names).Serialize();
				}

				const std::vector<std::string>& GetNames() const noexcept {
					return m_names;
				}

			private:
				std::vector<std::string> m_names;
		};

		class AskRandomNumber: public Generic {
			public:
				AskRandomNumber(): Generic(Opcode::C_MSG_ASKRANDOMNUMBER) {
				}

				StormByte::Safe::Binary DoSerialize() const noexcept override {
					return {};
				}
		};

		class AnswerRandomNumber: public Generic {
			public:
				AnswerRandomNumber(const int& number):
					Generic(Opcode::S_MSG_RESPONDRANDOMNUMBER), m_number(number) {
				}

				StormByte::Safe::Binary DoSerialize() const noexcept override {
					return Serializable<int>(m_number).Serialize();
				}

				int GetNumber() const noexcept {
					return m_number;
				}

			private:
				int m_number;
		};

		class LargeData: public Generic {
			public:
				explicit LargeData(std::string data) noexcept:
					Generic(Opcode::C_MSG_SENDLARGEDATA), m_data(std::move(data)) {
				}

				StormByte::Safe::Binary DoSerialize() const noexcept override {
					return Serializable<std::string>(m_data).Serialize();
				}

				const std::string& GetData() const noexcept {
					return m_data;
				}

				/**
				 * @brief Move the payload out for a server echo without an extra copy.
				 * @return Moved payload.
				 */
				std::string TakeData() noexcept {
					return std::move(m_data);
				}

			private:
				std::string m_data;
		};

		class AnswerLargeDataEchoed: public Generic {
			public:
				explicit AnswerLargeDataEchoed(std::string data) noexcept:
					Generic(Opcode::S_MSG_REPLYLARGEDATAECHOED), m_data(std::move(data)) {
				}

				StormByte::Safe::Binary DoSerialize() const noexcept override {
					return Serializable<std::string>(m_data).Serialize();
				}

				const std::string& GetData() const noexcept {
					return m_data;
				}

				/**
				 * @brief Move the echoed payload out of the reply.
				 * @return Moved payload.
				 */
				std::string TakeData() noexcept {
					return std::move(m_data);
				}

			private:
				std::string m_data;
		};

		class Ping: public Generic {
			public:
				Ping(): Generic(Opcode::C_MSG_PING) {
				}

				StormByte::Safe::Binary DoSerialize() const noexcept override {
					return {};
				}
		};

		class Pong: public Generic {
			public:
				Pong(): Generic(Opcode::S_MSG_PONG) {
				}

				StormByte::Safe::Binary DoSerialize() const noexcept override {
					return {};
				}
		};

		class DisconnectRequest: public Generic {
			public:
				DisconnectRequest(): Generic(Opcode::C_MSG_DISCONNECT) {
				}

				StormByte::Safe::Binary DoSerialize() const noexcept override {
					return {};
				}
		};

		class SlowRequest: public Generic {
			public:
				SlowRequest(): Generic(Opcode::C_MSG_SLOW) {
				}

				StormByte::Safe::Binary DoSerialize() const noexcept override {
					return {};
				}
		};

		class SlowReply: public Generic {
			public:
				SlowReply(): Generic(Opcode::S_MSG_SLOW) {
				}

				StormByte::Safe::Binary DoSerialize() const noexcept override {
					return {};
				}
		};

		class StopServerRequest: public Generic {
			public:
				StopServerRequest(): Generic(Opcode::C_MSG_STOPSERVER) {
				}

				StormByte::Safe::Binary DoSerialize() const noexcept override {
					return {};
				}
		};

		class EchoText: public Generic {
			public:
				explicit EchoText(std::string text) noexcept:
					Generic(Opcode::C_MSG_ECHOTEXT), m_text(std::move(text)) {
				}

				StormByte::Safe::Binary DoSerialize() const noexcept override {
					return Serializable<std::string>(m_text).Serialize();
				}

				const std::string& GetText() const noexcept {
					return m_text;
				}

			private:
				std::string m_text;
		};

		class ReplyText: public Generic {
			public:
				explicit ReplyText(std::string text) noexcept:
					Generic(Opcode::S_MSG_REPLYTEXT), m_text(std::move(text)) {
				}

				StormByte::Safe::Binary DoSerialize() const noexcept override {
					return Serializable<std::string>(m_text).Serialize();
				}

				const std::string& GetText() const noexcept {
					return m_text;
				}

			private:
				std::string m_text;
		};

		class SumNumbers: public Generic {
			public:
				explicit SumNumbers(std::vector<int> numbers) noexcept:
					Generic(Opcode::C_MSG_SUMNUMBERS), m_numbers(std::move(numbers)) {
				}

				StormByte::Safe::Binary DoSerialize() const noexcept override {
					return Serializable<std::vector<int>>(m_numbers).Serialize();
				}

				const std::vector<int>& GetNumbers() const noexcept {
					return m_numbers;
				}

			private:
				std::vector<int> m_numbers;
		};

		class ReplySum: public Generic {
			public:
				explicit ReplySum(const int& sum) noexcept:
					Generic(Opcode::S_MSG_REPLYSUM), m_sum(sum) {
				}

				StormByte::Safe::Binary DoSerialize() const noexcept override {
					return Serializable<int>(m_sum).Serialize();
				}

				int GetSum() const noexcept {
					return m_sum;
				}

			private:
				int m_sum;
		};
	}

}

STORMBYTE_DECLARE_MAYBE_SAFE(::Test::Telemetry);
STORMBYTE_DECLARE_MAYBE_SAFE(::Test::Packet::AskNameList);
STORMBYTE_DECLARE_MAYBE_SAFE(::Test::Packet::AnswerNameList);
STORMBYTE_DECLARE_MAYBE_SAFE(::Test::Packet::AskRandomNumber);
STORMBYTE_DECLARE_MAYBE_SAFE(::Test::Packet::AnswerRandomNumber);
STORMBYTE_DECLARE_MAYBE_SAFE(::Test::Packet::LargeData);
STORMBYTE_DECLARE_MAYBE_SAFE(::Test::Packet::AnswerLargeDataEchoed);
STORMBYTE_DECLARE_MAYBE_SAFE(::Test::Packet::Ping);
STORMBYTE_DECLARE_MAYBE_SAFE(::Test::Packet::Pong);
STORMBYTE_DECLARE_MAYBE_SAFE(::Test::Packet::DisconnectRequest);
STORMBYTE_DECLARE_MAYBE_SAFE(::Test::Packet::SlowRequest);
STORMBYTE_DECLARE_MAYBE_SAFE(::Test::Packet::SlowReply);
STORMBYTE_DECLARE_MAYBE_SAFE(::Test::Packet::StopServerRequest);
STORMBYTE_DECLARE_MAYBE_SAFE(::Test::Packet::EchoText);
STORMBYTE_DECLARE_MAYBE_SAFE(::Test::Packet::ReplyText);
STORMBYTE_DECLARE_MAYBE_SAFE(::Test::Packet::SumNumbers);
STORMBYTE_DECLARE_MAYBE_SAFE(::Test::Packet::ReplySum);

namespace Test {
	DeserializePacketFunction DeserializeFunction() {
		return [](Transport::Packet::OpcodeType opcode, Consumer consumer, StormByte::Safe::Shared<Log> logger) -> PacketPointer {
			(void)logger;
			StormByte::Safe::Binary data;
			consumer.ExtractUntilEoF(data);
			switch (static_cast<Packet::Opcode>(opcode)) {
				case Packet::Opcode::C_MSG_ASKNAMELIST: {
					auto expected_amount = Serializable<std::size_t>::Deserialize(data);
					if (!expected_amount) {
						return nullptr;
					}

					return StormByte::Network::PacketPointer::MakePointer<Packet::AskNameList>(*expected_amount);
				}

				case Packet::Opcode::S_MSG_RESPONDNAMELIST: {
					auto expected_names = Serializable<std::vector<std::string>>::Deserialize(data);
					if (!expected_names) {
						return nullptr;
					}

					return StormByte::Network::PacketPointer::MakePointer<Packet::AnswerNameList>(*expected_names);
				}

				case Packet::Opcode::C_MSG_ASKRANDOMNUMBER: {
					return StormByte::Network::PacketPointer::MakePointer<Packet::AskRandomNumber>();
				}

				case Packet::Opcode::C_MSG_DISCONNECT:
					return StormByte::Network::PacketPointer::MakePointer<Packet::DisconnectRequest>();
				case Packet::Opcode::C_MSG_SLOW:
					return StormByte::Network::PacketPointer::MakePointer<Packet::SlowRequest>();
				case Packet::Opcode::S_MSG_SLOW:
					return StormByte::Network::PacketPointer::MakePointer<Packet::SlowReply>();
				case Packet::Opcode::C_MSG_STOPSERVER:
					return StormByte::Network::PacketPointer::MakePointer<Packet::StopServerRequest>();
				case Packet::Opcode::S_MSG_RESPONDRANDOMNUMBER: {
					auto expected_number = Serializable<int>::Deserialize(data);
					if (!expected_number) {
						return nullptr;
					}

					return StormByte::Network::PacketPointer::MakePointer<Packet::AnswerRandomNumber>(*expected_number);
				}

				case Packet::Opcode::C_MSG_SENDLARGEDATA: {
					// Real payload (moved into packet) — no second synthetic 20 MiB string
					auto expected_data = Serializable<std::string>::Deserialize(data);
					if (!expected_data) {
						return nullptr;
					}

					return StormByte::Network::PacketPointer::MakePointer<Packet::LargeData>(std::move(*expected_data));
				}

				case Packet::Opcode::S_MSG_REPLYLARGEDATAECHOED: {
					auto expected_data = Serializable<std::string>::Deserialize(data);
					if (!expected_data) {
						return nullptr;
					}

					return StormByte::Network::PacketPointer::MakePointer<Packet::AnswerLargeDataEchoed>(std::move(*expected_data));
				}

				case Packet::Opcode::C_MSG_PING:
					return StormByte::Network::PacketPointer::MakePointer<Packet::Ping>();
				case Packet::Opcode::S_MSG_PONG:
					return StormByte::Network::PacketPointer::MakePointer<Packet::Pong>();
				case Packet::Opcode::C_MSG_ECHOTEXT: {
					auto expected_text = Serializable<std::string>::Deserialize(data);
					if (!expected_text) {
						return nullptr;
					}

					return StormByte::Network::PacketPointer::MakePointer<Packet::EchoText>(std::move(*expected_text));
				}

				case Packet::Opcode::S_MSG_REPLYTEXT: {
					auto expected_text = Serializable<std::string>::Deserialize(data);
					if (!expected_text) {
						return nullptr;
					}

					return StormByte::Network::PacketPointer::MakePointer<Packet::ReplyText>(std::move(*expected_text));
				}

				case Packet::Opcode::C_MSG_SUMNUMBERS: {
					auto expected_numbers = Serializable<std::vector<int>>::Deserialize(data);
					if (!expected_numbers) {
						return nullptr;
					}

					return StormByte::Network::PacketPointer::MakePointer<Packet::SumNumbers>(std::move(*expected_numbers));
				}

				case Packet::Opcode::S_MSG_REPLYSUM: {
					auto expected_sum = Serializable<int>::Deserialize(data);
					if (!expected_sum) {
						return nullptr;
					}

					return StormByte::Network::PacketPointer::MakePointer<Packet::ReplySum>(*expected_sum);
				}

				default:
					return nullptr;
			}
		};
	}

	using ExpectedNameList = NetExpected<std::vector<std::string>>;
	using ExpectedRandomNumber = NetExpected<int>;
	using ExpectedLargeData = NetExpected<std::string>;

	/**
	 * @brief XOR transform callable for framed test payloads.
	 */
	struct XorPipe final {
		public:
			/**
			 * @brief Select the reversible test transform.
			 * @param mask XOR mask; this fixture is not encryption.
			 */
			explicit XorPipe(std::byte mask = std::byte{0xAB}):
				m_mask(mask) {
			}

			/**
			 * @brief Transform input bursts, failing or closing the output before returning.
			 * @param in Borrowed input stream.
			 * @param out Borrowed output stream.
			 * @param log Borrowed logger for stage lifecycle and write failures.
			 */
			void operator()(const Buf::PipeInput& in, const Buf::PipeOutput& out,
				const StormByte::Safe::Shared<Log>& log) const {
				log << Level::Debug << "XOR Pipe: Starting..." << std::endl;
				constexpr StormByte::ByteSize max_chunk{10 * 1024 * 1024};

				while (!in.EoF()) {
					StormByte::Safe::Binary data;

					// Blocks until ≥1 byte or EoF/error (no yield spin)
					if (!in.Read(StormByte::ByteSize{1}, data) || data.empty()) {
						if (in.EoF()) {
							break;
						}

						continue;
					}

					// Non-blocking grab of the rest of the current burst (capped)
					const StormByte::ByteSize extra = std::min(in.Available(), max_chunk - data.size());
					if (extra > StormByte::ByteSize{0}) {
						StormByte::Safe::Binary more;
						if (in.Read(extra, more) && !more.empty()) {
							data.append(std::move(more));
						}
					}

					for (auto& byte: data) {
						byte ^= m_mask;
					}

					if (!out.Write(std::move(data))) {
						log << Level::Error << "XOR Pipe: Write failed" << std::endl;
						out.SetError();
						return;
					}
				}

				out.Close();
				log << Level::Debug << "XOR Pipe: Finished." << std::endl;
			}

		private:
			/**
			 * @brief Reversible test mask shared by input and output transformations.
			 */
			std::byte m_mask;
	};

	class Client: public Net::Client {
		public:
			Client(StormByte::Safe::Shared<Log> logger) noexcept:
				Net::Client(DeserializeFunction(), logger) {
			}

			~Client() noexcept = default;

			/**
			 * @brief Expose public packet requests to protocol tests.
			 */
			using Net::Client::Send;

			/**
			 * @brief Select the output mask before connecting the test peer.
			 * @param mask Reversible test mask, not encryption.
			 */
			void OutputMask(std::byte mask) noexcept {
				m_output_mask = mask;
			}

			/**
			 * @brief Install the fixture's XOR pair explicitly after transport connection.
			 * @param protocol Address family.
			 * @param address Remote address.
			 * @param port Listener port.
			 * @return Whether connection and one-time configuration succeeded.
			 */
			bool Connect(const Connection::Protocol& protocol, std::string_view address, const unsigned short& port) override {
				if (!Net::Client::Connect(protocol, address, port))
					return false;
				if (ConfigurePipelines(InputPipeline(), OutputPipeline()))
					return true;
				Disconnect();
				return false;
			}

			Pipeline InputPipeline() const noexcept {
				Pipeline pipeline;
				pipeline.Add(Buf::Pipe{XorPipe{}});
				return pipeline;
			}

			Pipeline OutputPipeline() const noexcept {
				Pipeline pipeline;
				pipeline.Add(Buf::Pipe{XorPipe{m_output_mask}});
				return pipeline;
			}

			ExpectedNameList RequestNameList(const std::size_t& amount) noexcept {
				Packet::AskNameList request_packet(amount);
				auto received_packet = Send(request_packet);
				if (!received_packet) {
					return SB::Unexpected<Net::Exception>("Client::RequestNameList: failed to send/receive AskNameList packet");
				}

				StormByte::Safe::Shared<Packet::AnswerNameList> namelist_packet = StormByte::Safe::DynamicPointerCast<Packet::AnswerNameList>(received_packet);
				if (!namelist_packet) {
					return SB::Unexpected<Net::Exception>("Client::RequestNameList: received unexpected packet opcode ({})", received_packet->Opcode());
				}

				return namelist_packet->GetNames();
			}

			ExpectedRandomNumber RequestRandomNumber() noexcept {
				Packet::AskRandomNumber request_packet;
				auto response_packet = Send(request_packet);
				if (!response_packet) {
					return SB::Unexpected<Net::Exception>("Client::RequestRandomNumber: failed to send AskRandomNumber packet");
				}

				StormByte::Safe::Shared<Packet::AnswerRandomNumber> answer_packet = StormByte::Safe::DynamicPointerCast<Packet::AnswerRandomNumber>(response_packet);
				if (!answer_packet) {
					return SB::Unexpected<Net::Exception>("Client::RequestRandomNumber: received unexpected packet opcode ({})", response_packet->Opcode());
				}

				return answer_packet->GetNumber();
			}

			ExpectedLargeData RequestLargeDataEcho(const std::size_t& size) noexcept {
				Packet::LargeData request_packet(std::string(size, large_data_repeat_char));
				auto response_packet = Send(request_packet);
				if (!response_packet) {
					return SB::Unexpected<Net::Exception>("Client::RequestLargeDataSize: failed to send LargeData packet");
				}

				StormByte::Safe::Shared<Packet::AnswerLargeDataEchoed> answer_packet = StormByte::Safe::DynamicPointerCast<Packet::AnswerLargeDataEchoed>(response_packet);
				if (!answer_packet) {
					return SB::Unexpected<Net::Exception>("Client::RequestLargeDataSize: received unexpected packet opcode ({})", response_packet->Opcode());
				}

				// Move data out of the packet so the owner can die without retaining 20 MiB
				return answer_packet->TakeData();
			}

			bool RequestPing() noexcept {
				Packet::Ping request_packet;
				auto response_packet = Send(request_packet);
				if (!response_packet) {
					return false;
				}

				return StormByte::Safe::DynamicPointerCast<Packet::Pong>(response_packet) != nullptr;
			}

			bool RequestDisconnect() noexcept {
				Packet::DisconnectRequest request_packet;
				return Send(request_packet) == nullptr;
			}

			bool RequestSlow() noexcept {
				Packet::SlowRequest request_packet;
				auto response_packet = Send(request_packet);
				return StormByte::Safe::DynamicPointerCast<Packet::SlowReply>(response_packet) != nullptr;
			}

			bool RequestStopServer() noexcept {
				Packet::StopServerRequest request_packet;
				return Send(request_packet) == nullptr;
			}

			NetExpected<std::string> RequestEchoText(std::string text) noexcept {
				Packet::EchoText request_packet(std::move(text));
				auto response_packet = Send(request_packet);
				if (!response_packet) {
					return SB::Unexpected<Net::Exception>("Client::RequestEchoText: failed to send/receive packet");
				}

				auto answer_packet = StormByte::Safe::DynamicPointerCast<Packet::ReplyText>(response_packet);
				if (!answer_packet) {
					return SB::Unexpected<Net::Exception>("Client::RequestEchoText: received unexpected packet opcode ({})", response_packet->Opcode());
				}

				return answer_packet->GetText();
			}

			NetExpected<int> RequestSum(std::vector<int> numbers) noexcept {
				Packet::SumNumbers request_packet(std::move(numbers));
				auto response_packet = Send(request_packet);
				if (!response_packet) {
					return SB::Unexpected<Net::Exception>("Client::RequestSum: failed to send/receive packet");
				}

				auto answer_packet = StormByte::Safe::DynamicPointerCast<Packet::ReplySum>(response_packet);
				if (!answer_packet) {
					return SB::Unexpected<Net::Exception>("Client::RequestSum: received unexpected packet opcode ({})", response_packet->Opcode());
				}

				return answer_packet->GetSum();
			}

		private:
			/**
			 * @brief Output mask fixed before connection configuration.
			 */
			std::byte m_output_mask{0xAB};
	};

	/**
	 * @brief Provider-owned response that deliberately cannot be decoded by the client factory.
	 */
	class InvalidReply final: public Transport::Packet {
		public:
			/**
			 * @brief Select unknown opcode or malformed payload for factory regressions.
			 * @param unknown Whether the opcode is absent from the factory.
			 * @param request Whether to select the request rather than reply opcode.
			 */
			explicit InvalidReply(bool unknown, bool request = false): Transport::Packet(unknown ? 0x7FFE
				: static_cast<OpcodeType>(request ? ::Test::Packet::Opcode::C_MSG_ECHOTEXT
					: ::Test::Packet::Opcode::S_MSG_REPLYTEXT)) {
			}

		private:
			/**
			 * @brief Return an incomplete serialized string.
			 * @return One byte that cannot contain a complete string header.
			 */
			StormByte::Safe::Binary DoSerialize() const noexcept override {
				auto bytes = Serializable<std::string>(std::string{"truncated payload"}).Serialize();
				bytes.pop_back();
				return bytes;
			}
	};

}

STORMBYTE_DECLARE_MAYBE_SAFE(::Test::Client);
STORMBYTE_DECLARE_MAYBE_SAFE(::Test::InvalidReply);

namespace Test {
	class Server: public Net::Server {
		public:
			Server(StormByte::Safe::Shared<Log> logger) noexcept:
				Net::Server(DeserializeFunction(), logger) {
			}

			~Server() noexcept override {
				Disconnect();
			}

			/**
			 * @brief Select deliberate reply failure before starting the server.
			 * @param mode Zero for normal, one for unknown opcode, two for corrupt bytes,
			 * three for a valid payload transformed with an incompatible test mask.
			 */
			void InvalidReplyMode(unsigned int mode) noexcept {
				m_invalid_reply = mode;
			}

			/**
			 * @brief Install the fixture pair independently for every accepted UUID.
			 * @param uuid New connection identity.
			 * @return Whether the fixture configuration was installed.
			 */
			bool OnClientConnected(std::string_view uuid) noexcept override {
				return ConfigureClientPipelines(uuid, InputPipeline(), OutputPipeline());
			}

			Pipeline InputPipeline() const noexcept {
				Pipeline pipeline;
				pipeline.Add(Buf::Pipe{XorPipe{}});
				return pipeline;
			}

			Pipeline OutputPipeline() const noexcept {
				Pipeline pipeline;
				pipeline.Add(Buf::Pipe{XorPipe{m_invalid_reply == 3 ? std::byte{0xCD} : std::byte{0xAB}}});
				return pipeline;
			}

		private:
			/**
			 * @brief Fault mode fixed before the event loop starts.
			 */
			unsigned int m_invalid_reply{0};

			PacketPointer ProcessClientPacket(std::string_view client_uuid, PacketPointer packet) noexcept override {
				(void)client_uuid;
				if (m_invalid_reply == 1 || m_invalid_reply == 2)
					return PacketPointer::MakePointer<InvalidReply>(m_invalid_reply == 1);
				switch (static_cast<Packet::Opcode>(packet->Opcode())) {
					case Packet::Opcode::C_MSG_ASKNAMELIST: {
						auto ask_packet = StormByte::Safe::DynamicPointerCast<Packet::AskNameList>(packet);
						if (!ask_packet) {
							return nullptr;
						}

						std::size_t amount = ask_packet->GetAmount();

						std::vector<std::string> names;
						for (std::size_t i = 0; i < amount; ++i) {
							names.push_back("Name_" + std::to_string(i + 1));
						}

						return StormByte::Network::PacketPointer::MakePointer<Packet::AnswerNameList>(names);
					}

					case Packet::Opcode::C_MSG_ASKRANDOMNUMBER: {
						static thread_local std::mt19937 gen{[]() {
							std::random_device rd;
							unsigned int seed = rd();
							if (seed == 0) {
								seed = static_cast<unsigned int>(std::chrono::high_resolution_clock::now().time_since_epoch().count());
							}

							return seed;
						}()};
						std::uniform_int_distribution<int> dist(0, 99);
						int random_number = dist(gen);
						return StormByte::Network::PacketPointer::MakePointer<Packet::AnswerRandomNumber>(random_number);
					}

					case Packet::Opcode::C_MSG_SENDLARGEDATA: {
						auto large_data_packet = StormByte::Safe::DynamicPointerCast<Packet::LargeData>(packet);
						if (!large_data_packet) {
							return nullptr;
						}

						// Move payload into the answer — no extra 20 MiB copy
						return StormByte::Network::PacketPointer::MakePointer<Packet::AnswerLargeDataEchoed>(large_data_packet->TakeData());
					}

					case Packet::Opcode::C_MSG_PING:
						return StormByte::Network::PacketPointer::MakePointer<Packet::Pong>();
					case Packet::Opcode::C_MSG_SLOW:
						std::this_thread::sleep_for(std::chrono::milliseconds(500));
						return StormByte::Network::PacketPointer::MakePointer<Packet::SlowReply>();
					case Packet::Opcode::C_MSG_STOPSERVER:
						Disconnect();
						return nullptr;
					case Packet::Opcode::C_MSG_DISCONNECT:
						DisconnectClient(client_uuid);
						return nullptr;
					case Packet::Opcode::C_MSG_ECHOTEXT: {
						auto text_packet = StormByte::Safe::DynamicPointerCast<Packet::EchoText>(packet);
						if (!text_packet) {
							return nullptr;
						}

						return StormByte::Network::PacketPointer::MakePointer<Packet::ReplyText>(text_packet->GetText());
					}

					case Packet::Opcode::C_MSG_SUMNUMBERS: {
						auto numbers_packet = StormByte::Safe::DynamicPointerCast<Packet::SumNumbers>(packet);
						if (!numbers_packet) {
							return nullptr;
						}

						const auto& numbers = numbers_packet->GetNumbers();
						const int sum = std::accumulate(numbers.begin(), numbers.end(), 0);
						return StormByte::Network::PacketPointer::MakePointer<Packet::ReplySum>(sum);
					}

					default:
						return nullptr;
				}

				return {};
			}
	};

	/**
	 * @brief Track derived client destruction and owned payload lifetime.
	 */
	class LifetimeClient final: public Client {
		public:
			/**
			 * @brief Construct a client with a destruction counter and owned payload.
			 * @param destructions Counter incremented when the client is destroyed.
			 */
			explicit LifetimeClient(int& destructions):
				Client(logger), m_destructions(destructions), m_payload(1024, 'c') {
			}

			/**
			 * @brief Record destruction of the derived client.
			 */
			~LifetimeClient() noexcept override {
				++m_destructions;
			}

			/**
			 * @brief Access the payload owned by the derived client.
			 * @return Borrowed payload.
			 */
			const std::string& Payload() const noexcept {
				return m_payload;
			}

		private:
			/**
			 * @brief Borrowed destruction counter.
			 */
			int& m_destructions;

			/**
			 * @brief Payload retained for lifetime checks.
			 */
			std::string m_payload;
	};

	/**
	 * @brief Track derived server destruction and owned payload lifetime.
	 */
	class LifetimeServer final: public Server {
		public:
			/**
			 * @brief Construct a server with a destruction counter and owned payload.
			 * @param destructions Counter incremented when the server is destroyed.
			 */
			explicit LifetimeServer(int& destructions):
				Server(logger), m_destructions(destructions), m_payload(1024, 's') {
			}

			/**
			 * @brief Record destruction of the derived server.
			 */
			~LifetimeServer() noexcept override {
				Disconnect();
				++m_destructions;
			}

			/**
			 * @brief Access the payload owned by the derived server.
			 * @return Borrowed payload.
			 */
			const std::string& Payload() const noexcept {
				return m_payload;
			}

		private:
			/**
			 * @brief Borrowed destruction counter.
			 */
			int& m_destructions;

			/**
			 * @brief Payload retained for lifetime checks.
			 */
			std::string m_payload;
	};
}

STORMBYTE_DECLARE_MAYBE_SAFE(::Test::Server);
STORMBYTE_DECLARE_MAYBE_SAFE(::Test::LifetimeClient);
STORMBYTE_DECLARE_MAYBE_SAFE(::Test::LifetimeServer);

/**
 * @namespace LoginTest
 * @brief Application authorization fixtures, not a production authentication system.
 */
namespace LoginTest {
	/**
	 * @brief Test-only login and protected-operation opcodes.
	 */
	enum class Opcode: Transport::Packet::OpcodeType {
		Login = 100,
		Accepted,
		ProtectedRequest,
		ProtectedReply
	};

	/**
	 * @brief Packet carrying optional fictitious username/password fields.
	 */
	class Packet final: public Transport::Packet {
		public:
			/**
			 * @brief Construct one test authorization packet.
			 * @param opcode Test operation.
			 * @param fields Username/password for Login, empty for other operations.
			 */
			explicit Packet(LoginTest::Opcode opcode, std::vector<std::string> fields = {}):
				Transport::Packet(static_cast<OpcodeType>(opcode)), m_fields(std::move(fields)) {
			}

			/**
			 * @brief Inspect the fictitious credentials without logging them.
			 * @return Borrowed fields for the handler.
			 */
			const std::vector<std::string>& Fields() const noexcept {
				return m_fields;
			}

		private:
			/**
			 * @brief Encode fields for the test protocol.
			 * @return Serialized vector.
			 */
			StormByte::Safe::Binary DoSerialize() const noexcept override {
				return Serializable<std::vector<std::string>>(m_fields).Serialize();
			}

			/**
			 * @brief Fictitious fields owned by the test provider.
			 */
			std::vector<std::string> m_fields;
	};

}

STORMBYTE_DECLARE_MAYBE_SAFE(::LoginTest::Packet);

namespace LoginTest {
	/**
	 * @brief Decode only well-formed packets of the authorization test protocol.
	 * @return Provider-owned packet factory.
	 */
	DeserializePacketFunction Factory() {
		return [](Transport::Packet::OpcodeType opcode, Consumer payload,
			StormByte::Safe::Shared<Log>) -> PacketPointer {
			if (opcode < static_cast<Transport::Packet::OpcodeType>(Opcode::Login)
				|| opcode > static_cast<Transport::Packet::OpcodeType>(Opcode::ProtectedReply))
				return {};
			StormByte::Safe::Binary bytes;
			payload.ExtractUntilEoF(bytes);
			auto fields = Serializable<std::vector<std::string>>::Deserialize(bytes);
			if (!fields || fields->size() != (opcode == static_cast<Transport::Packet::OpcodeType>(Opcode::Login) ? 2u : 0u))
				return {};
			return PacketPointer::MakePointer<Packet>(static_cast<Opcode>(opcode), std::move(*fields));
		};
	}

	/**
	 * @brief Client that completes an application login before reporting Connect success.
	 */
	class Client final: public Net::Client {
		public:
			/**
			 * @brief Store fictitious credentials for the automatic test login.
			 * @param username Test username.
			 * @param password Test password; never logged.
			 */
			Client(std::string username, std::string password):
				Net::Client(Factory(), logger), m_username(std::move(username)), m_password(std::move(password)) {
			}

			/**
			 * @brief Connect TCP and require an accepted login response.
			 * @param protocol Address family.
			 * @param address Remote address.
			 * @param port Remote port.
			 * @return False with a disconnected client when login fails.
			 */
			bool Connect(const Connection::Protocol& protocol, std::string_view address, const unsigned short& port) override {
				if (!Net::Client::Connect(protocol, address, port))
					return false;
				Packet request(Opcode::Login, {m_username, m_password});
				auto response = Send(request);
				if (response && response->Opcode() == static_cast<Transport::Packet::OpcodeType>(Opcode::Accepted))
					return true;
				Disconnect();
				return false;
			}

			/**
			 * @brief Bypass only the automatic login to simulate an unauthorized test peer.
			 * @return Whether the transport connected correctly.
			 */
			bool ConnectWithoutLogin() {
				return Net::Client::Connect(Connection::Protocol::IPv4, HOST, PORT);
			}

			/**
			 * @brief Request data that requires a logged-in session.
			 * @return Whether a protected reply was received.
			 */
			bool RequestProtected() {
				Packet request(Opcode::ProtectedRequest);
				auto response = Send(request);
				return response && response->Opcode() == static_cast<Transport::Packet::OpcodeType>(Opcode::ProtectedReply);
			}

			/**
			 * @brief Use no transformation; this fixture does not claim transport security.
			 * @return Empty input pipeline.
			 */
			Pipeline InputPipeline() const noexcept {
				return {};
			}

			/**
			 * @brief Use no transformation for the fictitious test protocol.
			 * @return Empty output pipeline.
			 */
			Pipeline OutputPipeline() const noexcept {
				return {};
			}

		private:
			/**
			 * @brief Fictitious username retained by the fixture.
			 */
			std::string m_username;

			/**
			 * @brief Fictitious password retained only for the test.
			 */
			std::string m_password;
	};

	/**
	 * @brief Authorize session UUIDs using a hardcoded fictitious credential table.
	 */
	class Server final: public Net::Server {
		public:
			/**
			 * @brief Construct the test packet handler.
			 */
			Server(): Net::Server(Factory(), logger) {
			}

			/**
			 * @brief Stop handlers before destroying their authorization state.
			 */
			~Server() noexcept override {
				Disconnect();
			}

			/**
			 * @brief Decode plaintext only in this non-security fixture.
			 * @return Empty input pipeline.
			 */
			Pipeline InputPipeline() const noexcept {
				return {};
			}

			/**
			 * @brief Encode plaintext only in this non-security fixture.
			 * @return Empty output pipeline.
			 */
			Pipeline OutputPipeline() const noexcept {
				return {};
			}

		private:
			/**
			 * @brief Apply authorization independently of successful transport connection.
			 * @param uuid Server-assigned session identity.
			 * @param incoming Well-formed test packet.
			 * @return Reply for authorized requests; no reply for rejected peers.
			 */
			PacketPointer ProcessClientPacket(std::string_view uuid, PacketPointer incoming) noexcept override {
				try {
					auto packet = StormByte::Safe::DynamicPointerCast<Packet>(incoming);
					if (!packet)
						return {};
					if (packet->Opcode() == static_cast<Transport::Packet::OpcodeType>(Opcode::Login)) {
						constexpr std::array<std::array<std::string_view, 2>, 2> credentials{{
							{"alice", "alice-test-only"}, {"bob", "bob-test-only"}}};
						const bool accepted = std::any_of(credentials.begin(), credentials.end(), [&](const auto& entry) {
							return packet->Fields()[0] == entry[0] && packet->Fields()[1] == entry[1];
						});
						{
							std::scoped_lock lock(m_mutex);
							if (accepted)
								m_authenticated.emplace(uuid);
							else
								m_authenticated.erase(std::string{uuid});
						}
						if (accepted)
							return PacketPointer::MakePointer<Packet>(Opcode::Accepted);
					}
					else if (packet->Opcode() == static_cast<Transport::Packet::OpcodeType>(Opcode::ProtectedRequest)) {
						std::scoped_lock lock(m_mutex);
						if (m_authenticated.contains(std::string{uuid}))
							return PacketPointer::MakePointer<Packet>(Opcode::ProtectedReply);
					}
				} catch (...) {
				}
				DisconnectClient(uuid);
				return {};
			}

			/**
			 * @brief Protect authorization state shared by packet workers.
			 */
			std::mutex m_mutex;

			/**
			 * @brief UUIDs accepted by this bounded test fixture, not a production session store.
			 */
			std::set<std::string> m_authenticated;
	};
}

STORMBYTE_DECLARE_MAYBE_SAFE(::LoginTest::Client);
STORMBYTE_DECLARE_MAYBE_SAFE(::LoginTest::Server);

// -------------------
// Authentication
// -------------------
int test_login_accepts_valid_credentials() {
	LoginTest::Server server;
	ASSERT_TRUE(server.Connect(Connection::Protocol::IPv4, HOST, PORT));
	LoginTest::Client alice("alice", "alice-test-only");
	LoginTest::Client bob("bob", "bob-test-only");
	ASSERT_TRUE(alice.Connect(Connection::Protocol::IPv4, HOST, PORT));
	ASSERT_TRUE(bob.Connect(Connection::Protocol::IPv4, HOST, PORT));
	ASSERT_TRUE(alice.RequestProtected() && bob.RequestProtected());
	alice.Disconnect();
	bob.Disconnect();
	server.Disconnect();
	RETURN_TEST(0);
}

int test_login_rejects_invalid_credentials() {
	LoginTest::Server server;
	ASSERT_TRUE(server.Connect(Connection::Protocol::IPv4, HOST, PORT));
	LoginTest::Client wrong_password("alice", "wrong-test-password");
	LoginTest::Client unknown_user("unknown-test-user", "alice-test-only");
	ASSERT_FALSE(wrong_password.Connect(Connection::Protocol::IPv4, HOST, PORT));
	ASSERT_FALSE(unknown_user.Connect(Connection::Protocol::IPv4, HOST, PORT));
	ASSERT_EQUAL(Connection::Status::Disconnected, wrong_password.Status());
	ASSERT_EQUAL(Connection::Status::Disconnected, unknown_user.Status());
	LoginTest::Client legitimate("alice", "alice-test-only");
	ASSERT_TRUE(legitimate.Connect(Connection::Protocol::IPv4, HOST, PORT));
	ASSERT_TRUE(legitimate.RequestProtected());
	legitimate.Disconnect();
	server.Disconnect();
	RETURN_TEST(0);
}

int test_login_required_after_reconnect() {
	LoginTest::Server server;
	ASSERT_TRUE(server.Connect(Connection::Protocol::IPv4, HOST, PORT));
	LoginTest::Client client("alice", "alice-test-only");
	ASSERT_TRUE(client.Connect(Connection::Protocol::IPv4, HOST, PORT));
	ASSERT_TRUE(client.RequestProtected());
	client.Disconnect();
	ASSERT_TRUE(client.ConnectWithoutLogin());
	ASSERT_FALSE(client.RequestProtected());
	client.Disconnect();
	ASSERT_TRUE(client.Connect(Connection::Protocol::IPv4, HOST, PORT));
	ASSERT_TRUE(client.RequestProtected());
	client.Disconnect();
	server.Disconnect();
	RETURN_TEST(0);
}

int test_login_required_before_protected_request() {
	LoginTest::Server server;
	ASSERT_TRUE(server.Connect(Connection::Protocol::IPv4, HOST, PORT));
	LoginTest::Client anonymous("alice", "alice-test-only");
	ASSERT_TRUE(anonymous.ConnectWithoutLogin());
	ASSERT_FALSE(anonymous.RequestProtected());
	anonymous.Disconnect();
	LoginTest::Client legitimate("bob", "bob-test-only");
	ASSERT_TRUE(legitimate.Connect(Connection::Protocol::IPv4, HOST, PORT));
	ASSERT_TRUE(legitimate.RequestProtected());
	legitimate.Disconnect();
	server.Disconnect();
	RETURN_TEST(0);
}

// -------------------
// Connection
// -------------------
int test_client_disconnect_keeps_server_alive() {
	constexpr std::string_view fn_name = "test_client_disconnect_keeps_server_alive";

	::Test::Server server(logger);
	if (!server.Connect(Net::Connection::Protocol::IPv4, HOST, PORT)) {
		logger << Level::Error << fn_name << ": server.Connect failed." << std::endl;
		RETURN_TEST(1);
	}

	std::this_thread::sleep_for(std::chrono::milliseconds(100));

	::Test::Client first_client(logger);
	if (!first_client.Connect(Net::Connection::Protocol::IPv4, HOST, PORT)) {
		logger << Level::Error << fn_name << ": first client.Connect failed." << std::endl;
		RETURN_TEST(1);
	}

	ASSERT_TRUE(first_client.RequestPing());
	first_client.Disconnect();

	std::this_thread::sleep_for(std::chrono::milliseconds(100));

	::Test::Client second_client(logger);
	if (!second_client.Connect(Net::Connection::Protocol::IPv4, HOST, PORT)) {
		logger << Level::Error << fn_name << ": second client.Connect failed." << std::endl;
		RETURN_TEST(1);
	}

	ASSERT_TRUE(second_client.RequestPing());
	second_client.Disconnect();

	server.Disconnect();
	RETURN_TEST(0);
}

int test_client_retry_after_failed_connect() {
	::Test::Server server(logger);
	if (!server.Connect(Net::Connection::Protocol::IPv4, HOST, PORT)) {
		RETURN_TEST(1);
	}

	::Test::Client client(logger);
	const auto invalid_protocol = static_cast<Net::Connection::Protocol>(-1);
	ASSERT_FALSE(client.Connect(invalid_protocol, HOST, PORT));
	ASSERT_EQUAL(Net::Connection::Status::Disconnected, client.Status());
	ASSERT_TRUE(client.Connect(Net::Connection::Protocol::IPv4, HOST, PORT));
	ASSERT_TRUE(client.RequestPing());

	ASSERT_FALSE(client.Connect(Net::Connection::Protocol::IPv4, HOST, PORT));
	ASSERT_EQUAL(Net::Connection::Status::Connected, client.Status());
	ASSERT_TRUE(client.RequestPing());

	client.Disconnect();
	ASSERT_EQUAL(Net::Connection::Status::Disconnected, client.Status());
	ASSERT_TRUE(client.Connect(Net::Connection::Protocol::IPv4, HOST, PORT));
	ASSERT_TRUE(client.RequestPing());
	client.Disconnect();
	server.Disconnect();
	RETURN_TEST(0);
}

int test_client_send_while_disconnected_and_repeated_disconnect() {
	::Test::Client client(logger);
	::Test::Packet::Ping request;
	ASSERT_EQUAL(Connection::Status::Disconnected, client.Status());
	ASSERT_NULL(client.Send(request));
	client.Disconnect();
	client.Disconnect();
	ASSERT_EQUAL(Connection::Status::Disconnected, client.Status());
	ASSERT_NULL(client.Send(request));

	::Test::Server server(logger);
	ASSERT_TRUE(server.Connect(Connection::Protocol::IPv4, HOST, PORT));
	ASSERT_TRUE(client.Connect(Connection::Protocol::IPv4, HOST, PORT));
	ASSERT_TRUE(client.RequestPing());
	client.Disconnect();
	client.Disconnect();
	ASSERT_EQUAL(Connection::Status::Disconnected, client.Status());
	ASSERT_NULL(client.Send(request));
	ASSERT_FALSE(client.Telemetry()->Connected());
	server.Disconnect();
	server.Disconnect();
	ASSERT_EQUAL(Connection::Status::Disconnected, server.Status());
	RETURN_TEST(0);
}

int test_disconnect_during_slow_handler() {
	::Test::Server server(logger);
	if (!server.Connect(Net::Connection::Protocol::IPv4, HOST, PORT)) {
		RETURN_TEST(1);
	}

	std::this_thread::sleep_for(std::chrono::milliseconds(100));
	::Test::Client abandoned_client(logger);
	ASSERT_TRUE(abandoned_client.Connect(Net::Connection::Protocol::IPv4, HOST, PORT));
	std::thread pending_thread([&]() {
		(void)abandoned_client.RequestSlow();
	});
	std::this_thread::sleep_for(std::chrono::milliseconds(50));
	abandoned_client.Disconnect();
	if (pending_thread.joinable()) {
		pending_thread.join();
	}

	::Test::Client surviving_client(logger);
	ASSERT_TRUE(surviving_client.Connect(Net::Connection::Protocol::IPv4, HOST, PORT));
	ASSERT_TRUE(surviving_client.RequestPing());
	surviving_client.Disconnect();
	server.Disconnect();
	RETURN_TEST(0);
}

int test_disconnect_requested_by_handler() {
	::Test::Server server(logger);
	if (!server.Connect(Net::Connection::Protocol::IPv4, HOST, PORT)) {
		RETURN_TEST(1);
	}

	std::this_thread::sleep_for(std::chrono::milliseconds(100));
	::Test::Client first_client(logger);
	if (!first_client.Connect(Net::Connection::Protocol::IPv4, HOST, PORT)) {
		RETURN_TEST(1);
	}

	ASSERT_TRUE(first_client.RequestDisconnect());
	first_client.Disconnect();
	std::this_thread::sleep_for(std::chrono::milliseconds(100));
	::Test::Client second_client(logger);
	if (!second_client.Connect(Net::Connection::Protocol::IPv4, HOST, PORT)) {
		RETURN_TEST(1);
	}

	ASSERT_TRUE(second_client.RequestPing());
	second_client.Disconnect();
	server.Disconnect();
	RETURN_TEST(0);
}

int test_many_concurrent_clients_keep_responses_isolated() {
	constexpr std::size_t client_count = 12;
	constexpr std::size_t requests_per_client = 4;
	::Test::Server server(logger);
	if (!server.Connect(Net::Connection::Protocol::IPv4, HOST, PORT)) {
		RETURN_TEST(1);
	}

	std::vector<int> client_results(client_count, 0);
	std::vector<std::thread> clients;
	clients.reserve(client_count);
	for (std::size_t client_index = 0; client_index < client_count; ++client_index) {
		clients.emplace_back([&, client_index] {
			::Test::Client client(logger);
			if (!client.Connect(Net::Connection::Protocol::IPv4, HOST, PORT)) {
				client_results[client_index] = 1;
				return;
			}

			for (std::size_t request_index = 0; request_index < requests_per_client; ++request_index) {
				const std::string request = std::format("client-{} request-{}", client_index, request_index);
				auto response = client.RequestEchoText(request);
				if (!response || response.value() != request) {
					client_results[client_index] = 1;
					break;
				}
			}

			client.Disconnect();
		});
	}

	for (auto& client_thread: clients) {
		if (client_thread.joinable()) {
			client_thread.join();
		}
	}

	for (const int result: client_results) {
		ASSERT_EQUAL(0, result);
	}

	server.Disconnect();
	RETURN_TEST(0);
}

int test_server_retry_after_failed_connect_and_restart() {
	::Test::Server server(logger);
	const auto invalid_protocol = static_cast<Net::Connection::Protocol>(-1);
	ASSERT_FALSE(server.Connect(invalid_protocol, HOST, PORT));
	ASSERT_EQUAL(Net::Connection::Status::Disconnected, server.Status());

	ASSERT_TRUE(server.Connect(Net::Connection::Protocol::IPv4, HOST, PORT));
	::Test::Client first_client(logger);
	ASSERT_TRUE(first_client.Connect(Net::Connection::Protocol::IPv4, HOST, PORT));
	ASSERT_TRUE(first_client.RequestPing());
	first_client.Disconnect();
	server.Disconnect();
	ASSERT_EQUAL(Net::Connection::Status::Disconnected, server.Status());

	ASSERT_TRUE(server.Connect(Net::Connection::Protocol::IPv4, HOST, PORT));
	::Test::Client second_client(logger);
	ASSERT_TRUE(second_client.Connect(Net::Connection::Protocol::IPv4, HOST, PORT));
	ASSERT_TRUE(second_client.RequestPing());
	second_client.Disconnect();
	server.Disconnect();
	RETURN_TEST(0);
}

int test_shutdown_with_pending_task() {
	::Test::Server server(logger);
	if (!server.Connect(Net::Connection::Protocol::IPv4, HOST, PORT)) {
		RETURN_TEST(1);
	}

	std::this_thread::sleep_for(std::chrono::milliseconds(100));
	::Test::Client client(logger);
	ASSERT_TRUE(client.Connect(Net::Connection::Protocol::IPv4, HOST, PORT));
	std::thread pending_thread([&]() {
		(void)client.RequestSlow();
	});
	std::this_thread::sleep_for(std::chrono::milliseconds(50));
	server.Disconnect();
	if (pending_thread.joinable()) {
		pending_thread.join();
	}

	client.Disconnect();
	ASSERT_EQUAL(Connection::Status::Disconnected, server.Status());
	RETURN_TEST(0);
}

int test_slow_handler_does_not_block_other_clients() {
	::Test::Server server(logger);
	if (!server.Connect(Net::Connection::Protocol::IPv4, HOST, PORT)) {
		RETURN_TEST(1);
	}

	std::this_thread::sleep_for(std::chrono::milliseconds(100));
	::Test::Client slow_client(logger);
	::Test::Client fast_client(logger);
	ASSERT_TRUE(slow_client.Connect(Net::Connection::Protocol::IPv4, HOST, PORT));
	ASSERT_TRUE(fast_client.Connect(Net::Connection::Protocol::IPv4, HOST, PORT));
	std::atomic<bool> slow_result{false};
	std::thread slow_thread([&]() {
		slow_result.store(slow_client.RequestSlow());
	});
	std::this_thread::sleep_for(std::chrono::milliseconds(50));
	const auto start = std::chrono::steady_clock::now();
	ASSERT_TRUE(fast_client.RequestPing());
	const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
		std::chrono::steady_clock::now() - start).count();
	ASSERT_TRUE(elapsed < 300);
	if (slow_thread.joinable()) {
		slow_thread.join();
	}

	ASSERT_TRUE(slow_result.load());
	slow_client.Disconnect();
	fast_client.Disconnect();
	server.Disconnect();
	RETURN_TEST(0);
}

int test_stop_requested_by_handler() {
	::Test::Server server(logger);
	if (!server.Connect(Net::Connection::Protocol::IPv4, HOST, PORT)) {
		RETURN_TEST(1);
	}

	std::this_thread::sleep_for(std::chrono::milliseconds(100));
	::Test::Client client(logger);
	ASSERT_TRUE(client.Connect(Net::Connection::Protocol::IPv4, HOST, PORT));
	ASSERT_TRUE(client.RequestStopServer());
	client.Disconnect();
	for (int attempt = 0; attempt < 40 && server.Status() != Connection::Status::Disconnected; ++attempt) {
		std::this_thread::sleep_for(std::chrono::milliseconds(25));
	}

	ASSERT_EQUAL(Connection::Status::Disconnected, server.Status());
	RETURN_TEST(0);
}

// -------------------
// Ownership
// -------------------
int test_deserializer_function_copy() {
	int invocation_count = 0;
	int destruction_count = 0;
	/**
	 * @brief Track deserializer callable invocations and copy destruction.
	 */
	struct Probe {
		/**
		 * @brief Borrowed invocation counter.
		 */
		int* invocations;

		/**
		 * @brief Borrowed destruction counter.
		 */
		int* destructions;

		/**
		 * @brief Record destruction of a callable instance.
		 */
		~Probe() noexcept {
			++*destructions;
		}

		/**
		 * @brief Record a deserializer invocation without constructing a packet.
		 * @return Null packet pointer.
		 */
		PacketPointer operator()(Transport::Packet::OpcodeType, Consumer,
			StormByte::Safe::Shared<Log>) const {
			++*invocations;
			return nullptr;
		}
	};

	{
		Probe probe{&invocation_count, &destruction_count};
		DeserializePacketFunction original{probe};
		DeserializePacketFunction copy = original;
		(void)copy(0, Consumer{}, {});
		ASSERT_EQUAL(1, invocation_count);
	}

	ASSERT_EQUAL(1, invocation_count);
	ASSERT_EQUAL(3, destruction_count);
	return 0;
}

int test_exact_derived_packet_copy() {
	const std::string payload{"owned\0packet", 12};
	auto original = PacketPointer::MakePointer<::Test::Packet::EchoText>(payload);
	auto original_leaf = SB::Safe::DynamicPointerCast<::Test::Packet::EchoText>(original);
	ASSERT_NOT_NULL(original_leaf);
	auto clone = PacketPointer::MakePointer<::Test::Packet::EchoText>(*original_leaf);
	auto clone_leaf = SB::Safe::DynamicPointerCast<::Test::Packet::EchoText>(clone);
	ASSERT_NOT_NULL(clone_leaf);
	ASSERT_NOT_EQUAL(original.get(), clone.get());
	ASSERT_NOT_EQUAL(original_leaf->GetText().data(), clone_leaf->GetText().data());
	ASSERT_EQUAL(original->Opcode(), clone->Opcode());
	ASSERT_EQUAL(payload, clone_leaf->GetText());
	original_leaf.reset();
	original.reset();
	auto retained = clone;
	clone.reset();
	clone_leaf.reset();
	auto retained_leaf = SB::Safe::DynamicPointerCast<::Test::Packet::EchoText>(retained);
	ASSERT_NOT_NULL(retained_leaf);
	ASSERT_SIZE(retained_leaf->GetText(), SB::Size{payload.size()});
	ASSERT_EQUAL(payload, retained_leaf->GetText());
	RETURN_TEST(0);
}

int test_safe_shared_endpoint_lifetime() {
	int client_destructions = 0;
	int server_destructions = 0;
	auto client = SB::Safe::Shared<Net::Client>::MakePointer<::Test::LifetimeClient>(client_destructions);
	auto server = SB::Safe::Shared<Net::Server>::MakePointer<::Test::LifetimeServer>(server_destructions);
	auto client_telemetry = client->Telemetry();
	auto server_telemetry = server->Telemetry();
	auto client_leaf = SB::Safe::DynamicPointerCast<::Test::LifetimeClient>(client);
	auto server_leaf = SB::Safe::DynamicPointerCast<::Test::LifetimeServer>(server);
	ASSERT_NOT_NULL(client_leaf);
	ASSERT_NOT_NULL(server_leaf);
	ASSERT_NOT_NULL(client_telemetry);
	ASSERT_NOT_NULL(server_telemetry);
	ASSERT_EQUAL(std::string(1024, 'c'), client_leaf->Payload());
	ASSERT_EQUAL(std::string(1024, 's'), server_leaf->Payload());
	auto client_copy = client;
	auto server_copy = server;
	client_leaf.reset();
	server_leaf.reset();
	client.reset();
	server.reset();
	ASSERT_EQUAL(0, client_destructions);
	ASSERT_EQUAL(0, server_destructions);
	client_copy.reset();
	server_copy.reset();
	ASSERT_EQUAL(1, client_destructions);
	ASSERT_EQUAL(1, server_destructions);
	ASSERT_EQUAL(SB::Size{0}, client_telemetry->ConnectionAttempts());
	ASSERT_FALSE(client_telemetry->Connected());
	ASSERT_EQUAL(SB::Size{0}, server_telemetry->AcceptedConnections());
	ASSERT_EQUAL(SB::Size{0}, server_telemetry->CurrentConnections());
	RETURN_TEST(0);
}

// -------------------
// Protocol
// -------------------
int test_factory_failure_disconnects_client() {
	for (unsigned int mode = 1; mode <= 3; ++mode) {
		::Test::Server server(logger);
		server.InvalidReplyMode(mode);
		ASSERT_TRUE(server.Connect(Connection::Protocol::IPv4, HOST, PORT));
		::Test::Client client(logger);
		ASSERT_TRUE(client.Connect(Connection::Protocol::IPv4, HOST, PORT));
		ASSERT_FALSE(client.RequestEchoText("factory_rejection_payload").has_value());
		ASSERT_EQUAL(Connection::Status::Disconnected, client.Status());
		ASSERT_EQUAL(SB::Size{1}, client.Telemetry()->RequestsWithoutResponse());
		server.Disconnect();
	}
	::Test::Server healthy(logger);
	ASSERT_TRUE(healthy.Connect(Connection::Protocol::IPv4, HOST, PORT));
	::Test::Client legitimate(logger);
	ASSERT_TRUE(legitimate.Connect(Connection::Protocol::IPv4, HOST, PORT));
	const auto echoed = legitimate.RequestEchoText("healthy_factory_payload");
	ASSERT_TRUE(echoed && *echoed == "healthy_factory_payload");
	legitimate.Disconnect();
	healthy.Disconnect();
	RETURN_TEST(0);
}

int test_factory_failure_disconnects_only_server_peer() {
	::Test::Server server(logger);
	ASSERT_TRUE(server.Connect(Connection::Protocol::IPv4, HOST, PORT));
	::Test::Client legitimate(logger);
	ASSERT_TRUE(legitimate.Connect(Connection::Protocol::IPv4, HOST, PORT));
	for (const bool unknown: {true, false}) {
		::Test::Client peer(logger);
		ASSERT_TRUE(peer.Connect(Connection::Protocol::IPv4, HOST, PORT));
		::Test::InvalidReply request(unknown, true);
		ASSERT_NULL(peer.Send(request));
		ASSERT_EQUAL(Connection::Status::Disconnected, peer.Status());
		ASSERT_EQUAL(SB::Size{1}, peer.Telemetry()->RequestsWithoutResponse());
		ASSERT_EQUAL(Connection::Status::Connected, server.Status());
	}
	ASSERT_EQUAL(SB::Size{0}, server.Telemetry()->PacketsDispatched());
	const auto echoed = legitimate.RequestEchoText("server_still_usable");
	ASSERT_TRUE(echoed && *echoed == "server_still_usable");
	legitimate.Disconnect();
	server.Disconnect();
	RETURN_TEST(0);
}

int test_mismatched_pipeline_disconnects_only_peer() {
	::Test::Server server(logger);
	ASSERT_TRUE(server.Connect(Connection::Protocol::IPv4, HOST, PORT));
	::Test::Client legitimate(logger);
	ASSERT_TRUE(legitimate.Connect(Connection::Protocol::IPv4, HOST, PORT));
	::Test::Client peer(logger);
	peer.OutputMask(std::byte{0xCD});
	ASSERT_TRUE(peer.Connect(Connection::Protocol::IPv4, HOST, PORT));
	ASSERT_FALSE(peer.RequestEchoText("untrusted client").has_value());
	ASSERT_EQUAL(Connection::Status::Disconnected, peer.Status());
	ASSERT_EQUAL(SB::Size{1}, peer.Telemetry()->RequestsWithoutResponse());
	ASSERT_EQUAL(SB::Size{0}, server.Telemetry()->PacketsDispatched());
	ASSERT_EQUAL(Connection::Status::Connected, server.Status());
	const auto echoed = legitimate.RequestEchoText("legitimate client");
	ASSERT_TRUE(echoed && *echoed == "legitimate client");
	legitimate.Disconnect();
	server.Disconnect();
	RETURN_TEST(0);
}

int test_network_exception_string_view() {
	const Net::Exception network_error{std::string_view{"request failed"}};
	const Net::ConnectionError connection_error{std::string_view{"socket closed"}};
	const Net::FrameError frame_error{std::string_view{"invalid frame"}};
	ASSERT_EQUAL(std::string_view{"StormByte.Network: request failed"}, std::string_view{network_error.what()});
	ASSERT_EQUAL(std::string_view{"StormByte.Network.Connection: socket closed"}, std::string_view{connection_error.what()});
	ASSERT_EQUAL(std::string_view{"StormByte.Network.Transport.Frame: invalid frame"}, std::string_view{frame_error.what()});
	return 0;
}

int test_ordered_batch_requests() {
	::Test::Server server(logger);
	ASSERT_TRUE(server.Connect(Connection::Protocol::IPv4, HOST, PORT));
	::Test::Client client(logger);
	ASSERT_TRUE(client.Connect(Connection::Protocol::IPv4, HOST, PORT));
	for (std::size_t index = 0; index < 8; ++index) {
		const std::string text = std::format("ordered request {}", index);
		::Test::Packet::EchoText request(text);
		auto reply = SB::Safe::DynamicPointerCast<::Test::Packet::ReplyText>(client.Send(request));
		ASSERT_NOT_NULL(reply);
		ASSERT_EQUAL(text, reply->GetText());
		::Test::Packet::Ping ping;
		ASSERT_NOT_NULL(SB::Safe::DynamicPointerCast<::Test::Packet::Pong>(client.Send(ping)));
	}
	ASSERT_EQUAL(SB::Size{16}, client.Telemetry()->Requests());
	ASSERT_EQUAL(SB::Size{16}, client.Telemetry()->Responses());
	ASSERT_EQUAL(SB::Size{0}, client.Telemetry()->RequestsWithoutResponse());
	client.Disconnect();
	server.Disconnect();
	RETURN_TEST(0);
}

int test_request_additional_commands() {
	constexpr std::string_view fn_name = "test_request_additional_commands";

	::Test::Server server(logger);
	if (!server.Connect(Net::Connection::Protocol::IPv4, HOST, PORT)) {
		logger << Level::Error << fn_name << ": server.Connect failed." << std::endl;
		RETURN_TEST(1);
	}

	std::this_thread::sleep_for(std::chrono::milliseconds(100));

	::Test::Client client(logger);
	if (!client.Connect(Net::Connection::Protocol::IPv4, HOST, PORT)) {
		logger << Level::Error << fn_name << ": client.Connect failed." << std::endl;
		RETURN_TEST(1);
	}

	ASSERT_TRUE(client.RequestPing());

	const std::string text = "StormByte network command with spaces and UTF-8: cafe";
	auto echoed_text = client.RequestEchoText(text);
	ASSERT_TRUE(echoed_text.has_value());
	ASSERT_EQUAL(text, echoed_text.value());

	const std::vector<int> numbers{ -100, 0, 1, 2, 42, 1000 };
	auto sum = client.RequestSum(numbers);
	ASSERT_TRUE(sum.has_value());
	ASSERT_EQUAL(945, sum.value());

	client.Disconnect();
	server.Disconnect();
	RETURN_TEST(0);
}

int test_request_empty_and_embedded_nul_payloads() {
	::Test::Server server(logger);
	ASSERT_TRUE(server.Connect(Connection::Protocol::IPv4, HOST, PORT));
	::Test::Client client(logger);
	ASSERT_TRUE(client.Connect(Connection::Protocol::IPv4, HOST, PORT));
	ASSERT_TRUE(client.RequestPing());
	auto text = client.RequestEchoText({});
	ASSERT_TRUE(text.has_value());
	ASSERT_EMPTY(*text);
	const std::string embedded_nul{"before\0after", 12};
	text = client.RequestEchoText(embedded_nul);
	ASSERT_TRUE(text.has_value());
	ASSERT_SIZE(*text, SB::Size{embedded_nul.size()});
	ASSERT_EQUAL(embedded_nul, *text);
	auto names = client.RequestNameList(0);
	ASSERT_TRUE(names.has_value());
	ASSERT_EMPTY(*names);
	auto sum = client.RequestSum({});
	ASSERT_TRUE(sum.has_value());
	ASSERT_EQUAL(0, *sum);
	auto large = client.RequestLargeDataEcho(0);
	ASSERT_TRUE(large.has_value());
	ASSERT_EMPTY(*large);
	client.Disconnect();
	server.Disconnect();
	RETURN_TEST(0);
}

int test_request_large_data_echoed() {
	constexpr std::string_view fn_name = "test_request_large_data_echoed";

	::Test::Server server(logger);
	if (!server.Connect(Net::Connection::Protocol::IPv4, HOST, PORT)) {
		logger << Level::Error << fn_name << ": server.Connect failed." << std::endl;
		RETURN_TEST(1);
	}

	std::this_thread::sleep_for(std::chrono::milliseconds(100));

	::Test::Client client(logger);
	if (!client.Connect(Net::Connection::Protocol::IPv4, HOST, PORT)) {
		logger << Level::Error << fn_name << ": client.Connect failed." << std::endl;
		RETURN_TEST(1);
	}

	auto data_expected = client.RequestLargeDataEcho(large_data_size);
	if (!data_expected) {
		logger << Level::Error << fn_name << ": RequestLargeDataEcho failed: " << data_expected.error()->what() << std::endl;
		RETURN_TEST(1);
	}

	// Single 20 MiB buffer: size + content check without a second reference string
	const std::string& data = data_expected.value();
	ASSERT_SIZE(data, SB::Size{large_data_size});
	ASSERT_EQUAL(std::string::npos, data.find_first_not_of(large_data_repeat_char));

	logger << Level::Info << fn_name << ": Received large data size: " << humanreadable_bytes << data.size()
		<< nohumanreadable << std::endl;
	client.Disconnect();
	server.Disconnect();
	RETURN_TEST(0);
}

int test_request_name_list() {
	constexpr std::string_view fn_name = "test_request_name_list";

	::Test::Server server(logger);
	if (!server.Connect(Net::Connection::Protocol::IPv4, HOST, PORT)) {
		logger << Level::Error << fn_name << ": server.Connect failed." << std::endl;
		RETURN_TEST(1);
	}

	std::this_thread::sleep_for(std::chrono::milliseconds(100));

	::Test::Client client(logger);
	if (!client.Connect(Net::Connection::Protocol::IPv4, HOST, PORT)) {
		logger << Level::Error << fn_name << ": client.Connect failed." << std::endl;
		RETURN_TEST(1);
	}

	const std::size_t amount = 3;
	auto names_expected = client.RequestNameList(amount);
	if (!names_expected) {
		logger << Level::Error << fn_name << ": RequestNameList failed: " << names_expected.error()->what() << std::endl;
		RETURN_TEST(1);
	}

	auto names = names_expected.value();
	std::string all_names;
	ASSERT_SIZE(names, SB::Size{amount});
	for (std::size_t i = 0; i < amount; ++i) {
		all_names += names[i] + " ";
		ASSERT_EQUAL("Name_" + std::to_string(i + 1), names[i]);
	}

	logger << Level::Info << fn_name << ": Received names: " << std::string_view{all_names} << std::endl;

	client.Disconnect();
	server.Disconnect();
	RETURN_TEST(0);
}

int test_request_random_number() {
	constexpr std::string_view fn_name = "test_request_random_number";

	::Test::Server server(logger);
	if (!server.Connect(Net::Connection::Protocol::IPv4, HOST, PORT)) {
		logger << Level::Error << fn_name << ": server.Connect failed." << std::endl;
		RETURN_TEST(1);
	}

	std::this_thread::sleep_for(std::chrono::milliseconds(100));

	::Test::Client client(logger);
	if (!client.Connect(Net::Connection::Protocol::IPv4, HOST, PORT)) {
		logger << Level::Error << fn_name << ": client.Connect failed." << std::endl;
		RETURN_TEST(1);
	}

	auto number_expected = client.RequestRandomNumber();
	if (!number_expected) {
		logger << Level::Error << fn_name << ": RequestRandomNumber failed: " << number_expected.error()->what() << std::endl;
		RETURN_TEST(1);
	}

	int n = number_expected.value();
	ASSERT_TRUE(n >= 0 && n < 100);
	logger << Level::Info << fn_name << ": Received random number: " << n << std::endl;
	client.Disconnect();
	server.Disconnect();
	RETURN_TEST(0);
}

// -------------------
// Remote File
// -------------------
int test_remote_file_mount_codec() {
	using Mount = Net::RemoteFileMount;

	for (const Mount& mount: { Mount::NotAuthorized(), Mount::Unavailable() }) {
		const SB::Safe::Binary encoded = Serializable<Mount>(mount).Serialize();
		const auto decoded = Serializable<Mount>::Deserialize(encoded.span());
		ASSERT_TRUE(decoded.has_value());
		ASSERT_EQUAL(mount.Result(), decoded->Result());
		ASSERT_EQUAL(0u, decoded->Port());
		ASSERT_EQUAL(Mount::Access::None, decoded->Mode());
		ASSERT_EQUAL(0u, decoded->MaximumTimeoutSeconds());
		ASSERT_EQUAL(mount.Token(), decoded->Token());
		ASSERT_NOT_EMPTY(encoded);
		SB::Safe::Binary truncated = encoded;
		truncated.resize(truncated.size() - SB::ByteSize{1});
		ASSERT_FALSE(Serializable<Mount>::Deserialize(truncated.span()).has_value());
		SB::Safe::Binary extended = encoded;
		extended.push_back(std::byte{0});
		ASSERT_FALSE(Serializable<Mount>::Deserialize(extended.span()).has_value());
	}
	return 0;
}

// -------------------
// Telemetry
// -------------------
int test_telemetry_concurrent_samples() {
	constexpr std::size_t sample_count = 4;
	::Test::Telemetry telemetry;
	std::latch ready{sample_count};
	std::latch release{1};
	std::array<std::chrono::microseconds, sample_count> elapsed{};
	std::array<std::chrono::microseconds, sample_count> repeated{};
	std::array<std::thread, sample_count> workers;
	for (std::size_t index = 0; index < sample_count; ++index) {
		workers[index] = std::thread([&, index]() {
			auto sample = telemetry.Measure("concurrent");
			ready.count_down();
			release.wait();
			elapsed[index] = sample.Stop();
			repeated[index] = sample.Stop();
		});
	}
	ready.wait();
	const auto active_values = telemetry.Values("concurrent");
	std::this_thread::sleep_for(std::chrono::milliseconds(5));
	release.count_down();
	for (auto& worker: workers) {
		worker.join();
	}
	ASSERT_EQUAL(SB::Size{0}, active_values.Count);
	const auto values = telemetry.Values("concurrent");
	ASSERT_EQUAL(SB::Size{sample_count}, values.Count);
	std::chrono::microseconds total{};
	for (std::size_t index = 0; index < sample_count; ++index) {
		ASSERT_TRUE(elapsed[index].count() > 0);
		ASSERT_EQUAL(elapsed[index], repeated[index]);
		total += elapsed[index];
	}
	ASSERT_EQUAL(total, values.Time);
	ASSERT_EQUAL(total / sample_count, values.MeanDuration);
	RETURN_TEST(0);
}

int test_telemetry_cross_thread_sample_destruction() {
	::Test::Telemetry telemetry;
	{
		auto sample = telemetry.Measure("transferred-raii");
		std::thread worker([transferred = std::move(sample)]() mutable {
			auto scoped = std::move(transferred);
			(void)scoped;
			std::this_thread::sleep_for(std::chrono::milliseconds(5));
		});
		worker.join();
		ASSERT_EQUAL(std::chrono::microseconds::zero(), sample.Stop());
		ASSERT_EQUAL(SB::Size{1}, telemetry.Values("transferred-raii").Count);
	}
	const auto values = telemetry.Values("transferred-raii");
	ASSERT_EQUAL(SB::Size{1}, values.Count);
	ASSERT_TRUE(values.Time.count() > 0);
	ASSERT_EQUAL(values.Time, values.MeanDuration);
	RETURN_TEST(0);
}

int test_telemetry_cross_thread_sample_transfer() {
	::Test::Telemetry telemetry;
	std::chrono::microseconds elapsed{};
	std::chrono::microseconds repeated{};
	{
		auto sample = telemetry.Measure("transferred");
		std::this_thread::sleep_for(std::chrono::milliseconds(5));
		std::thread worker([transferred = std::move(sample), &elapsed, &repeated]() mutable {
			elapsed = transferred.Stop();
			repeated = transferred.Stop();
		});
		worker.join();
		ASSERT_EQUAL(std::chrono::microseconds::zero(), sample.Stop());
		ASSERT_EQUAL(SB::Size{1}, telemetry.Values("transferred").Count);
	}
	const auto values = telemetry.Values("transferred");
	ASSERT_EQUAL(SB::Size{1}, values.Count);
	ASSERT_TRUE(elapsed.count() > 0);
	ASSERT_EQUAL(elapsed, repeated);
	ASSERT_EQUAL(elapsed, values.Time);
	ASSERT_EQUAL(elapsed, values.MeanDuration);
	RETURN_TEST(0);
}

int test_telemetry_nested_samples() {
	::Test::Telemetry telemetry;
	std::chrono::microseconds inner_elapsed{};
	{
		auto outer = telemetry.Measure("nested");
		std::this_thread::sleep_for(std::chrono::milliseconds(5));
		{
			auto inner = telemetry.Measure("nested");
			std::this_thread::sleep_for(std::chrono::milliseconds(5));
			inner_elapsed = inner.Stop();
			const auto values = telemetry.Values("nested");
			ASSERT_EQUAL(SB::Size{1}, values.Count);
			ASSERT_EQUAL(inner_elapsed, values.Time);
		}
		ASSERT_EQUAL(SB::Size{1}, telemetry.Values("nested").Count);
	}
	const auto values = telemetry.Values("nested");
	ASSERT_EQUAL(SB::Size{2}, values.Count);
	ASSERT_TRUE(inner_elapsed.count() > 0);
	ASSERT_TRUE(values.Time - inner_elapsed >= inner_elapsed + std::chrono::milliseconds(1));
	ASSERT_EQUAL(values.Time / 2, values.MeanDuration);
	RETURN_TEST(0);
}

int test_telemetry_repeated_stop() {
	::Test::Telemetry telemetry;
	std::chrono::microseconds elapsed{};
	{
		auto sample = telemetry.Measure("repeated");
		std::this_thread::sleep_for(std::chrono::milliseconds(5));
		elapsed = sample.Stop();
		std::this_thread::sleep_for(std::chrono::milliseconds(5));
		ASSERT_EQUAL(elapsed, sample.Stop());
		ASSERT_EQUAL(elapsed, sample.Stop());
		ASSERT_EQUAL(SB::Size{1}, telemetry.Values("repeated").Count);
	}
	const auto values = telemetry.Values("repeated");
	ASSERT_EQUAL(SB::Size{1}, values.Count);
	ASSERT_TRUE(elapsed.count() > 0);
	ASSERT_EQUAL(elapsed, values.Time);
	ASSERT_EQUAL(elapsed, values.MeanDuration);
	RETURN_TEST(0);
}

int test_telemetry_snapshots_and_lifetime() {
	StormByte::Safe::Shared<Net::ServerTelemetry> server_telemetry;
	StormByte::Safe::Shared<Net::ClientTelemetry> first_telemetry;
	StormByte::Safe::Shared<Net::ClientTelemetry> second_telemetry;

	{
		auto server = SB::Safe::Shared<Net::Server>::MakePointer<::Test::Server>(logger);
		ASSERT_TRUE(server->Connect(Net::Connection::Protocol::IPv4, HOST, PORT));
		server_telemetry = server->Telemetry();
		ASSERT_NOT_NULL(server_telemetry);

		{
			::Test::Client first_client(logger);
			::Test::Client second_client(logger);
			ASSERT_TRUE(first_client.Connect(Net::Connection::Protocol::IPv4, HOST, PORT));
			ASSERT_TRUE(second_client.Connect(Net::Connection::Protocol::IPv4, HOST, PORT));
			first_telemetry = first_client.Telemetry();
			second_telemetry = second_client.Telemetry();
			ASSERT_NOT_NULL(first_telemetry);
			ASSERT_NOT_NULL(second_telemetry);
			ASSERT_NOT_EQUAL(first_telemetry.get(), second_telemetry.get());

			ASSERT_TRUE(first_client.RequestPing());
			ASSERT_TRUE(second_client.RequestPing());

			ASSERT_EQUAL(SB::Size{1}, first_telemetry->ConnectionAttempts());
			ASSERT_EQUAL(SB::Size{1}, first_telemetry->ConnectionsEstablished());
			ASSERT_EQUAL(SB::Size{0}, first_telemetry->ConnectionFailures());
			ASSERT_TRUE(first_telemetry->Connected());
			ASSERT_EQUAL(SB::Size{1}, first_telemetry->Requests());
			ASSERT_EQUAL(SB::Size{1}, first_telemetry->Responses());
			ASSERT_EQUAL(SB::Size{0}, first_telemetry->RequestsWithoutResponse());
			ASSERT_EQUAL(SB::Size{1}, first_telemetry->RequestLatencySamples());
			ASSERT_TRUE(first_telemetry->MeanRequestLatency().count() >= 0);

			ASSERT_EQUAL(SB::Size{1}, second_telemetry->ConnectionAttempts());
			ASSERT_EQUAL(SB::Size{1}, second_telemetry->ConnectionsEstablished());
			ASSERT_EQUAL(SB::Size{1}, second_telemetry->Requests());
			ASSERT_EQUAL(SB::Size{1}, second_telemetry->Responses());
			ASSERT_EQUAL(SB::Size{0}, second_telemetry->RequestsWithoutResponse());
			ASSERT_EQUAL(SB::Size{1}, second_telemetry->RequestLatencySamples());

			ASSERT_EQUAL(SB::Size{2}, server_telemetry->CurrentConnections());
			ASSERT_EQUAL(SB::Size{2}, server_telemetry->AcceptedConnections());
			ASSERT_EQUAL(SB::Size{0}, server_telemetry->ClosedConnections());
			ASSERT_EQUAL(SB::Size{2}, server_telemetry->PeakConnections());
			ASSERT_EQUAL(SB::Size{2}, server_telemetry->PacketsDispatched());
			ASSERT_EQUAL(SB::Size{2}, server_telemetry->HandlersCompleted());
			ASSERT_EQUAL(SB::Size{0}, server_telemetry->HandlersWithoutResponse());
			ASSERT_EQUAL(SB::Size{0}, server_telemetry->HandlerErrors());
			ASSERT_EQUAL(SB::Size{2}, server_telemetry->HandlerLatencySamples());
			ASSERT_TRUE(server_telemetry->MeanHandlerLatency().count() >= 0);

			first_client.Disconnect();
			second_client.Disconnect();
			ASSERT_FALSE(first_telemetry->Connected());
		}

		server->Disconnect();
		ASSERT_EQUAL(SB::Size{0}, server_telemetry->CurrentConnections());
		ASSERT_EQUAL(SB::Size{2}, server_telemetry->ClosedConnections());
	}

	ASSERT_EQUAL(SB::Size{2}, server_telemetry->AcceptedConnections());
	ASSERT_EQUAL(SB::Size{2}, server_telemetry->PacketsDispatched());
	ASSERT_EQUAL(SB::Size{1}, first_telemetry->Responses());
	ASSERT_EQUAL(SB::Size{1}, second_telemetry->Responses());
	const auto snapshot = static_cast<SB::Safe::String>(*first_telemetry);
	const std::string_view snapshot_text = snapshot;
	ASSERT_NOT_EMPTY(snapshot_text);
	ASSERT_EQUAL(std::string_view::npos, snapshot_text.find('\0'));
	ASSERT_SIZE(snapshot, SB::Size{std::char_traits<char>::length(snapshot.data())});
	return 0;
}

int main() {
	int result = 0;

	// -------------------
	// Authentication
	// -------------------
	result += test_login_accepts_valid_credentials();
	result += test_login_rejects_invalid_credentials();
	result += test_login_required_after_reconnect();
	result += test_login_required_before_protected_request();

	// -------------------
	// Connection
	// -------------------
	result += test_client_disconnect_keeps_server_alive();
	result += test_client_retry_after_failed_connect();
	result += test_client_send_while_disconnected_and_repeated_disconnect();
	result += test_disconnect_during_slow_handler();
	result += test_disconnect_requested_by_handler();
	result += test_many_concurrent_clients_keep_responses_isolated();
	result += test_server_retry_after_failed_connect_and_restart();
	result += test_shutdown_with_pending_task();
	result += test_slow_handler_does_not_block_other_clients();
	result += test_stop_requested_by_handler();

	// -------------------
	// Ownership
	// -------------------
	result += test_deserializer_function_copy();
	result += test_exact_derived_packet_copy();
	result += test_safe_shared_endpoint_lifetime();

	// -------------------
	// Protocol
	// -------------------
	result += test_factory_failure_disconnects_client();
	result += test_factory_failure_disconnects_only_server_peer();
	result += test_mismatched_pipeline_disconnects_only_peer();
	result += test_network_exception_string_view();
	result += test_ordered_batch_requests();
	result += test_request_additional_commands();
	result += test_request_empty_and_embedded_nul_payloads();
	result += test_request_large_data_echoed();
	result += test_request_name_list();
	result += test_request_random_number();

	// -------------------
	// Remote File
	// -------------------
	result += test_remote_file_mount_codec();

	// -------------------
	// Telemetry
	// -------------------
	result += test_telemetry_concurrent_samples();
	result += test_telemetry_cross_thread_sample_destruction();
	result += test_telemetry_cross_thread_sample_transfer();
	result += test_telemetry_nested_samples();
	result += test_telemetry_repeated_stop();
	result += test_telemetry_snapshots_and_lifetime();

	if (result == 0) {
		std::cout << "All tests passed!" << std::endl;
	}
	else {
		std::cout << result << " tests failed." << std::endl;
	}

	return result;
}
