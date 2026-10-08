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

#include <StormByte/network/remote_file_host.hxx>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <limits>
#include <mutex>
#include <ranges>
#include <utility>

namespace StormByte::Network::Detail::RemoteFile {
	using StormByte::Safe::Binary;
	using StormByte::Serializable;
	using StormByte::Unexpected;

	/**
	 * @brief Native file resources whose allocation and destruction stay in Network.
	 */
	class FileStream final {
		public:
			/**
			 * @brief Retain the server-local native path.
			 * @param native_path Authorized native path.
			 */
			explicit FileStream(std::filesystem::path native_path): path(std::move(native_path)) {}

			std::filesystem::path path;	///< Native path used only inside Network.
			std::fstream stream;		///< Native stream closed and destroyed inside Network.
	};

	MountedFile::MountedFile() = default;

	MountedFile::~MountedFile() noexcept = default;

	namespace {
		Message Reply(const Message& request, const Status status) {
			Message response;
			response.opcode = request.opcode;
			response.status = status;
			response.request_id = request.request_id;
			response.offset = request.offset;
			response.token = request.token;
			return response;
		}

		bool EmptyToken(const RemoteFileMount::ChannelToken& token) noexcept {
			return std::ranges::all_of(token, [](const std::byte byte) { return byte == std::byte{0}; });
		}

		Binary TokenKey(const RemoteFileMount::ChannelToken& token) {
			return Binary{std::span<const std::byte>{token}};
		}
	}

	MountRegistry::MountRegistry() = default;

	MountRegistry::~MountRegistry() noexcept = default;

	bool MountRegistry::AddMount(const Token& token, std::string_view path,
		const RemoteFileMount::Access access) noexcept {
		if (EmptyToken(token) || path.empty()
			|| (access != RemoteFileMount::Access::Read && access != RemoteFileMount::Access::Write)) {
			return false;
		}

		try {
			auto native_path = std::filesystem::path{std::u8string_view{reinterpret_cast<const char8_t*>(path.data()), path.size()}};
			const auto native_key = native_path.generic_u8string();
			const Safe::String key{std::string_view{reinterpret_cast<const char*>(native_key.data()), native_key.size()}};
			const auto token_key = TokenKey(token);
			std::scoped_lock lock(m_mutex);
			if (m_tokens.contains(token_key)) return false;
			auto file_it = m_files.find(key);
			Safe::Shared<FileHandle> file;
			if (file_it != m_files.end()) {
				if (access != RemoteFileMount::Access::Read || file_it->second->access != access) return false;
				file = file_it->second;
			} else {
				file = Safe::MakeShared<FileHandle>();
				file->path_key = key;
				file->access = access;
				file->backend = Safe::MakeUnique<FileStream>(std::move(native_path));
				auto& backend = *file->backend;
				if (access == RemoteFileMount::Access::Read) {
					backend.stream.open(backend.path, std::ios::binary | std::ios::in);
				} else {
					backend.stream.open(backend.path, std::ios::binary | std::ios::in | std::ios::out);
					if (!backend.stream.is_open()) {
						backend.stream.clear();
						std::ofstream create(backend.path, std::ios::binary | std::ios::out);
						if (!create) return false;
						create.close();
						if (!create) return false;
						backend.stream.clear();
						backend.stream.open(backend.path, std::ios::binary | std::ios::in | std::ios::out);
					}
				}
				if (!backend.stream.is_open()) return false;
				m_files.emplace(key, file);
			}
			try {
				m_tokens.emplace(token_key, TokenEntry{access, file, false});
			} catch (...) {
				if (file_it == m_files.end()) m_files.erase(key);
				throw;
			}
			return true;
		} catch (...) {
			return false;
		}
	}

	bool MountRegistry::HasToken(const Token& token) const noexcept {
		std::scoped_lock lock(m_mutex);
		return m_tokens.contains(TokenKey(token));
	}

	bool MountRegistry::ReleaseToken(const Token& token) noexcept {
		std::scoped_lock lock(m_mutex);
		const auto token_it = m_tokens.find(TokenKey(token));
		if (token_it == m_tokens.end()) return false;
		auto file = token_it->second.file;
		m_tokens.erase(token_it);
		const bool still_used = std::ranges::any_of(m_tokens, [&file](const auto& entry) {
			return entry.second.file == file;
		});
		if (!still_used) m_files.erase(file->path_key);
		return true;
	}

