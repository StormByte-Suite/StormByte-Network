#include <StormByte/network/remote_file_protocol.hxx>

#include <StormByte/buffer/consumer.hxx>
#include <StormByte/buffer/producer.hxx>

#include <algorithm>
#include <chrono>
#include <utility>

namespace StormByte::Network::Detail::RemoteFile {
	using StormByte::BinaryData;
	using StormByte::Buffer::Consumer;
	using StormByte::Buffer::ExecutionMode;
	using StormByte::Buffer::Producer;
	using StormByte::Serializable;
	using StormByte::Unexpected;

	BinaryData Serialize(const Message& message) {
		BinaryData payload;
		payload.append(Serializable<std::uint8_t>(static_cast<std::uint8_t>(message.opcode)).Serialize());
		payload.append(Serializable<std::uint8_t>(static_cast<std::uint8_t>(message.status)).Serialize());
		payload.append(Serializable<std::uint64_t>(message.request_id).Serialize());
		payload.append(Serializable<std::uint64_t>(message.offset).Serialize());
		payload.append(Serializable<std::uint64_t>(message.value).Serialize());
		payload.append(message.token);
		payload.append(message.data);
		return payload;
	}

	StormByte::Expected<Message, StormByte::DeserializeError> Deserialize(
		std::span<const std::byte> payload) noexcept {
		constexpr std::size_t header_size = sizeof(std::uint8_t) * 2 + sizeof(std::uint64_t) * 3 + 32;
		if (payload.size() < header_size || payload.size() > max_message_size) {
			return Unexpected<StormByte::DeserializeError>("Invalid remote file message size");
		}

		const auto opcode = Serializable<std::uint8_t>::Deserialize(payload.first(sizeof(std::uint8_t)));
		const auto status = Serializable<std::uint8_t>::Deserialize(payload.subspan(sizeof(std::uint8_t), sizeof(std::uint8_t)));
		const auto request_id = Serializable<std::uint64_t>::Deserialize(payload.subspan(sizeof(std::uint8_t) * 2, sizeof(std::uint64_t)));
		const auto offset = Serializable<std::uint64_t>::Deserialize(payload.subspan(
			sizeof(std::uint8_t) * 2 + sizeof(std::uint64_t), sizeof(std::uint64_t)));
		const auto value = Serializable<std::uint64_t>::Deserialize(payload.subspan(
			sizeof(std::uint8_t) * 2 + sizeof(std::uint64_t) * 2, sizeof(std::uint64_t)));
		if (!opcode || !status || !request_id || !offset || !value || *request_id == 0
			|| *opcode < static_cast<std::uint8_t>(Opcode::Attach)
			|| *opcode > static_cast<std::uint8_t>(Opcode::CloseToken)
			|| *status > static_cast<std::uint8_t>(Status::Failed)) {
			return Unexpected<StormByte::DeserializeError>("Invalid remote file message header");
		}

		Message message;
		message.opcode = static_cast<Opcode>(*opcode);
		message.status = static_cast<Status>(*status);
		message.request_id = *request_id;
		message.offset = *offset;
		message.value = *value;
		constexpr std::size_t token_offset = sizeof(std::uint8_t) * 2 + sizeof(std::uint64_t) * 3;
		std::ranges::copy(payload.subspan(token_offset, message.token.size()), message.token.begin());
		message.data.assign(payload.begin() + static_cast<std::ptrdiff_t>(header_size), payload.end());
		if ((message.opcode == Opcode::Ping || message.opcode == Opcode::Pong) && !message.data.empty()) {
			return Unexpected<StormByte::DeserializeError>("Heartbeat message has an unexpected body");
		}
		return message;
	}

	StormByte::Expected<BinaryData, ConnectionError> Process(Buffer::Pipeline& pipeline, BinaryData input,
		StormByte::Shared<StormByte::Logger::Log> logger) {
		if (input.empty() || input.size() > max_message_size) {
			return Unexpected<ConnectionError>("Invalid remote file pipeline input size");
		}
		try {
			Producer producer;
			producer.Write(std::move(input));
			producer.Close();
			Consumer output = pipeline.Process(producer.Consumer(), logger, ExecutionMode::Sync);
			BinaryData transformed;
			while (!output.EoF()) {
				if (!output.IsReadable()) {
					return Unexpected<ConnectionError>("Remote file pipeline failed");
				}
				const StormByte::ByteSize available = output.Available();
				if (available == StormByte::ByteSize{0}) {
					return Unexpected<ConnectionError>("Remote file pipeline stalled");
				}
				const StormByte::ByteSize count = std::min(available, StormByte::ByteSize{64 * 1024});
				BinaryData chunk;
				if (!output.Extract(count, chunk) || chunk.empty()
					|| transformed.size() > max_message_size - chunk.size()) {
					return Unexpected<ConnectionError>("Remote file pipeline output exceeded its limit");
				}
				transformed.append(std::move(chunk));
			}
			if (transformed.empty()) {
				return Unexpected<ConnectionError>("Remote file pipeline produced an empty message");
			}
			return transformed;
		} catch (...) {
			return Unexpected<ConnectionError>("Remote file pipeline processing failed");
		}
	}

