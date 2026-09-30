#include <StormByte/network/remote_file_host.hxx>

#include <algorithm>
#include <limits>
#include <ranges>
#include <utility>

namespace StormByte::Network::Detail::RemoteFile {
	using StormByte::BinaryData;
	using StormByte::Serializable;
	using StormByte::Unexpected;

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
	}

	std::size_t MountRegistry::TokenHash::operator()(const Token& token) const noexcept {
		std::size_t hash = 1469598103934665603ull;
		for (const std::byte byte: token) {
			hash = (hash ^ std::to_integer<std::uint8_t>(byte)) * 1099511628211ull;
		}
		return hash;
	}

	bool MountRegistry::AddMount(const Token& token, const std::filesystem::path& path,
		const RemoteFileMount::Access access) noexcept {
		if (EmptyToken(token) || path.empty()
			|| (access != RemoteFileMount::Access::Read && access != RemoteFileMount::Access::Write)) {
			return false;
		}

		try {
			const std::string key = path.generic_string();
			std::scoped_lock lock(m_mutex);
			if (m_tokens.contains(token)) return false;
			auto file_it = m_files.find(key);
			std::shared_ptr<FileHandle> file;
			if (file_it != m_files.end()) {
				if (file_it->second->access != access) return false;
				file = file_it->second;
			} else {
				file = std::make_shared<FileHandle>();
				file->path = path;
				file->access = access;
				if (access == RemoteFileMount::Access::Read) {
					file->stream.open(path, std::ios::binary | std::ios::in);
				} else {
					file->stream.open(path, std::ios::binary | std::ios::in | std::ios::out);
					if (!file->stream.is_open()) {
						file->stream.clear();
						std::ofstream create(path, std::ios::binary | std::ios::out);
						if (!create) return false;
						create.close();
						file->stream.clear();
						file->stream.open(path, std::ios::binary | std::ios::in | std::ios::out);
					}
				}
				if (!file->stream.is_open()) return false;
				m_files.emplace(key, file);
			}
			m_tokens.emplace(token, TokenEntry{access, std::move(file), false});
			return true;
		} catch (...) {
			return false;
		}
	}

	bool MountRegistry::HasToken(const Token& token) const noexcept {
		std::scoped_lock lock(m_mutex);
		return m_tokens.contains(token);
	}

	bool MountRegistry::ReleaseToken(const Token& token) noexcept {
		std::scoped_lock lock(m_mutex);
		const auto token_it = m_tokens.find(token);
		if (token_it == m_tokens.end()) return false;
		auto file = token_it->second.file;
		m_tokens.erase(token_it);
		const bool still_used = std::ranges::any_of(m_tokens, [&file](const auto& entry) {
			return entry.second.file == file;
		});
		if (!still_used) m_files.erase(file->path.generic_string());
		return true;
	}

	Message MountRegistry::Execute(const Message& request) noexcept {
		if (request.opcode == Opcode::CloseToken) {
			return Reply(request, ReleaseToken(request.token) ? Status::Ok : Status::Failed);
		}

		std::shared_ptr<FileHandle> file;
		RemoteFileMount::Access access = RemoteFileMount::Access::None;
		{
			std::scoped_lock lock(m_mutex);
			const auto token_it = m_tokens.find(request.token);
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
				file->stream.clear();
				file->stream.seekg(static_cast<std::streamoff>(request.offset), std::ios::beg);
				if (!file->stream) return response;
				response.data.resize(StormByte::ByteSize{request.value});
				file->stream.read(reinterpret_cast<char*>(response.data.data()), static_cast<std::streamsize>(request.value));
				const std::streamsize count = file->stream.gcount();
				if (count < 0 || file->stream.bad()) return Reply(request, Status::Failed);
				response.data.resize(StormByte::ByteSize{static_cast<std::size_t>(count)});
				response.value = static_cast<std::uint64_t>(count);
				response.status = static_cast<std::uint64_t>(count) < request.value ? Status::End : Status::Ok;
				file->stream.clear();
				return response;
			}
			case Opcode::Write: {
				if (access != RemoteFileMount::Access::Write || request.value == 0
					|| request.value != request.data.size() || request.value > max_data_size
					|| request.value > max_stream_offset - request.offset) {
					return response;
				}
				std::scoped_lock lock(file->mutex);
				file->stream.clear();
				file->stream.seekp(static_cast<std::streamoff>(request.offset), std::ios::beg);
				file->stream.write(reinterpret_cast<const char*>(request.data.data()),
					static_cast<std::streamsize>(request.data.size()));
				if (!file->stream) return response;
				response.status = Status::Ok;
				response.value = request.value;
				return response;
			}
			case Opcode::Size: {
				if (request.offset != 0 || request.value != 0 || !request.data.empty()) return response;
				std::scoped_lock lock(file->mutex);
				file->stream.clear();
				if (access == RemoteFileMount::Access::Write) file->stream.flush();
				file->stream.seekg(0, std::ios::end);
				const std::streampos end = file->stream.tellg();
				if (end < 0 || !file->stream) return response;
				response.status = Status::Ok;
				response.value = static_cast<std::uint64_t>(end);
				file->stream.clear();
				return response;
			}
			case Opcode::Flush: {
				if (access != RemoteFileMount::Access::Write || request.offset != 0
					|| request.value != 0 || !request.data.empty()) return response;
				std::scoped_lock lock(file->mutex);
				file->stream.flush();
				response.status = file->stream ? Status::Ok : Status::Failed;
				return response;
			}
			case Opcode::Truncate: {
				if (access != RemoteFileMount::Access::Write || request.offset != 0
					|| request.value != 0 || !request.data.empty()) return response;
				std::scoped_lock lock(file->mutex);
				file->stream.close();
				file->stream.clear();
				std::error_code error;
				std::filesystem::resize_file(file->path, 0, error);
				file->stream.open(file->path, std::ios::binary | std::ios::in | std::ios::out);
				response.status = !error && file->stream.is_open() ? Status::Ok : Status::Failed;
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

	Host::Host(Connection::Protocol protocol, std::string bind_address, Buffer::Pipeline input,
		Buffer::Pipeline output, const std::uint16_t timeout_seconds,
		std::shared_ptr<MountRegistry> registry, StormByte::Shared<StormByte::Logger::Log> logger):
		m_protocol(protocol), m_bind_address(std::move(bind_address)), m_input(std::move(input)),
		m_output(std::move(output)), m_timeout_seconds(timeout_seconds), m_registry(std::move(registry)),
		m_logger(std::move(logger)), m_last_activity(std::chrono::steady_clock::now()) {}

	Host::~Host() noexcept {
		Stop();
	}

	bool Host::Start() noexcept {
		if (m_listener || m_finished.load(std::memory_order_acquire) || !m_registry
			|| m_timeout_seconds < 3 || m_timeout_seconds > 3600) return false;
		try {
			m_listener = std::make_unique<Socket::Server>(m_protocol, m_logger);
			if (!m_listener->Listen(m_bind_address, 0)) {
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
		return m_finished.load(std::memory_order_acquire);
	}

	void Host::Stop() noexcept {
		if (m_stopping.exchange(true, std::memory_order_acq_rel)) return;
		if (m_listener) m_listener->Disconnect();
		if (m_active_client) m_active_client->Disconnect();
		ReleaseTokens();
		m_finished.store(true, std::memory_order_release);
	}

	bool Host::RegisterToken(const Token& token) noexcept {
		if (!m_registry || !m_registry->HasToken(token) || Finished()) return false;
		std::scoped_lock lock(m_mutex);
		return m_registered_tokens.insert(token).second;
	}

	void Host::UnregisterToken(const Token& token) noexcept {
		{
			std::scoped_lock lock(m_mutex);
			m_registered_tokens.erase(token);
			m_attached_tokens.erase(token);
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
		if (Finished() || m_stopping.load(std::memory_order_acquire)) return false;
		if (!m_active_client) return m_listener && !m_accepted;
		std::scoped_lock lock(m_mutex);
		constexpr std::size_t input_limit = static_cast<std::size_t>(max_message_size * 2
			+ sizeof(std::uint64_t) * 2 + 64 * 1024);
		return !m_pending_request && !m_in_flight && !m_task_blocked && m_output_buffer.empty()
			&& m_input_buffer.size() < input_limit;
	}

	bool Host::ReadyForProcessing() const noexcept {
		std::scoped_lock lock(m_mutex);
		return m_pending_request.has_value() && !m_task_blocked && !Finished();
	}

	bool Host::HasBufferedFrame() const noexcept {
		if (m_input_buffer.size() < sizeof(std::uint64_t)) return false;
		const auto expected_size = Serializable<std::uint64_t>::Deserialize(
			std::span<const std::byte>{m_input_buffer.data(), sizeof(std::uint64_t)});
		if (!expected_size || *expected_size == 0 || *expected_size > max_message_size) return true;
		return m_input_buffer.size() >= sizeof(std::uint64_t) + static_cast<std::size_t>(*expected_size);
	}

	bool Host::HasOutput() const noexcept {
		std::scoped_lock lock(m_mutex);
		return !m_output_buffer.empty();
	}

	void Host::SetTaskBlocked(const bool blocked) noexcept {
		std::scoped_lock lock(m_mutex);
		m_task_blocked = blocked;
	}

	bool Host::Expired() const noexcept {
		{
			std::scoped_lock lock(m_mutex);
			if (m_in_flight) return false;
		}
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
			constexpr std::size_t input_limit = static_cast<std::size_t>(max_message_size * 2
				+ sizeof(std::uint64_t) * 2 + 64 * 1024);
			if (received->size() > input_limit || m_input_buffer.size() > input_limit - received->size()) {
				return Unexpected<ConnectionError>("Peer data-plane input exceeded its bounded backpressure buffer");
			}
			m_input_buffer.append(std::move(received.value()));
		}
		if (m_input_buffer.size() < sizeof(std::uint64_t)) return {};

		const auto expected_size = Serializable<std::uint64_t>::Deserialize(
			std::span<const std::byte>{m_input_buffer.data(), sizeof(std::uint64_t)});
		if (!expected_size || *expected_size == 0 || *expected_size > max_message_size) {
			return Unexpected<ConnectionError>("Invalid peer data-plane frame length");
		}
		const std::size_t frame_size = sizeof(std::uint64_t) + static_cast<std::size_t>(*expected_size);
		if (m_input_buffer.size() < frame_size) return {};

		const std::span<const std::byte> payload_span{m_input_buffer.data() + sizeof(std::uint64_t),
			static_cast<std::size_t>(*expected_size)};
		BinaryData payload(payload_span);
		m_input_buffer.erase(m_input_buffer.begin(), m_input_buffer.begin() + static_cast<std::ptrdiff_t>(frame_size));
		auto decoded = Process(m_input, std::move(payload), m_logger);
		if (!decoded) return Unexpected(decoded.error());
		auto request = Deserialize(std::span<const std::byte>{*decoded});
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
			const bool valid = request->offset == 0 && request->value == 0 && request->data.empty()
				&& IsRegisteredToken(request->token) && m_registry->HasToken(request->token);
				if (valid) {
					std::scoped_lock lock(m_mutex);
					m_attached_tokens.insert(request->token);
					response.status = Status::Ok;
				}
				return QueueResponse(response) ? ExpectedVoid{} : ExpectedVoid{Unexpected<ConnectionError>("Failed to queue token response")};
		}
		if (request->opcode == Opcode::CloseToken) {
			bool attached = false;
			{
				std::scoped_lock lock(m_mutex);
				attached = m_attached_tokens.erase(request->token) != 0;
				m_registered_tokens.erase(request->token);
			}
			response.status = attached && request->offset == 0 && request->value == 0
				&& request->data.empty() && m_registry->ReleaseToken(request->token) ? Status::Ok : Status::Failed;
			return QueueResponse(response) ? ExpectedVoid{} : ExpectedVoid{Unexpected<ConnectionError>("Failed to queue token-close response")};
		}
		{
			bool attached = false;
			{
				std::scoped_lock lock(m_mutex);
				attached = m_attached_tokens.contains(request->token);
				if (attached && (m_pending_request || m_in_flight)) {
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
			if (!m_attached_tokens.contains(request.token)) return Reply(request, Status::Failed);
		}
		return m_registry ? m_registry->Execute(request) : Reply(request, Status::Failed);
	}

	bool Host::QueueEncodedResponse(const Message& response) noexcept {
		try {
			BinaryData encoded = Serialize(response);
			if (encoded.size() > max_message_size) return false;
			auto transformed = Process(m_output, std::move(encoded), m_logger);
			if (!transformed || transformed->size() > max_message_size) return false;
			const BinaryData size_bytes = Serializable<std::uint64_t>(transformed->size()).Serialize();
			BinaryData framed;
			framed.append(size_bytes);
			framed.append(std::move(transformed.value()));
			std::scoped_lock lock(m_mutex);
			if (!m_output_buffer.empty()) return false;
			m_output_buffer = std::move(framed);
			m_output_offset = 0;
			m_in_flight = false;
			m_task_blocked = false;
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
		if (m_output_buffer.empty()) return true;
		if (!m_active_client) return Unexpected<ConnectionError>("Peer plane output has no connected socket");
		bool would_block = false;
		const std::span<const std::byte> remaining{m_output_buffer.data() + m_output_offset,
			m_output_buffer.size() - m_output_offset};
		auto written = m_active_client->TryWrite(remaining, would_block);
		if (!written) return Unexpected(written.error());
		if (would_block) return false;
		m_output_offset += written.value();
		if (m_output_offset == m_output_buffer.size()) {
			m_output_buffer.clear();
			m_output_offset = 0;
		}
		return m_output_buffer.empty();
	}

	bool Host::IsRegisteredToken(const Token& token) const noexcept {
		std::scoped_lock lock(m_mutex);
		return m_registered_tokens.contains(token);
	}

	void Host::Fail() noexcept {
		Stop();
	}

	void Host::ReleaseTokens() noexcept {
		std::set<Token> tokens;
		{
			std::scoped_lock lock(m_mutex);
			tokens.swap(m_registered_tokens);
			m_attached_tokens.clear();
			m_pending_request.reset();
		}
		if (m_registry) {
			for (const Token& token: tokens) (void)m_registry->ReleaseToken(token);
		}
	}
}