	Message MountRegistry::Execute(const Message& request) noexcept {
		if (request.opcode == Opcode::CloseToken) {
			if (request.offset != 0 || request.value != 0 || !request.data.empty())
				return Reply(request, Status::Failed);
			return Reply(request, ReleaseToken(request.token) ? Status::Ok : Status::Failed);
		}

		Safe::Shared<FileHandle> file;
		RemoteFileMount::Access access = RemoteFileMount::Access::None;
		{
			std::scoped_lock lock(m_mutex);
			const auto token_it = m_tokens.find(TokenKey(request.token));
			if (token_it == m_tokens.end()) return Reply(request, Status::Failed);
			access = token_it->second.access;
			file = token_it->second.file;
			if (request.opcode == Opcode::Open) {
				if (!request.data.empty() || request.offset != 0 || request.value != 0) {
					return Reply(request, Status::Failed);
				}
				token_it->second.opened = true;
				return Reply(request, Status::Ok);
			}
			if (!token_it->second.opened) return Reply(request, Status::Failed);
		}

		Message response = Reply(request, Status::Failed);
		auto& backend = *file->backend;
		const std::uint64_t max_stream_offset = static_cast<std::uint64_t>(std::numeric_limits<std::streamoff>::max());
		if (request.offset > max_stream_offset) return response;

		switch (request.opcode) {
			case Opcode::Read: {
				if (access != RemoteFileMount::Access::Read || request.value == 0
					|| request.value > max_data_size || !request.data.empty()
					|| request.value > static_cast<std::uint64_t>(std::numeric_limits<std::streamsize>::max())) {
					return response;
				}
				std::scoped_lock lock(file->mutex);
				backend.stream.clear();
				backend.stream.seekg(static_cast<std::streamoff>(request.offset), std::ios::beg);
				if (!backend.stream) return response;
				response.data.resize(StormByte::ByteSize{request.value});
				backend.stream.read(reinterpret_cast<char*>(response.data.data()), static_cast<std::streamsize>(request.value));
				const std::streamsize count = backend.stream.gcount();
				if (count < 0 || backend.stream.bad()) return Reply(request, Status::Failed);
				response.data.resize(StormByte::ByteSize{static_cast<std::size_t>(count)});
				response.value = static_cast<std::uint64_t>(count);
				response.status = static_cast<std::uint64_t>(count) < request.value ? Status::End : Status::Ok;
				backend.stream.clear();
				return response;
			}
			case Opcode::Write: {
				if (access != RemoteFileMount::Access::Write || request.value == 0
					|| request.value != request.data.size() || request.value > max_data_size
					|| request.value > max_stream_offset - request.offset) {
					return response;
				}
				std::scoped_lock lock(file->mutex);
				backend.stream.clear();
				backend.stream.seekp(static_cast<std::streamoff>(request.offset), std::ios::beg);
				backend.stream.write(reinterpret_cast<const char*>(request.data.data()),
					static_cast<std::streamsize>(request.data.size()));
				if (!backend.stream) return response;
				response.status = Status::Ok;
				response.value = request.value;
				return response;
			}
			case Opcode::Size: {
				if (request.offset != 0 || request.value != 0 || !request.data.empty()) return response;
				std::scoped_lock lock(file->mutex);
				if (access == RemoteFileMount::Access::Write) {
					backend.stream.flush();
					if (!backend.stream)
						return response;
				}
				std::error_code size_error;
				const auto size = std::filesystem::file_size(backend.path, size_error);
				if (size_error || size > std::numeric_limits<std::uint64_t>::max())
					return response;
				response.status = Status::Ok;
				response.value = static_cast<std::uint64_t>(size);
				return response;
			}
			case Opcode::Flush: {
				if (access != RemoteFileMount::Access::Write || request.offset != 0
					|| request.value != 0 || !request.data.empty()) return response;
				std::scoped_lock lock(file->mutex);
				backend.stream.flush();
				response.status = backend.stream ? Status::Ok : Status::Failed;
				return response;
			}
			case Opcode::Truncate: {
				if (access != RemoteFileMount::Access::Write || request.offset != 0
					|| request.value != 0 || !request.data.empty()) return response;
				std::scoped_lock lock(file->mutex);
				backend.stream.close();
				backend.stream.clear();
				std::error_code error;
				std::filesystem::resize_file(backend.path, 0, error);
				backend.stream.open(backend.path, std::ios::binary | std::ios::in | std::ios::out);
				response.status = !error && backend.stream.is_open() ? Status::Ok : Status::Failed;
				return response;
			}
			case Opcode::Open:
			case Opcode::Close:
			case Opcode::Seek:
			case Opcode::CloseToken:
			case Opcode::Attach:
			case Opcode::Ping:
			case Opcode::Pong:
				return response;
		}
		return response;
	}