	StormByte::Expected<BinaryData, ConnectionError> ReceivePayload(
		Socket::Client& socket, const std::uint16_t timeout_seconds) noexcept {
		BinaryData size_bytes;
		if (auto received = socket.ReceiveInto(sizeof(std::uint64_t), size_bytes, timeout_seconds); !received) {
			return Unexpected<ConnectionError>(received.error()->what());
		}

		const auto expected_size = Serializable<std::uint64_t>::Deserialize(size_bytes);
		if (!expected_size || *expected_size == 0 || *expected_size > max_message_size) {
			return Unexpected<ConnectionError>("Invalid private channel message length");
		}

		BinaryData payload;
		if (auto received = socket.ReceiveInto(static_cast<std::size_t>(*expected_size), payload, timeout_seconds); !received) {
			return Unexpected<ConnectionError>(received.error()->what());
		}
		return payload;
	}

	StormByte::Network::ExpectedVoid SendPayload(Socket::Client& socket,
		const std::span<const std::byte> payload) noexcept {
		if (payload.empty() || payload.size() > max_message_size) {
			return Unexpected<ConnectionError>("Invalid private channel message length");
		}

		const std::uint64_t size = payload.size();
		const BinaryData size_bytes = Serializable<std::uint64_t>(size).Serialize();
		if (auto sent = socket.Send(std::span<const std::byte>{size_bytes}); !sent) {
			return Unexpected<ConnectionError>(sent.error()->what());
		}
		return socket.Send(payload);
	}

	DataPlane::DataPlane(std::shared_ptr<Socket::Client> socket, Buffer::Pipeline input,
		Buffer::Pipeline output, const std::uint16_t timeout_seconds,
		StormByte::Shared<StormByte::Logger::Log> logger, std::string local_address,
		StormByte::Shared<StormByte::System::Device> device, const std::uint16_t port) noexcept:
		m_socket(std::move(socket)), m_input(std::move(input)), m_output(std::move(output)),
		m_timeout_seconds(timeout_seconds), m_logger(std::move(logger)),
		m_local_address(std::move(local_address)), m_device(std::move(device)), m_port(port) {}

	DataPlane::~DataPlane() noexcept {
		Stop();
	}

	bool DataPlane::RegisterToken(const Token& token, std::function<void()> on_failure) noexcept {
		if (!m_socket || m_timeout_seconds < 3 || m_timeout_seconds > 3600
			|| std::ranges::all_of(token, [](const std::byte byte) { return byte == std::byte{0}; })) {
			return false;
		}
		Message attach;
		attach.opcode = Opcode::Attach;
		attach.token = token;
		auto response = Exchange(std::move(attach));
		if (!response || response->status != Status::Ok || !response->data.empty()) {
			return false;
		}
		{
			std::scoped_lock lock(m_token_mutex);
			if (m_failed.load(std::memory_order_acquire)) return false;
			m_token_failures.insert_or_assign(token, std::move(on_failure));
		}
		return true;
	}

	bool DataPlane::ReleaseToken(const Token& token) noexcept {
		Message close;
		close.opcode = Opcode::CloseToken;
		close.token = token;
		auto response = Exchange(std::move(close));
		{
			std::scoped_lock lock(m_token_mutex);
			m_token_failures.erase(token);
		}
		return response && response->status == Status::Ok;
	}

	bool DataPlane::StartHeartbeat() noexcept {
		if (m_timeout_seconds < 3 || m_timeout_seconds > 3600 || m_heartbeat_thread.joinable()) {
			return false;
		}

		try {
			m_heartbeat_thread = std::thread(&DataPlane::RunHeartbeat, this);
			return true;
		} catch (...) {
			return false;
		}
	}

