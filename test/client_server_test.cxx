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

#include <StormByte/network/client.hxx>
#include <StormByte/network/remote_file_mount.hxx>
#include <StormByte/network/server.hxx>
#include <StormByte/serializable.hxx>
#include <StormByte/logger/threaded_log.hxx>
#include <StormByte/test_handlers.h>
#include <StormByte/system/this_thread.hxx>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <numeric>
#include <thread>
#include <random>
#include <utility>
#ifdef UNIX
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>
#else
#include <winsock2.h>
#include <ws2tcpip.h>
#endif
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
StormByte::Shared<Log> logger = StormByte::Heap::MakeShared<ThreadedLog>(std::cout, Level::Info, "[%L] [T%i] %T:");
constexpr const unsigned short timeout = 5; // 5 seconds
constexpr const std::size_t large_data_size = 20 * 1024 * 1024; // 20 MB
constexpr const char large_data_repeat_char = 'x';
constexpr const char* HOST = "localhost";
constexpr const unsigned short PORT = 7080;
#ifdef WINDOWS
using RawSocket = SOCKET;
constexpr RawSocket invalid_raw_socket = INVALID_SOCKET;
#else
using RawSocket = int;
constexpr RawSocket invalid_raw_socket = -1;
#endif

RawSocket ConnectRawSocket() {
	RawSocket socket_handle = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (socket_handle == invalid_raw_socket) {
		return invalid_raw_socket;
	}

	sockaddr_in address{};
	address.sin_family = AF_INET;
	address.sin_port = htons(PORT);
	if (inet_pton(AF_INET, "127.0.0.1", &address.sin_addr) != 1) {
#ifdef WINDOWS
		closesocket(socket_handle);
#else
		close(socket_handle);
#endif
		return invalid_raw_socket;
	}

	if (::connect(socket_handle, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) < 0) {
#ifdef WINDOWS
		closesocket(socket_handle);
#else
		close(socket_handle);
#endif
		return invalid_raw_socket;
	}

	return socket_handle;
}

void CloseRawSocket(RawSocket socket_handle) noexcept {
#ifdef WINDOWS
	closesocket(socket_handle);
#else
	close(socket_handle);
#endif
}

bool SendRawBytes(RawSocket socket_handle, std::span<const std::byte> data) {
	while (!data.empty()) {
#ifdef WINDOWS
		const int sent = ::send(socket_handle, reinterpret_cast<const char*>(data.data()), static_cast<int>(data.size()), 0);
#else
		const ssize_t sent = ::send(socket_handle, data.data(), data.size(), 0);
#endif
		if (sent <= 0) {
			return false;
		}

		data = data.subspan(static_cast<std::size_t>(sent));
	}

	return true;
}

bool ReceiveRawBytes(RawSocket socket_handle, std::span<std::byte> data) {
	while (!data.empty()) {
#ifdef WINDOWS
		const int received = ::recv(socket_handle, reinterpret_cast<char*>(data.data()), static_cast<int>(data.size()), 0);
#else
		const ssize_t received = ::recv(socket_handle, data.data(), data.size(), 0);
#endif
		if (received <= 0) {
			return false;
		}

		data = data.subspan(static_cast<std::size_t>(received));
	}

	return true;
}

bool WaitForRawDisconnect(RawSocket socket_handle, const std::chrono::seconds timeout_duration) {
	fd_set read_fds;
	FD_ZERO(&read_fds);
	FD_SET(socket_handle, &read_fds);
	timeval timeout_value{};
	timeout_value.tv_sec = static_cast<decltype(timeout_value.tv_sec)>(timeout_duration.count());
#ifdef WINDOWS
	const int ready = select(0, &read_fds, nullptr, nullptr, &timeout_value);
#else
	const int ready = select(socket_handle + 1, &read_fds, nullptr, nullptr, &timeout_value);
#endif
	if (ready <= 0)
		return false;

	char byte = 0;
	return ::recv(socket_handle, &byte, 1, 0) <= 0;
}