	Host::Host(Connection::Protocol protocol, Safe::String bind_address, Buffer::Pipeline input,
		Buffer::Pipeline output, const std::uint16_t timeout_seconds,
		Safe::Shared<MountRegistry> registry, StormByte::Safe::Shared<StormByte::Logger::Log> logger):
		m_protocol(protocol), m_bind_address(std::move(bind_address)), m_input(std::move(input)),
		m_output(std::move(output)), m_timeout_seconds(timeout_seconds), m_registry(std::move(registry)),
		m_logger(std::move(logger)), m_last_activity(std::chrono::steady_clock::now()) {}

	Host::~Host() noexcept {
		Stop();
	}

	bool Host::Start() noexcept {
		if (m_listener || m_finished.load(Safe::MemoryOrder::Acquire) || !m_registry
			|| m_timeout_seconds < 3 || m_timeout_seconds > 3600) return false;
		try {
			m_listener = Safe::MakeUnique<Socket::Server>(m_protocol, m_logger);
			if (!m_listener->Listen(std::string_view{m_bind_address}, 0)) {
				m_listener.reset();
				return false;
			}
			m_port = m_listener->Port();
			m_last_activity = std::chrono::steady_clock::now();
			return m_port != 0;
		} catch (...) {
			if (m_listener) m_listener->Disconnect();
			m_listener.reset();
			return false;
		}
	}

	std::uint16_t Host::Port() const noexcept {
		return m_port;
	}

	std::uint16_t Host::TimeoutSeconds() const noexcept {
		return m_timeout_seconds;
	}

	bool Host::Finished() const noexcept {
		return m_finished.load(Safe::MemoryOrder::Acquire);
	}

	void Host::Stop() noexcept {
		if (m_stopping.exchange(true, Safe::MemoryOrder::AcqRel)) return;
		if (m_listener) m_listener->Disconnect();
		if (m_active_client) m_active_client->Disconnect();
		ReleaseTokens();
		m_finished.store(true, Safe::MemoryOrder::Release);
	}

	bool Host::RegisterToken(const Token& token) noexcept {
		if (!m_registry || !m_registry->HasToken(token) || Finished()) return false;
		std::scoped_lock lock(m_mutex);
		return m_registered_tokens.emplace(TokenKey(token), true).second;
	}

	void Host::UnregisterToken(const Token& token) noexcept {
		{
			std::scoped_lock lock(m_mutex);
			const auto key = TokenKey(token);
			m_registered_tokens.erase(key);
			m_attached_tokens.erase(key);
		}
		if (m_registry) (void)m_registry->ReleaseToken(token);
	}

	Connection::HandlerType Host::Handle() const noexcept {
		if (m_active_client) return m_active_client->Handle();
		return m_listener ? m_listener->Handle() : Connection::HandlerType{};
	}

	bool Host::WaitingForAccept() const noexcept {
		return m_listener && !m_accepted && !Finished();
	}

	bool Host::CanRead() const noexcept {
		if (Finished() || m_stopping.load(Safe::MemoryOrder::Acquire)) return false;
		if (!m_active_client) return m_listener && !m_accepted;
		std::scoped_lock lock(m_mutex);
		constexpr ByteSize input_limit{max_message_size * 2
			+ sizeof(std::uint64_t) * 2 + 64 * 1024};
		return !m_pending_request && Size{m_output_queue.size()} < Size{4}
			&& m_input_buffer.size() < input_limit;
	}

	bool Host::ReadyForProcessing() const noexcept {
		std::scoped_lock lock(m_mutex);
		return m_pending_request.has_value() && !m_in_flight && !m_task_blocked && !Finished();
	}

	bool Host::HasBufferedFrame() const noexcept {
		if (m_input_buffer.size() < ByteSize{sizeof(std::uint64_t)}) return false;
		const auto expected_size = Serializable<std::uint64_t>::Deserialize(
			std::span<const std::byte>{m_input_buffer.data(), sizeof(std::uint64_t)});
		if (!expected_size || *expected_size == 0 || *expected_size > max_message_size) return true;
		return m_input_buffer.size() >= ByteSize{sizeof(std::uint64_t)} + ByteSize{*expected_size};
	}

	bool Host::HasOutput() const noexcept {
		std::scoped_lock lock(m_mutex);
		return !m_output_buffer.empty() || !m_output_queue.empty();
	}

	void Host::SetTaskBlocked(const bool blocked) noexcept {
		std::scoped_lock lock(m_mutex);
		m_task_blocked = blocked;
	}

	bool Host::Expired() const noexcept {
		return m_port != 0 && std::chrono::steady_clock::now() - m_last_activity
			>= std::chrono::seconds{m_timeout_seconds};
	}