	StormByte::Expected<Message, ConnectionError> DataPlane::Exchange(Message request) noexcept {
		std::scoped_lock lock(m_mutex);
		if (m_failed.load(std::memory_order_acquire) || !m_socket) {
			return Unexpected<ConnectionError>("Remote file channel has failed");
		}

		request.request_id = m_next_request_id.fetch_add(1, std::memory_order_relaxed);
		if (request.request_id == 0) {
			MarkFailed();
			return Unexpected<ConnectionError>("Remote file request sequence exhausted");
		}

		auto response = ExchangeLocked(request);
		if (!response) {
			MarkFailed();
		}
		return response;
	}

	StormByte::Expected<Message, ConnectionError> DataPlane::ExchangeLocked(const Message& request) noexcept {
		try {
			const std::uint64_t request_id = request.request_id;
			const Opcode expected_opcode = request.opcode == Opcode::Ping ? Opcode::Pong : request.opcode;
			BinaryData encoded = Serialize(request);
			auto transformed = Process(m_output, std::move(encoded), m_logger);
			if (!transformed) {
				return Unexpected<ConnectionError>(transformed.error()->what());
			}
			if (auto sent = SendPayload(*m_socket, *transformed); !sent) {
				return Unexpected<ConnectionError>(sent.error()->what());
			}

			auto received = ReceivePayload(*m_socket, m_timeout_seconds);
			if (!received) {
				return Unexpected<ConnectionError>(received.error()->what());
			}

			auto decoded = Process(m_input, std::move(received.value()), m_logger);
			if (!decoded) {
				return Unexpected<ConnectionError>(decoded.error()->what());
			}
			auto response = Deserialize(std::span<const std::byte>{*decoded});
			if (!response || response->request_id != request_id || response->opcode != expected_opcode
				|| response->token != request.token
				|| (!response->data.empty() && (response->opcode == Opcode::Open
					|| response->opcode == Opcode::Close || response->opcode == Opcode::Write
					|| response->opcode == Opcode::Seek || response->opcode == Opcode::Size
					|| response->opcode == Opcode::Flush || response->opcode == Opcode::Truncate
					|| response->opcode == Opcode::CloseToken || response->opcode == Opcode::Attach
					|| response->opcode == Opcode::Pong))) {
				return Unexpected<ConnectionError>("Invalid remote file channel response");
			}
			return response.value();
		} catch (...) {
			return Unexpected<ConnectionError>("Remote file channel processing failed");
		}
	}

	bool DataPlane::Failed() const noexcept {
		return m_failed.load(std::memory_order_acquire);
	}

	void DataPlane::Stop() noexcept {
		{
			std::scoped_lock lock(m_heartbeat_mutex);
			m_stopping = true;
		}
		m_heartbeat_condition.notify_all();
		if (m_socket) {
			m_socket->Disconnect();
		}
		if (m_heartbeat_thread.joinable() && m_heartbeat_thread.get_id() != std::this_thread::get_id()) {
			m_heartbeat_thread.join();
		}
	}

	void DataPlane::RunHeartbeat() noexcept {
		const auto interval = std::chrono::seconds{std::max<std::uint16_t>(1, m_timeout_seconds / 3)};
		while (true) {
			{
				std::unique_lock lock(m_heartbeat_mutex);
				if (m_heartbeat_condition.wait_for(lock, interval, [this]() { return m_stopping; })) {
					return;
				}
			}

			Message ping;
			ping.opcode = Opcode::Ping;
			auto pong = Exchange(std::move(ping));
			if (!pong || pong->status != Status::Ok) {
				MarkFailed();
				return;
			}
		}
	}

	void DataPlane::MarkFailed() noexcept {
		if (m_failed.exchange(true, std::memory_order_acq_rel)) {
			return;
		}

		if (m_socket) {
			m_socket->Disconnect();
		}
		NotifyTokenFailures();
	}

	void DataPlane::NotifyTokenFailures() noexcept {
		std::scoped_lock lock(m_token_mutex);
		for (const auto& [_, on_failure]: m_token_failures) {
			if (on_failure) on_failure();
		}
	}

	const std::string& DataPlane::LocalAddress() const noexcept {
		return m_local_address;
	}

	StormByte::Shared<StormByte::System::Device> DataPlane::Device() const noexcept {
		return m_device;
	}

	std::uint16_t DataPlane::Port() const noexcept {
		return m_port;
	}
}