namespace Test {
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
				Generic(const enum Opcode& opcode): Transport::Packet(static_cast<Transport::Packet::OpcodeType>(opcode)) {}
		};
		class AskNameList: public Generic {
			public:
				AskNameList(const std::size_t& amount): Generic(Opcode::C_MSG_ASKNAMELIST), m_amount(amount) {}

				StormByte::BinaryData DoSerialize() const noexcept override {
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
				AnswerNameList(const std::vector<std::string>& names): Generic(Opcode::S_MSG_RESPONDNAMELIST), m_names(names) {}

				StormByte::BinaryData DoSerialize() const noexcept override {
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
				AskRandomNumber(): Generic(Opcode::C_MSG_ASKRANDOMNUMBER) {}

				StormByte::BinaryData DoSerialize() const noexcept override {
					return {};
				}
		};
		class AnswerRandomNumber: public Generic {
			public:
				AnswerRandomNumber(const int& number): Generic(Opcode::S_MSG_RESPONDRANDOMNUMBER), m_number(number) {}

				StormByte::BinaryData DoSerialize() const noexcept override {
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
				explicit LargeData(std::string data) noexcept
					: Generic(Opcode::C_MSG_SENDLARGEDATA),
					m_data(std::move(data)) {}

				StormByte::BinaryData DoSerialize() const noexcept override {
					return Serializable<std::string>(m_data).Serialize();
				}

				const std::string& GetData() const noexcept {
					return m_data;
				}

				/** Move payload out (server echo without extra copy). */
				std::string TakeData() noexcept {
					return std::move(m_data);
				}

			private:
				std::string m_data;
		};

		class AnswerLargeDataEchoed: public Generic {
			public:
				explicit AnswerLargeDataEchoed(std::string data) noexcept
					: Generic(Opcode::S_MSG_REPLYLARGEDATAECHOED),
					m_data(std::move(data)) {}

				StormByte::BinaryData DoSerialize() const noexcept override {
					return Serializable<std::string>(m_data).Serialize();
				}

				const std::string& GetData() const noexcept {
					return m_data;
				}

				std::string TakeData() noexcept {
					return std::move(m_data);
				}

			private:
				std::string m_data;
		};
		class Ping: public Generic {
			public:
				Ping(): Generic(Opcode::C_MSG_PING) {}

				StormByte::BinaryData DoSerialize() const noexcept override {
					return {};
				}
		};
		class Pong: public Generic {
			public:
				Pong(): Generic(Opcode::S_MSG_PONG) {}

				StormByte::BinaryData DoSerialize() const noexcept override {
					return {};
				}
		};
		class DisconnectRequest: public Generic {
			public:
				DisconnectRequest(): Generic(Opcode::C_MSG_DISCONNECT) {}

				StormByte::BinaryData DoSerialize() const noexcept override { return {}; }
		};
		class SlowRequest: public Generic {
			public:
				SlowRequest(): Generic(Opcode::C_MSG_SLOW) {}

				StormByte::BinaryData DoSerialize() const noexcept override { return {}; }
		};
		class SlowReply: public Generic {
			public:
				SlowReply(): Generic(Opcode::S_MSG_SLOW) {}

				StormByte::BinaryData DoSerialize() const noexcept override { return {}; }
		};
		class StopServerRequest: public Generic {
			public:
				StopServerRequest(): Generic(Opcode::C_MSG_STOPSERVER) {}

				StormByte::BinaryData DoSerialize() const noexcept override { return {}; }
		};
		class EchoText: public Generic {
			public:
				explicit EchoText(std::string text) noexcept:
					Generic(Opcode::C_MSG_ECHOTEXT), m_text(std::move(text)) {}

				StormByte::BinaryData DoSerialize() const noexcept override {
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
					Generic(Opcode::S_MSG_REPLYTEXT), m_text(std::move(text)) {}

				StormByte::BinaryData DoSerialize() const noexcept override {
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
					Generic(Opcode::C_MSG_SUMNUMBERS), m_numbers(std::move(numbers)) {}

				StormByte::BinaryData DoSerialize() const noexcept override {
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
					Generic(Opcode::S_MSG_REPLYSUM), m_sum(sum) {}

				StormByte::BinaryData DoSerialize() const noexcept override {
					return Serializable<int>(m_sum).Serialize();
				}

				int GetSum() const noexcept {
					return m_sum;
				}

			private:
				int m_sum;
		};
	}

	DeserializePacketFunction DeserializeFunction() {
		return [](Transport::Packet::OpcodeType opcode, Consumer consumer, StormByte::Shared<Log> logger) -> PacketPointer {
			(void)logger;
			StormByte::BinaryData data;
			consumer.ExtractUntilEoF(data);
			switch(static_cast<Packet::Opcode>(opcode)) {
				case Packet::Opcode::C_MSG_ASKNAMELIST: {
					auto expected_amount = Serializable<std::size_t>::Deserialize(data);
					if (!expected_amount) {
						return nullptr;
					}

					return std::make_shared<Packet::AskNameList>(*expected_amount);
				}

				case Packet::Opcode::S_MSG_RESPONDNAMELIST: {
					auto expected_names = Serializable<std::vector<std::string>>::Deserialize(data);
					if (!expected_names) {
						return nullptr;
					}

					return std::make_shared<Packet::AnswerNameList>(*expected_names);
				}

				case Packet::Opcode::C_MSG_ASKRANDOMNUMBER: {
					return std::make_shared<Packet::AskRandomNumber>();
				}

				case Packet::Opcode::C_MSG_DISCONNECT:
					return std::make_shared<Packet::DisconnectRequest>();
				case Packet::Opcode::C_MSG_SLOW:
					return std::make_shared<Packet::SlowRequest>();
				case Packet::Opcode::S_MSG_SLOW:
					return std::make_shared<Packet::SlowReply>();
				case Packet::Opcode::C_MSG_STOPSERVER:
					return std::make_shared<Packet::StopServerRequest>();
				case Packet::Opcode::S_MSG_RESPONDRANDOMNUMBER: {
					auto expected_number = Serializable<int>::Deserialize(data);
					if (!expected_number) {
						return nullptr;
					}

					return std::make_shared<Packet::AnswerRandomNumber>(*expected_number);
				}

				case Packet::Opcode::C_MSG_SENDLARGEDATA: {
					// Real payload (moved into packet) — no second synthetic 20 MiB string
					auto expected_data = Serializable<std::string>::Deserialize(data);
					if (!expected_data) {
						return nullptr;
					}

					return std::make_shared<Packet::LargeData>(std::move(*expected_data));
				}

				case Packet::Opcode::S_MSG_REPLYLARGEDATAECHOED: {
					auto expected_data = Serializable<std::string>::Deserialize(data);
					if (!expected_data) {
						return nullptr;
					}

					return std::make_shared<Packet::AnswerLargeDataEchoed>(std::move(*expected_data));
				}

				case Packet::Opcode::C_MSG_PING:
					return std::make_shared<Packet::Ping>();
				case Packet::Opcode::S_MSG_PONG:
					return std::make_shared<Packet::Pong>();
				case Packet::Opcode::C_MSG_ECHOTEXT: {
					auto expected_text = Serializable<std::string>::Deserialize(data);
					if (!expected_text) {
						return nullptr;
					}

					return std::make_shared<Packet::EchoText>(std::move(*expected_text));
				}

				case Packet::Opcode::S_MSG_REPLYTEXT: {
					auto expected_text = Serializable<std::string>::Deserialize(data);
					if (!expected_text) {
						return nullptr;
					}

					return std::make_shared<Packet::ReplyText>(std::move(*expected_text));
				}

				case Packet::Opcode::C_MSG_SUMNUMBERS: {
					auto expected_numbers = Serializable<std::vector<int>>::Deserialize(data);
					if (!expected_numbers) {
						return nullptr;
					}

					return std::make_shared<Packet::SumNumbers>(std::move(*expected_numbers));
				}

				case Packet::Opcode::S_MSG_REPLYSUM: {
					auto expected_sum = Serializable<int>::Deserialize(data);
					if (!expected_sum) {
						return nullptr;
					}

					return std::make_shared<Packet::ReplySum>(*expected_sum);
				}

				default:
					return nullptr;
			}
		};
	}

	using ExpectedNameList = NetExpected<std::vector<std::string>>;
	using ExpectedRandomNumber = NetExpected<int>;
	using ExpectedLargeData = NetExpected<std::string>;

	/** @brief XOR transform stage for framed test payloads. */
	class XorPipe final: public Buf::Pipe {
		public:
			void Run(Buf::ReadOnly& in, Buf::WriteOnly& out,
				const StormByte::Shared<Log>& log) override {
			log << Level::Debug << "XOR Pipe: Starting..." << std::endl;
			constexpr StormByte::ByteSize max_chunk{10 * 1024 * 1024};

			while (!in.EoF()) {
				StormByte::BinaryData data;

				// Blocks until ≥1 byte or EoF/error (no yield spin)
				if (!in.Extract(StormByte::ByteSize{1}, data) || data.empty()) {
					if (in.EoF())
						break;
					continue;
				}

				// Non-blocking grab of the rest of the current burst (capped)
				const StormByte::ByteSize extra = std::min(in.Available(), max_chunk - data.size());
				if (extra > StormByte::ByteSize{0}) {
					StormByte::BinaryData more;
					if (in.Extract(extra, more) && !more.empty()) {
						data.insert(data.end(),
							std::make_move_iterator(more.begin()),
							std::make_move_iterator(more.end()));
					}
				}

				for (auto& b : data)
					b ^= std::byte{0xAB};

				if (!out.Write(std::move(data))) {
					log << Level::Error << "XOR Pipe: Write failed" << std::endl;
					out.SetError();
					return;
				}
			}

			out.Close();
			log << Level::Debug << "XOR Pipe: Finished." << std::endl;
			}

			PointerType Clone() const noexcept override {
				return MakePointer<XorPipe>();
			}

			PointerType Move() noexcept override {
				return MakePointer<XorPipe>();
			}
	};

	class Client: public Net::Client {
		public:
			Client(StormByte::Shared<Log> logger) noexcept:
			Net::Client(DeserializeFunction(), logger) {}

			~Client() noexcept = default;

			Pipeline InputPipeline() const noexcept override {
				Pipeline pipeline;
				pipeline.Add(XorPipe{});
				return pipeline;
			}

			Pipeline OutputPipeline() const noexcept override {
				Pipeline pipeline;
				pipeline.Add(XorPipe{});
				return pipeline;
			}

			ExpectedNameList RequestNameList(const std::size_t& amount) noexcept {
				Packet::AskNameList request_packet(amount);
				auto received_packet = Send(request_packet);
				if (!received_packet) {
					return SB::Unexpected<Net::Exception>("Client::RequestNameList: failed to send/receive AskNameList packet");
				}

				std::shared_ptr<Packet::AnswerNameList> namelist_packet = std::dynamic_pointer_cast<Packet::AnswerNameList>(received_packet);
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

				std::shared_ptr<Packet::AnswerRandomNumber> answer_packet = std::dynamic_pointer_cast<Packet::AnswerRandomNumber>(response_packet);
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

				std::shared_ptr<Packet::AnswerLargeDataEchoed> answer_packet = std::dynamic_pointer_cast<Packet::AnswerLargeDataEchoed>(response_packet);
				if (!answer_packet) {
					return SB::Unexpected<Net::Exception>("Client::RequestLargeDataSize: received unexpected packet opcode ({})", response_packet->Opcode());
				}

				// Move data out of the packet so the shared_ptr can die without retaining 20 MiB
				return answer_packet->TakeData();
			}

			bool RequestPing() noexcept {
				Packet::Ping request_packet;
				auto response_packet = Send(request_packet);
				if (!response_packet) {
					return false;
				}

				return std::dynamic_pointer_cast<Packet::Pong>(response_packet) != nullptr;
			}

			bool RequestDisconnect() noexcept {
				Packet::DisconnectRequest request_packet;
				return Send(request_packet) == nullptr;
			}

			bool RequestSlow() noexcept {
				Packet::SlowRequest request_packet;
				auto response_packet = Send(request_packet);
				return std::dynamic_pointer_cast<Packet::SlowReply>(response_packet) != nullptr;
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

				auto answer_packet = std::dynamic_pointer_cast<Packet::ReplyText>(response_packet);
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

				auto answer_packet = std::dynamic_pointer_cast<Packet::ReplySum>(response_packet);
				if (!answer_packet) {
					return SB::Unexpected<Net::Exception>("Client::RequestSum: received unexpected packet opcode ({})", response_packet->Opcode());
				}

				return answer_packet->GetSum();
			}
	};

	class Server: public Net::Server {
		public:
			Server(StormByte::Shared<Log> logger) noexcept:
			Net::Server(DeserializeFunction(), logger) {}

			~Server() noexcept = default;

			Pipeline InputPipeline() const noexcept override {
				Pipeline pipeline;
				pipeline.Add(XorPipe{});
				return pipeline;
			}

			Pipeline OutputPipeline() const noexcept override {
				Pipeline pipeline;
				pipeline.Add(XorPipe{});
				return pipeline;
			}

		private:
			PacketPointer ProcessClientPacket(std::string_view client_uuid, PacketPointer packet) noexcept override {
				(void)client_uuid;
				switch(static_cast<Packet::Opcode>(packet->Opcode())) {
					case Packet::Opcode::C_MSG_ASKNAMELIST: {
						auto ask_packet = std::dynamic_pointer_cast<Packet::AskNameList>(packet);
						if (!ask_packet) {
							return nullptr;
						}

						std::size_t amount = ask_packet->GetAmount();

						std::vector<std::string> names;
						for (std::size_t i = 0; i < amount; ++i) {
							names.push_back("Name_" + std::to_string(i + 1));
						}

						return std::make_shared<Packet::AnswerNameList>(names);
					}

					case Packet::Opcode::C_MSG_ASKRANDOMNUMBER: {
						static thread_local std::mt19937 gen{[](){
							std::random_device rd;
							unsigned int seed = rd();
							if (seed == 0) {
								seed = static_cast<unsigned int>(std::chrono::high_resolution_clock::now().time_since_epoch().count());
							}

							return seed;
						}()};
						std::uniform_int_distribution<int> dist(0, 99);
						int random_number = dist(gen);
						return std::make_shared<Packet::AnswerRandomNumber>(random_number);
					}

					case Packet::Opcode::C_MSG_SENDLARGEDATA: {
						auto large_data_packet = std::dynamic_pointer_cast<Packet::LargeData>(packet);
						if (!large_data_packet) {
							return nullptr;
						}

						// Move payload into the answer — no extra 20 MiB copy
						return std::make_shared<Packet::AnswerLargeDataEchoed>(large_data_packet->TakeData());
					}

					case Packet::Opcode::C_MSG_PING:
						return std::make_shared<Packet::Pong>();
					case Packet::Opcode::C_MSG_SLOW:
						std::this_thread::sleep_for(std::chrono::milliseconds(500));
						return std::make_shared<Packet::SlowReply>();
					case Packet::Opcode::C_MSG_STOPSERVER:
						Disconnect();
						return nullptr;
					case Packet::Opcode::C_MSG_DISCONNECT:
						DisconnectClient(client_uuid);
						return nullptr;
					case Packet::Opcode::C_MSG_ECHOTEXT: {
						auto text_packet = std::dynamic_pointer_cast<Packet::EchoText>(packet);
						if (!text_packet) {
							return nullptr;
						}

						return std::make_shared<Packet::ReplyText>(text_packet->GetText());
					}

					case Packet::Opcode::C_MSG_SUMNUMBERS: {
						auto numbers_packet = std::dynamic_pointer_cast<Packet::SumNumbers>(packet);
						if (!numbers_packet) {
							return nullptr;
						}

						const auto& numbers = numbers_packet->GetNumbers();
						const int sum = std::accumulate(numbers.begin(), numbers.end(), 0);
						return std::make_shared<Packet::ReplySum>(sum);
					}

					default:
						return nullptr;
				}

				return {};
			}
	};
}

int TestDeserializerFunctionCopy() {
	constexpr std::string_view test_name = "TestDeserializerFunctionCopy";
	int invocation_count = 0;
	int destruction_count = 0;
	struct Probe {
		int* invocations;
		int* destructions;

		~Probe() noexcept {
			++*destructions;
		}

		PacketPointer operator()(Transport::Packet::OpcodeType, Consumer,
			StormByte::Shared<Log>) const {
			++*invocations;
			return nullptr;
		}
	};

	{
		Probe probe{&invocation_count, &destruction_count};
		DeserializePacketFunction original{probe};
		DeserializePacketFunction copy = original;
		(void)copy(0, Consumer{}, {});
		ASSERT_TRUE(test_name, invocation_count == 1);
	}

	ASSERT_TRUE(test_name, invocation_count == 1);
	ASSERT_TRUE(test_name, destruction_count == 3);
	return 0;
}

int TestRemoteFileMountCodec() {
	constexpr std::string_view fn_name = "TestRemoteFileMountCodec";
	using Mount = Net::RemoteFileMount;

	for (const Mount mount: { Mount::NotAuthorized(), Mount::Unavailable() }) {
		const StormByte::BinaryData encoded = Serializable<Mount>(mount).Serialize();
		const auto decoded = Serializable<Mount>::Deserialize(encoded);
		ASSERT_TRUE(fn_name, decoded.has_value());
		ASSERT_TRUE(fn_name, decoded->Result() == mount.Result());
		ASSERT_TRUE(fn_name, decoded->Port() == 0);
		ASSERT_TRUE(fn_name, decoded->Mode() == Mount::Access::None);
	}

	StormByte::BinaryData authorized;
	authorized.append(Serializable<std::uint32_t>(0x5342464Du).Serialize());
	authorized.append(Serializable<std::uint16_t>(1).Serialize());
	authorized.append(Serializable<std::uint16_t>(7081).Serialize());
	authorized.append(Serializable<std::uint16_t>(30).Serialize());
	authorized.append(Serializable<std::uint8_t>(static_cast<std::uint8_t>(Mount::Status::Authorized)).Serialize());
	authorized.append(Serializable<std::uint8_t>(static_cast<std::uint8_t>(Mount::Access::Read)).Serialize());
	Mount::ChannelToken token{};
	token.back() = std::byte{0x5A};
	authorized.append(std::span<const std::byte>{token});
	const auto decoded_authorized = Serializable<Mount>::Deserialize(authorized);
	ASSERT_TRUE(fn_name, decoded_authorized.has_value());
	ASSERT_TRUE(fn_name, decoded_authorized->Result() == Mount::Status::Authorized);
	ASSERT_TRUE(fn_name, decoded_authorized->Port() == 7081);
	ASSERT_TRUE(fn_name, decoded_authorized->MaximumTimeoutSeconds() == 30);
	ASSERT_TRUE(fn_name, decoded_authorized->Token() == token);

	StormByte::BinaryData truncated = authorized;
	truncated.pop_back();
	ASSERT_FALSE(fn_name, Serializable<Mount>::Deserialize(truncated).has_value());
	StormByte::BinaryData extended = authorized;
	extended.push_back(std::byte{0});
	ASSERT_FALSE(fn_name, Serializable<Mount>::Deserialize(extended).has_value());

	StormByte::BinaryData forged_denial = authorized;
	forged_denial[sizeof(std::uint32_t) + sizeof(std::uint16_t) + sizeof(std::uint16_t) + sizeof(std::uint16_t)] =
		static_cast<std::byte>(Mount::Status::NotAuthorized);
	ASSERT_FALSE(fn_name, Serializable<Mount>::Deserialize(forged_denial).has_value());
	return 0;
}

int TestRequestNameList() {
	constexpr std::string_view fn_name = "TestRequestNameList";

	::Test::Server server(logger);
	if (!server.Connect(Net::Connection::Protocol::IPv4, HOST, PORT)) {
		logger << Level::Error << fn_name << ": server.Connect failed." << std::endl;
		RETURN_TEST(fn_name, 1);
	}

	std::this_thread::sleep_for(std::chrono::milliseconds(100));

	::Test::Client client(logger);
	if (!client.Connect(Net::Connection::Protocol::IPv4, HOST, PORT)) {
		logger << Level::Error << fn_name << ": client.Connect failed." << std::endl;
		RETURN_TEST(fn_name, 1);
	}

	const std::size_t amount = 3;
	auto names_expected = client.RequestNameList(amount);
	if (!names_expected) {
		logger << Level::Error << fn_name << ": RequestNameList failed: " << names_expected.error()->what() << std::endl;
		RETURN_TEST(fn_name, 1);
	}

	auto names = names_expected.value();
	std::string all_names;
	ASSERT_TRUE(fn_name, names.size() == amount);
	for (std::size_t i = 0; i < amount; ++i) {
		all_names += names[i] + " ";
		ASSERT_TRUE(fn_name, names[i] == ("Name_" + std::to_string(i + 1)));
	}

	logger << Level::Info << fn_name << ": Received names: " << std::string_view{all_names} << std::endl;

	client.Disconnect();
	server.Disconnect();
	RETURN_TEST(fn_name, 0);
}

int TestRequestRandomNumber() {
	constexpr std::string_view fn_name = "TestRequestRandomNumber";

	::Test::Server server(logger);
	if (!server.Connect(Net::Connection::Protocol::IPv4, HOST, PORT)) {
		logger << Level::Error << fn_name << ": server.Connect failed." << std::endl;
		RETURN_TEST(fn_name, 1);
	}

	std::this_thread::sleep_for(std::chrono::milliseconds(100));

	::Test::Client client(logger);
	if (!client.Connect(Net::Connection::Protocol::IPv4, HOST, PORT)) {
		logger << Level::Error << fn_name << ": client.Connect failed." << std::endl;
		RETURN_TEST(fn_name, 1);
	}

	auto number_expected = client.RequestRandomNumber();
	if (!number_expected) {
		logger << Level::Error << fn_name << ": RequestRandomNumber failed: " << number_expected.error()->what() << std::endl;
		RETURN_TEST(fn_name, 1);
	}

	int n = number_expected.value();
	ASSERT_TRUE(fn_name, n >= 0 && n < 100);
	logger << Level::Info << fn_name << ": Received random number: " << n << std::endl;
	client.Disconnect();
	server.Disconnect();
	RETURN_TEST(fn_name, 0);
}

int TestRequestLargeDataEchoed() {
	constexpr std::string_view fn_name = "TestRequestLargeDataEchoed";

	::Test::Server server(logger);
	if (!server.Connect(Net::Connection::Protocol::IPv4, HOST, PORT)) {
		logger << Level::Error << fn_name << ": server.Connect failed." << std::endl;
		RETURN_TEST(fn_name, 1);
	}

	std::this_thread::sleep_for(std::chrono::milliseconds(100));

	::Test::Client client(logger);
	if (!client.Connect(Net::Connection::Protocol::IPv4, HOST, PORT)) {
		logger << Level::Error << fn_name << ": client.Connect failed." << std::endl;
		RETURN_TEST(fn_name, 1);
	}

	auto data_expected = client.RequestLargeDataEcho(large_data_size);
	if (!data_expected) {
		logger << Level::Error << fn_name << ": RequestLargeDataEcho failed: " << data_expected.error()->what() << std::endl;
		RETURN_TEST(fn_name, 1);
	}

	// Single 20 MiB buffer: size + content check without a second reference string
	const std::string& data = data_expected.value();
	ASSERT_EQUAL(fn_name, data.size(), large_data_size);
	ASSERT_TRUE(fn_name, data.find_first_not_of(large_data_repeat_char) == std::string::npos);

	logger << Level::Info << fn_name << ": Received large data size: " << humanreadable_bytes << data.size()
		<< nohumanreadable << std::endl;
	client.Disconnect();
	server.Disconnect();
	RETURN_TEST(fn_name, 0);
}

int TestRequestAdditionalCommands() {
	constexpr std::string_view fn_name = "TestRequestAdditionalCommands";

	::Test::Server server(logger);
	if (!server.Connect(Net::Connection::Protocol::IPv4, HOST, PORT)) {
		logger << Level::Error << fn_name << ": server.Connect failed." << std::endl;
		RETURN_TEST(fn_name, 1);
	}

	std::this_thread::sleep_for(std::chrono::milliseconds(100));

	::Test::Client client(logger);
	if (!client.Connect(Net::Connection::Protocol::IPv4, HOST, PORT)) {
		logger << Level::Error << fn_name << ": client.Connect failed." << std::endl;
		RETURN_TEST(fn_name, 1);
	}

	ASSERT_TRUE(fn_name, client.RequestPing());

	const std::string text = "StormByte network command with spaces and UTF-8: cafe";
	auto echoed_text = client.RequestEchoText(text);
	ASSERT_TRUE(fn_name, echoed_text.has_value());
	ASSERT_TRUE(fn_name, echoed_text.value() == text);

	const std::vector<int> numbers{ -100, 0, 1, 2, 42, 1000 };
	auto sum = client.RequestSum(numbers);
	ASSERT_TRUE(fn_name, sum.has_value());
	ASSERT_EQUAL(fn_name, sum.value(), 945);

	client.Disconnect();
	server.Disconnect();
	RETURN_TEST(fn_name, 0);
}

int TestClientRetryAfterFailedConnect() {
	constexpr std::string_view fn_name = "TestClientRetryAfterFailedConnect";
	::Test::Server server(logger);
	if (!server.Connect(Net::Connection::Protocol::IPv4, HOST, PORT))
		RETURN_TEST(fn_name, 1);

	::Test::Client client(logger);
	const auto invalid_protocol = static_cast<Net::Connection::Protocol>(-1);
	ASSERT_FALSE(fn_name, client.Connect(invalid_protocol, HOST, PORT));
	ASSERT_TRUE(fn_name, client.Status() == Net::Connection::Status::Disconnected);
	ASSERT_TRUE(fn_name, client.Connect(Net::Connection::Protocol::IPv4, HOST, PORT));
	ASSERT_TRUE(fn_name, client.RequestPing());

	ASSERT_FALSE(fn_name, client.Connect(Net::Connection::Protocol::IPv4, HOST, PORT));
	ASSERT_TRUE(fn_name, client.Status() == Net::Connection::Status::Connected);
	ASSERT_TRUE(fn_name, client.RequestPing());

	client.Disconnect();
	ASSERT_TRUE(fn_name, client.Status() == Net::Connection::Status::Disconnected);
	ASSERT_TRUE(fn_name, client.Connect(Net::Connection::Protocol::IPv4, HOST, PORT));
	ASSERT_TRUE(fn_name, client.RequestPing());
	client.Disconnect();
	server.Disconnect();
	RETURN_TEST(fn_name, 0);
}

int TestServerRetryAfterFailedConnectAndRestart() {
	constexpr std::string_view fn_name = "TestServerRetryAfterFailedConnectAndRestart";
	::Test::Server server(logger);
	const auto invalid_protocol = static_cast<Net::Connection::Protocol>(-1);
	ASSERT_FALSE(fn_name, server.Connect(invalid_protocol, HOST, PORT));
	ASSERT_TRUE(fn_name, server.Status() == Net::Connection::Status::Disconnected);

	ASSERT_TRUE(fn_name, server.Connect(Net::Connection::Protocol::IPv4, HOST, PORT));
	::Test::Client first_client(logger);
	ASSERT_TRUE(fn_name, first_client.Connect(Net::Connection::Protocol::IPv4, HOST, PORT));
	ASSERT_TRUE(fn_name, first_client.RequestPing());
	first_client.Disconnect();
	server.Disconnect();
	ASSERT_TRUE(fn_name, server.Status() == Net::Connection::Status::Disconnected);

	ASSERT_TRUE(fn_name, server.Connect(Net::Connection::Protocol::IPv4, HOST, PORT));
	::Test::Client second_client(logger);
	ASSERT_TRUE(fn_name, second_client.Connect(Net::Connection::Protocol::IPv4, HOST, PORT));
	ASSERT_TRUE(fn_name, second_client.RequestPing());
	second_client.Disconnect();
	server.Disconnect();
	RETURN_TEST(fn_name, 0);
}

int TestMalformedFramesDisconnectOnlyPeer() {
	constexpr std::string_view fn_name = "TestMalformedFramesDisconnectOnlyPeer";
	::Test::Server server(logger);
	if (!server.Connect(Net::Connection::Protocol::IPv4, HOST, PORT))
		RETURN_TEST(fn_name, 1);

	auto make_header = [](const Transport::Packet::OpcodeType opcode, const std::size_t payload_size) {
		StormByte::BinaryData header = Serializable<Transport::Packet::OpcodeType>(opcode).Serialize();
		const StormByte::BinaryData size_bytes = Serializable<std::size_t>(payload_size).Serialize();
		header.insert(header.end(), size_bytes.begin(), size_bytes.end());
		return header;
	};

	const RawSocket unknown_opcode_socket = ConnectRawSocket();
	if (unknown_opcode_socket == invalid_raw_socket) {
		server.Disconnect();
		RETURN_TEST(fn_name, 1);
	}
	const StormByte::BinaryData unknown_opcode_frame = make_header(
		static_cast<Transport::Packet::OpcodeType>(0xFFFF), 0);
	const bool unknown_sent = SendRawBytes(unknown_opcode_socket,
		std::span<const std::byte>{unknown_opcode_frame.data(), unknown_opcode_frame.size()});
	const bool unknown_closed = unknown_sent && WaitForRawDisconnect(unknown_opcode_socket, std::chrono::seconds{3});
	CloseRawSocket(unknown_opcode_socket);
	ASSERT_TRUE(fn_name, unknown_closed);

	const RawSocket malformed_payload_socket = ConnectRawSocket();
	if (malformed_payload_socket == invalid_raw_socket) {
		server.Disconnect();
		RETURN_TEST(fn_name, 1);
	}
	const StormByte::BinaryData malformed_header = make_header(
		static_cast<Transport::Packet::OpcodeType>(::Test::Packet::Opcode::C_MSG_ASKNAMELIST), 1);
	const std::byte malformed_payload{0xAB};
	const bool malformed_sent = SendRawBytes(malformed_payload_socket,
		std::span<const std::byte>{malformed_header.data(), malformed_header.size()})
		&& SendRawBytes(malformed_payload_socket, std::span<const std::byte>{&malformed_payload, 1});
	const bool malformed_closed = malformed_sent
		&& WaitForRawDisconnect(malformed_payload_socket, std::chrono::seconds{3});
	CloseRawSocket(malformed_payload_socket);
	ASSERT_TRUE(fn_name, malformed_closed);

	const RawSocket truncated_payload_socket = ConnectRawSocket();
	if (truncated_payload_socket == invalid_raw_socket) {
		server.Disconnect();
		RETURN_TEST(fn_name, 1);
	}
	const StormByte::BinaryData truncated_header = make_header(
		static_cast<Transport::Packet::OpcodeType>(::Test::Packet::Opcode::C_MSG_ECHOTEXT), 16);
	const std::byte partial_payload{0xAB};
	const bool partial_frame_sent = SendRawBytes(truncated_payload_socket,
		std::span<const std::byte>{truncated_header.data(), truncated_header.size()})
		&& SendRawBytes(truncated_payload_socket, std::span<const std::byte>{&partial_payload, 1});
	CloseRawSocket(truncated_payload_socket);
	ASSERT_TRUE(fn_name, partial_frame_sent);

	::Test::Client healthy_client(logger);
	ASSERT_TRUE(fn_name, healthy_client.Connect(Net::Connection::Protocol::IPv4, HOST, PORT));
	ASSERT_TRUE(fn_name, healthy_client.RequestPing());
	healthy_client.Disconnect();
	server.Disconnect();
	RETURN_TEST(fn_name, 0);
}

int TestManyConcurrentClientsKeepResponsesIsolated() {
	constexpr std::string_view fn_name = "TestManyConcurrentClientsKeepResponsesIsolated";
	constexpr std::size_t client_count = 12;
	constexpr std::size_t requests_per_client = 4;
	::Test::Server server(logger);
	if (!server.Connect(Net::Connection::Protocol::IPv4, HOST, PORT))
		RETURN_TEST(fn_name, 1);

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

	for (auto& client_thread : clients) {
		if (client_thread.joinable())
			client_thread.join();
	}

	for (const int result : client_results)
		ASSERT_EQUAL(fn_name, result, 0);

	server.Disconnect();
	RETURN_TEST(fn_name, 0);
}

int TestClientDisconnectKeepsServerAlive() {
	constexpr std::string_view fn_name = "TestClientDisconnectKeepsServerAlive";

	::Test::Server server(logger);
	if (!server.Connect(Net::Connection::Protocol::IPv4, HOST, PORT)) {
		logger << Level::Error << fn_name << ": server.Connect failed." << std::endl;
		RETURN_TEST(fn_name, 1);
	}

	std::this_thread::sleep_for(std::chrono::milliseconds(100));

	::Test::Client first_client(logger);
	if (!first_client.Connect(Net::Connection::Protocol::IPv4, HOST, PORT)) {
		logger << Level::Error << fn_name << ": first client.Connect failed." << std::endl;
		RETURN_TEST(fn_name, 1);
	}

	ASSERT_TRUE(fn_name, first_client.RequestPing());
	first_client.Disconnect();

	std::this_thread::sleep_for(std::chrono::milliseconds(100));

	::Test::Client second_client(logger);
	if (!second_client.Connect(Net::Connection::Protocol::IPv4, HOST, PORT)) {
		logger << Level::Error << fn_name << ": second client.Connect failed." << std::endl;
		RETURN_TEST(fn_name, 1);
	}

	ASSERT_TRUE(fn_name, second_client.RequestPing());
	second_client.Disconnect();

	server.Disconnect();
	RETURN_TEST(fn_name, 0);
}

int TestDisconnectRequestedByHandler() {
	constexpr std::string_view fn_name = "TestDisconnectRequestedByHandler";
	::Test::Server server(logger);
	if (!server.Connect(Net::Connection::Protocol::IPv4, HOST, PORT)) {
		RETURN_TEST(fn_name, 1);
	}

	std::this_thread::sleep_for(std::chrono::milliseconds(100));
	::Test::Client first_client(logger);
	if (!first_client.Connect(Net::Connection::Protocol::IPv4, HOST, PORT)) {
		RETURN_TEST(fn_name, 1);
	}

	ASSERT_TRUE(fn_name, first_client.RequestDisconnect());
	first_client.Disconnect();
	std::this_thread::sleep_for(std::chrono::milliseconds(100));
	::Test::Client second_client(logger);
	if (!second_client.Connect(Net::Connection::Protocol::IPv4, HOST, PORT)) {
		RETURN_TEST(fn_name, 1);
	}

	ASSERT_TRUE(fn_name, second_client.RequestPing());
	second_client.Disconnect();
	server.Disconnect();
	RETURN_TEST(fn_name, 0);
}

int TestSlowHandlerDoesNotBlockOtherClients() {
	constexpr std::string_view fn_name = "TestSlowHandlerDoesNotBlockOtherClients";
	::Test::Server server(logger);
	if (!server.Connect(Net::Connection::Protocol::IPv4, HOST, PORT)) {
		RETURN_TEST(fn_name, 1);
	}

	std::this_thread::sleep_for(std::chrono::milliseconds(100));
	::Test::Client slow_client(logger);
	::Test::Client fast_client(logger);
	ASSERT_TRUE(fn_name, slow_client.Connect(Net::Connection::Protocol::IPv4, HOST, PORT));
	ASSERT_TRUE(fn_name, fast_client.Connect(Net::Connection::Protocol::IPv4, HOST, PORT));
	std::atomic<bool> slow_result{false};
	std::thread slow_thread([&]() { slow_result.store(slow_client.RequestSlow()); });
	std::this_thread::sleep_for(std::chrono::milliseconds(50));
	const auto start = std::chrono::steady_clock::now();
	ASSERT_TRUE(fn_name, fast_client.RequestPing());
	const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
		std::chrono::steady_clock::now() - start).count();
	ASSERT_TRUE(fn_name, elapsed < 300);
	if (slow_thread.joinable()) {
		slow_thread.join();
	}

	ASSERT_TRUE(fn_name, slow_result.load());
	slow_client.Disconnect();
	fast_client.Disconnect();
	server.Disconnect();
	RETURN_TEST(fn_name, 0);
}

int TestStopRequestedByHandler() {
	constexpr std::string_view fn_name = "TestStopRequestedByHandler";
	::Test::Server server(logger);
	if (!server.Connect(Net::Connection::Protocol::IPv4, HOST, PORT)) {
		RETURN_TEST(fn_name, 1);
	}

	std::this_thread::sleep_for(std::chrono::milliseconds(100));
	::Test::Client client(logger);
	ASSERT_TRUE(fn_name, client.Connect(Net::Connection::Protocol::IPv4, HOST, PORT));
	ASSERT_TRUE(fn_name, client.RequestStopServer());
	client.Disconnect();
	for (int attempt = 0; attempt < 40 && server.Status() != Connection::Status::Disconnected; ++attempt) {
		std::this_thread::sleep_for(std::chrono::milliseconds(25));
	}

	ASSERT_TRUE(fn_name, server.Status() == Connection::Status::Disconnected);
	RETURN_TEST(fn_name, 0);
}

int TestShutdownWithPendingTask() {
	constexpr std::string_view fn_name = "TestShutdownWithPendingTask";
	::Test::Server server(logger);
	if (!server.Connect(Net::Connection::Protocol::IPv4, HOST, PORT)) {
		RETURN_TEST(fn_name, 1);
	}

	std::this_thread::sleep_for(std::chrono::milliseconds(100));
	::Test::Client client(logger);
	ASSERT_TRUE(fn_name, client.Connect(Net::Connection::Protocol::IPv4, HOST, PORT));
	std::thread pending_thread([&]() {
		(void)client.RequestSlow();
	});
	std::this_thread::sleep_for(std::chrono::milliseconds(50));
	server.Disconnect();
	if (pending_thread.joinable()) {
		pending_thread.join();
	}

	client.Disconnect();
	ASSERT_TRUE(fn_name, server.Status() == Connection::Status::Disconnected);
	RETURN_TEST(fn_name, 0);
}

int TestDisconnectDuringSlowHandler() {
	constexpr std::string_view fn_name = "TestDisconnectDuringSlowHandler";
	::Test::Server server(logger);
	if (!server.Connect(Net::Connection::Protocol::IPv4, HOST, PORT)) {
		RETURN_TEST(fn_name, 1);
	}

	std::this_thread::sleep_for(std::chrono::milliseconds(100));
	::Test::Client abandoned_client(logger);
	ASSERT_TRUE(fn_name, abandoned_client.Connect(Net::Connection::Protocol::IPv4, HOST, PORT));
	std::thread pending_thread([&]() {
		(void)abandoned_client.RequestSlow();
	});
	std::this_thread::sleep_for(std::chrono::milliseconds(50));
	abandoned_client.Disconnect();
	if (pending_thread.joinable()) {
		pending_thread.join();
	}

	::Test::Client surviving_client(logger);
	ASSERT_TRUE(fn_name, surviving_client.Connect(Net::Connection::Protocol::IPv4, HOST, PORT));
	ASSERT_TRUE(fn_name, surviving_client.RequestPing());
	surviving_client.Disconnect();
	server.Disconnect();
	RETURN_TEST(fn_name, 0);
}

int TestFragmentedAndBatchedFrames() {
	constexpr std::string_view fn_name = "TestFragmentedAndBatchedFrames";
	constexpr std::size_t frame_header_size = sizeof(Transport::Packet::OpcodeType) + sizeof(std::size_t);

	::Test::Server server(logger);
	if (!server.Connect(Net::Connection::Protocol::IPv4, HOST, PORT)) {
		logger << Level::Error << fn_name << ": server.Connect failed." << std::endl;
		RETURN_TEST(fn_name, 1);
	}

	std::this_thread::sleep_for(std::chrono::milliseconds(100));

	const RawSocket socket_handle = ConnectRawSocket();
	if (socket_handle == invalid_raw_socket) {
		logger << Level::Error << fn_name << ": raw socket connect failed." << std::endl;
		RETURN_TEST(fn_name, 1);
	}

	auto make_wire_frame = [](const ::Test::Packet::Opcode opcode, const StormByte::BinaryData& payload) {
		StormByte::BinaryData frame = Serializable<Transport::Packet::OpcodeType>(
			static_cast<Transport::Packet::OpcodeType>(opcode)).Serialize();
		const StormByte::BinaryData payload_size = Serializable<std::size_t>(payload.size()).Serialize();
		frame.insert(frame.end(), payload_size.begin(), payload_size.end());
		frame.insert(frame.end(), payload.begin(), payload.end());
		return frame;
	};

	auto receive_frame = [&](const ::Test::Packet::Opcode expected_opcode, const std::string* expected_text = nullptr) -> bool {
		StormByte::BinaryData header(frame_header_size);
		if (!ReceiveRawBytes(socket_handle, std::span<std::byte>(header.data(), header.size()))) {
			return false;
		}

		auto opcode = Serializable<Transport::Packet::OpcodeType>::Deserialize(header);
		auto payload_size = Serializable<std::size_t>::Deserialize(std::span<const std::byte>{
			header.data() + sizeof(Transport::Packet::OpcodeType),
			header.size() - sizeof(Transport::Packet::OpcodeType)});
		if (!opcode || !payload_size || *opcode != static_cast<Transport::Packet::OpcodeType>(expected_opcode)) {
			return false;
		}

		if (*payload_size == 0) {
			return expected_text == nullptr;
		}

		if (expected_text == nullptr) {
			return false;
		}

		StormByte::BinaryData payload(*payload_size);
		if (!ReceiveRawBytes(socket_handle, std::span<std::byte>(payload.data(), payload.size()))) {
			return false;
		}

		for (auto& byte: payload) {
			byte ^= std::byte{0xAB};
		}

		auto text = Serializable<std::string>::Deserialize(payload);
		return text && *text == *expected_text;
	};

	const StormByte::BinaryData ping_data = make_wire_frame(::Test::Packet::Opcode::C_MSG_PING, {});
	ASSERT_TRUE(fn_name, SendRawBytes(socket_handle, std::span<const std::byte>(ping_data.data(), 1)));
	ASSERT_TRUE(fn_name, SendRawBytes(socket_handle, std::span<const std::byte>(ping_data.data() + 1, ping_data.size() - 1)));
	ASSERT_TRUE(fn_name, receive_frame(::Test::Packet::Opcode::S_MSG_PONG));

	const std::string text = "fragmented payload";
	StormByte::BinaryData text_payload = Serializable<std::string>(text).Serialize();
	for (auto& byte: text_payload) {
		byte ^= std::byte{0xAB};
	}

	const StormByte::BinaryData text_data = make_wire_frame(::Test::Packet::Opcode::C_MSG_ECHOTEXT, text_payload);
	const std::size_t split = frame_header_size + 2;
	ASSERT_TRUE(fn_name, SendRawBytes(socket_handle, std::span<const std::byte>(text_data.data(), split)));
	ASSERT_TRUE(fn_name, SendRawBytes(socket_handle, std::span<const std::byte>(text_data.data() + split, text_data.size() - split)));
	ASSERT_TRUE(fn_name, receive_frame(::Test::Packet::Opcode::S_MSG_REPLYTEXT, &text));

	StormByte::BinaryData batched;
	batched.insert(batched.end(), ping_data.begin(), ping_data.end());
	batched.insert(batched.end(), ping_data.begin(), ping_data.end());
	ASSERT_TRUE(fn_name, SendRawBytes(socket_handle, std::span<const std::byte>(batched.data(), batched.size())));
	ASSERT_TRUE(fn_name, receive_frame(::Test::Packet::Opcode::S_MSG_PONG));
	ASSERT_TRUE(fn_name, receive_frame(::Test::Packet::Opcode::S_MSG_PONG));

	CloseRawSocket(socket_handle);
	server.Disconnect();
	RETURN_TEST(fn_name, 0);
}

int main() {
	int result = 0;
	result += TestRemoteFileMountCodec();
	result += TestDeserializerFunctionCopy();
	result += TestRequestNameList();
	result += TestRequestRandomNumber();
	result += TestRequestLargeDataEchoed();
	result += TestRequestAdditionalCommands();
	result += TestClientRetryAfterFailedConnect();
	result += TestServerRetryAfterFailedConnectAndRestart();
	result += TestMalformedFramesDisconnectOnlyPeer();
	result += TestManyConcurrentClientsKeepResponsesIsolated();
	result += TestClientDisconnectKeepsServerAlive();
	result += TestDisconnectRequestedByHandler();
	result += TestSlowHandlerDoesNotBlockOtherClients();
	result += TestStopRequestedByHandler();
	result += TestShutdownWithPendingTask();
	result += TestDisconnectDuringSlowHandler();
	result += TestFragmentedAndBatchedFrames();

	if (result == 0) {
		std::cout << "All tests passed!" << std::endl;
	} else {
		std::cout << result << " tests failed." << std::endl;
	}

	return result;
}