	bool Host::AcceptReady() noexcept {
		if (!m_listener || m_accepted || Finished()) return false;
		auto accepted = m_listener->Accept();
		if (!accepted) return false;
		m_active_client = std::move(accepted.value());
		m_accepted = true;
		m_last_activity = std::chrono::steady_clock::now();
		return true;
	}

	StormByte::Network::ExpectedVoid Host::ReadReady() noexcept {
		if (!m_active_client || Finished()) return Unexpected<ConnectionError>("Peer data plane is not connected");
		if (!HasBufferedFrame()) {
			bool would_block = false;
			auto received = m_active_client->TryRead(would_block);
			if (!received) return Unexpected(received.error());
			if (would_block) return {};
			if (received->empty()) return Unexpected<ConnectionError>("Peer data plane closed");
			constexpr ByteSize input_limit{max_message_size * 2
				+ sizeof(std::uint64_t) * 2 + 64 * 1024};
			if (received->size() > input_limit || m_input_buffer.size() > input_limit - received->size()) {
				return Unexpected<ConnectionError>("Peer data-plane input exceeded its bounded backpressure buffer");
			}
			m_input_buffer.append(std::move(received.value()));
		}
		if (m_input_buffer.size() < ByteSize{sizeof(std::uint64_t)}) return {};

		const auto expected_size = Serializable<std::uint64_t>::Deserialize(
			std::span<const std::byte>{m_input_buffer.data(), sizeof(std::uint64_t)});
		if (!expected_size || *expected_size == 0 || *expected_size > max_message_size) {
			return Unexpected<ConnectionError>("Invalid peer data-plane frame length");
		}
		const ByteSize frame_size = ByteSize{sizeof(std::uint64_t)} + ByteSize{*expected_size};
		if (m_input_buffer.size() < frame_size) return {};

		const std::span<const std::byte> payload_span{m_input_buffer.data() + sizeof(std::uint64_t),
			static_cast<std::size_t>(*expected_size)};
		Binary payload(payload_span);
		m_input_buffer.erase(m_input_buffer.begin(), m_input_buffer.begin() + static_cast<std::ptrdiff_t>(frame_size));
		auto decoded = Process(m_input, std::move(payload), m_logger);
		if (!decoded) return Unexpected(decoded.error());
		auto request = Deserialize(decoded->span());
		if (!request || request->status != Status::Ok || request->request_id <= m_last_request_id) {
			return Unexpected<ConnectionError>("Invalid or repeated peer data-plane request sequence");
		}
		m_last_request_id = request->request_id;
		m_last_activity = std::chrono::steady_clock::now();

		Message response = Reply(request.value(), Status::Failed);
		if (request->opcode == Opcode::Ping) {
			if (!EmptyToken(request->token) || request->offset != 0 || request->value != 0 || !request->data.empty()) {
				return Unexpected<ConnectionError>("Malformed peer data-plane heartbeat");
			}
			response.opcode = Opcode::Pong;
			response.status = Status::Ok;
			return QueueResponse(response) ? ExpectedVoid{} : ExpectedVoid{Unexpected<ConnectionError>("Failed to queue heartbeat response")};
		}
		if (request->opcode == Opcode::Attach) {
			{
				std::scoped_lock lock(m_mutex);
				if (m_in_flight)
					return Unexpected<ConnectionError>("Peer attached a capability during disk I/O");
			}
			const bool valid = request->offset == 0 && request->value == 0 && request->data.empty()
				&& IsRegisteredToken(request->token) && m_registry->HasToken(request->token);
				if (valid) {
					std::scoped_lock lock(m_mutex);
					m_attached_tokens.emplace(TokenKey(request->token), true);
					response.status = Status::Ok;
				}
				return QueueResponse(response) ? ExpectedVoid{} : ExpectedVoid{Unexpected<ConnectionError>("Failed to queue token response")};
		}
		if (request->opcode == Opcode::CloseToken) {
			if (request->offset != 0 || request->value != 0 || !request->data.empty())
				return QueueResponse(response) ? ExpectedVoid{} : ExpectedVoid{Unexpected<ConnectionError>("Failed to queue token-close response")};
			bool attached = false;
			{
				std::scoped_lock lock(m_mutex);
				if (m_in_flight)
					return Unexpected<ConnectionError>("Peer closed a capability during disk I/O");
				const auto key = TokenKey(request->token);
				attached = m_attached_tokens.erase(key) != 0;
				m_registered_tokens.erase(key);
			}
			response.status = attached && m_registry->ReleaseToken(request->token) ? Status::Ok : Status::Failed;
			return QueueResponse(response) ? ExpectedVoid{} : ExpectedVoid{Unexpected<ConnectionError>("Failed to queue token-close response")};
		}
		{
			bool attached = false;
			{
				std::scoped_lock lock(m_mutex);
				attached = m_attached_tokens.contains(TokenKey(request->token));
				if (attached && m_pending_request) {
					return Unexpected<ConnectionError>("Peer exceeded its bounded operation queue");
				}
			}
			if (!attached) {
				return QueueResponse(response) ? ExpectedVoid{} : ExpectedVoid{Unexpected<ConnectionError>("Failed to queue token-fault response")};
			}
			std::scoped_lock lock(m_mutex);
			m_pending_request = std::move(request.value());
		}
		return {};
	}

	Message Host::TakeRequest() noexcept {
		std::scoped_lock lock(m_mutex);
		if (!m_pending_request || m_task_blocked || m_in_flight) return {};
		Message request = std::move(*m_pending_request);
		m_pending_request.reset();
		m_in_flight = true;
		return request;
	}

	void Host::RequeueRequest(Message request) noexcept {
		std::scoped_lock lock(m_mutex);
		m_in_flight = false;
		m_task_blocked = true;
		if (!m_pending_request) m_pending_request = std::move(request);
	}

	Message Host::ProcessRequest(const Message& request) noexcept {
		{
			std::scoped_lock lock(m_mutex);
			if (!m_attached_tokens.contains(TokenKey(request.token))) return Reply(request, Status::Failed);
		}
		return m_registry ? m_registry->Execute(request) : Reply(request, Status::Failed);
	}

	bool Host::QueueEncodedResponse(const Message& response) noexcept {
		try {
			std::scoped_lock encoding(m_encoding_mutex);
			if (m_stopping.load(Safe::MemoryOrder::Acquire) || Finished())
				return false;
			Binary encoded = Serialize(response);
			if (encoded.size() > max_message_size) return false;
			auto transformed = Process(m_output, std::move(encoded), m_logger);
			if (!transformed || transformed->size() > max_message_size) return false;
			const Binary size_bytes = Serializable<std::uint64_t>(static_cast<std::uint64_t>(transformed->size())).Serialize();
			Binary framed;
			framed.append(size_bytes);
			framed.append(std::move(transformed.value()));
			std::scoped_lock lock(m_mutex);
			if (m_stopping.load(Safe::MemoryOrder::Acquire) || Finished() || Size{m_output_queue.size()} >= Size{4})
				return false;
			if (m_output_buffer.empty()) {
				m_output_buffer = std::move(framed);
				m_output_offset = ByteSize{0};
			}
			else
				m_output_queue.push_back(std::move(framed));
			if (response.opcode != Opcode::Pong) {
				m_in_flight = false;
				m_task_blocked = false;
			}
			return true;
		} catch (...) {
			return false;
		}
	}

	bool Host::QueueResponse(const Message& response) noexcept {
		return QueueEncodedResponse(response);
	}

	StormByte::Expected<bool, ConnectionError> Host::FlushOutput() noexcept {
		std::scoped_lock lock(m_mutex);
		if (m_output_buffer.empty()) {
			if (m_output_queue.empty())
				return true;
			m_output_buffer = std::move(m_output_queue.front());
			m_output_queue.erase(m_output_queue.begin());
			m_output_offset = ByteSize{0};
		}
		if (!m_active_client) return Unexpected<ConnectionError>("Peer plane output has no connected socket");
		bool would_block = false;
		const auto remaining = m_output_buffer.span().subspan(static_cast<std::size_t>(m_output_offset));
		auto written = m_active_client->TryWrite(remaining, would_block);
		if (!written) return Unexpected(written.error());
		if (would_block) return false;
		m_output_offset += written.value();
		if (m_output_offset == m_output_buffer.size()) {
			m_output_buffer.clear();
			m_output_offset = ByteSize{0};
		}
		return m_output_buffer.empty() && m_output_queue.empty();
	}

	bool Host::IsRegisteredToken(const Token& token) const noexcept {
		std::scoped_lock lock(m_mutex);
		return m_registered_tokens.contains(TokenKey(token));
	}

	void Host::ReleaseTokens() noexcept {
		Safe::Map<Binary, bool> tokens;
		{
			std::scoped_lock lock(m_mutex);
			tokens.swap(m_registered_tokens);
			m_attached_tokens.clear();
			m_pending_request.reset();
		}
		if (m_registry) {
			for (const auto& entry: tokens) {
				Token token{};
				std::ranges::copy(entry.first.span(), token.begin());
				(void)m_registry->ReleaseToken(token);
			}
		}
	}
}
