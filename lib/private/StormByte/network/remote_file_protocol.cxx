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

#include <StormByte/network/remote_file_protocol.hxx>

#include <StormByte/buffer/consumer.hxx>
#include <StormByte/buffer/producer.hxx>

#include <algorithm>
#include <chrono>
#include <utility>

namespace StormByte::Network::Detail::RemoteFile {
	using StormByte::Safe::Binary;
	using StormByte::Buffer::Consumer;
	using StormByte::Buffer::ExecutionMode;
	using StormByte::Buffer::Producer;
	using StormByte::Serializable;
	using StormByte::Unexpected;

	Binary Serialize(const Message& message) {
		Binary payload;
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
		message.data.assign(payload.subspan(header_size));
		if ((message.opcode == Opcode::Ping || message.opcode == Opcode::Pong) && !message.data.empty()) {
			return Unexpected<StormByte::DeserializeError>("Heartbeat message has an unexpected body");
		}
		return message;
	}

	StormByte::Expected<Binary, ConnectionError> Process(Buffer::Pipeline& pipeline, Binary input,
		StormByte::Safe::Shared<StormByte::Logger::Log> logger) {
		if (input.empty() || input.size() > max_message_size) {
			return Unexpected<ConnectionError>("Invalid remote file pipeline input size");
		}
		try {
			Producer producer;
			producer.Write(std::move(input));
			producer.Close();
			Consumer output = pipeline.Process(producer.Consumer(), logger, ExecutionMode::Sync);
			Binary transformed;
			while (!output.EoF()) {
				if (!output.IsReadable()) {
					return Unexpected<ConnectionError>("Remote file pipeline failed");
				}
				const StormByte::ByteSize available = output.Available();
				if (available == StormByte::ByteSize{0}) {
					return Unexpected<ConnectionError>("Remote file pipeline stalled");
				}
				const StormByte::ByteSize count = std::min(available, StormByte::ByteSize{64 * 1024});
				Binary chunk;
				if (!output.Extract(count, chunk) || chunk.empty()
					|| transformed.size() > max_message_size - chunk.size()) {
					return Unexpected<ConnectionError>("Remote file pipeline output exceeded its limit");
				}
				transformed.append(std::move(chunk));
			}
			if (!output.IsReadable())
				return Unexpected<ConnectionError>("Remote file pipeline failed");
			if (transformed.empty()) {
				return Unexpected<ConnectionError>("Remote file pipeline produced an empty message");
			}
			return transformed;
		} catch (...) {
			return Unexpected<ConnectionError>("Remote file pipeline processing failed");
		}
	}

	StormByte::Expected<Binary, ConnectionError> ReceivePayload(
		Socket::Client& socket, const std::uint16_t timeout_seconds) noexcept {
		Binary size_bytes;
		if (auto received = socket.ReceiveInto(sizeof(std::uint64_t), size_bytes, timeout_seconds); !received) {
			return Unexpected<ConnectionError>(received.error()->what());
		}

		const auto expected_size = Serializable<std::uint64_t>::Deserialize(size_bytes);
		if (!expected_size || *expected_size == 0 || *expected_size > max_message_size) {
			return Unexpected<ConnectionError>("Invalid private channel message length");
		}

		Binary payload;
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
		const Binary size_bytes = Serializable<std::uint64_t>(size).Serialize();
		if (auto sent = socket.Send(size_bytes.span()); !sent) {
			return Unexpected<ConnectionError>(sent.error()->what());
		}
		return socket.Send(payload);
	}

	DataPlane::DataPlane(Safe::Shared<Socket::Client> socket, Buffer::Pipeline input,
		Buffer::Pipeline output, const std::uint16_t timeout_seconds,
		StormByte::Safe::Shared<StormByte::Logger::Log> logger, Safe::String local_address,
		StormByte::Safe::Shared<StormByte::System::Device> device, const std::uint16_t port) noexcept:
		m_socket(std::move(socket)), m_input(std::move(input)), m_output(std::move(output)),
		m_timeout_seconds(timeout_seconds), m_logger(std::move(logger)),
		m_local_address(std::move(local_address)), m_device(std::move(device)), m_port(port) {}

	DataPlane::~DataPlane() noexcept {
		Stop();
	}

	bool DataPlane::RegisterToken(const Token& token, Safe::Function<void()> on_failure) noexcept {
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
			Safe::UniqueLock lock(m_token_mutex);
			if (m_failed.load(Safe::MemoryOrder::Acquire))
				return false;
			m_token_failures.insert_or_assign(Binary{token},
				Safe::Optional<Safe::Function<void()>>{std::move(on_failure)});
		}
		return true;
	}

	bool DataPlane::ReleaseToken(const Token& token) noexcept {
		Message close;
		close.opcode = Opcode::CloseToken;
		close.token = token;
		auto response = Exchange(std::move(close));
		{
			Safe::UniqueLock lock(m_token_mutex);
			m_token_failures.erase(Binary{token});
		}
		return response && response->status == Status::Ok;
	}

	bool DataPlane::StartHeartbeat() noexcept {
		if (m_timeout_seconds < 3 || m_timeout_seconds > 3600 || m_heartbeat_thread.joinable()) {
			return false;
		}

		try {
			m_heartbeat_thread = Safe::Thread(&DataPlane::RunHeartbeat, this);
			return true;
		} catch (...) {
			return false;
		}
	}

	StormByte::Expected<Message, ConnectionError> DataPlane::Exchange(Message request) noexcept {
		const bool heartbeat = request.opcode == Opcode::Ping;
		const std::size_t slot = heartbeat ? 1 : 0;
		Safe::UniqueLock operation(m_operation_mutex, Safe::DeferLock{});
		if (!heartbeat)
			operation.lock();
		try {
			{
				Safe::UniqueLock sending(m_send_mutex);
				if (Failed() || !m_socket)
					return Unexpected<ConnectionError>("Remote file channel has failed");
				request.request_id = m_next_request_id.fetch_add(std::uint64_t{1}, Safe::MemoryOrder::Relaxed);
				if (request.request_id == 0)
					throw ConnectionError("Remote file request sequence exhausted");
				Binary encoded = Serialize(request);
				auto transformed = Process(m_output, std::move(encoded), m_logger);
				if (!transformed)
					throw ConnectionError(transformed.error()->what());
				{
					Safe::UniqueLock state(m_mutex);
					if (Failed() || m_pending[slot])
						return Unexpected<ConnectionError>("Remote file response slot is unavailable");
					Message metadata;
					metadata.opcode = request.opcode;
					metadata.request_id = request.request_id;
					metadata.token = request.token;
					m_pending[slot].emplace(Pending{std::move(metadata), {}});
					if (!m_receiver_thread.joinable())
						m_receiver_thread = Safe::Thread(&DataPlane::RunReceiver, this);
				}
				if (auto sent = SendPayload(*m_socket, transformed->span()); !sent)
					throw ConnectionError(sent.error()->what());
			}
			m_response_condition.notify_all();
			Safe::UniqueLock state(m_mutex);
			auto completed = [&] { return Failed() || m_pending[slot]->response.has_value(); };
			if (heartbeat || request.opcode == Opcode::Attach) {
				if (!m_response_condition.wait_for(state, std::chrono::seconds{m_timeout_seconds}, completed)) {
					state.unlock();
					MarkFailed();
					return Unexpected<ConnectionError>("Remote file control response timed out");
				}
			}
			else
				m_response_condition.wait(state, completed);
			if (Failed()) {
				m_pending[slot].reset();
				return Unexpected<ConnectionError>("Remote file channel has failed");
			}
			Message response = std::move(*m_pending[slot]->response);
			m_pending[slot].reset();
			return response;
		} catch (...) {
			MarkFailed();
			return Unexpected<ConnectionError>("Remote file channel processing failed");
		}
	}

	void DataPlane::RunReceiver() noexcept {
		try {
			while (true) {
				{
					Safe::UniqueLock state(m_mutex);
					m_response_condition.wait(state, [this] {
						return Failed() || (m_pending[0] && !m_pending[0]->response)
							|| (m_pending[1] && !m_pending[1]->response);
					});
					if (Failed())
						return;
				}
				auto available = m_socket->WaitForData(100000);
				if (!available || *available == Connection::Read::Result::Closed
					|| *available == Connection::Read::Result::ShutdownRequest)
					throw ConnectionError("Remote file peer disconnected");
				if (*available == Connection::Read::Result::Timeout)
					continue;
				auto received = ReceivePayload(*m_socket, m_timeout_seconds);
				if (!received)
					throw ConnectionError(received.error()->what());
				auto decoded = Process(m_input, std::move(*received), m_logger);
				if (!decoded)
					throw ConnectionError(decoded.error()->what());
				auto response = Deserialize(decoded->span());
				if (!response)
					throw ConnectionError("Invalid remote file response");
				{
					Safe::UniqueLock state(m_mutex);
					auto& pending = m_pending[response->opcode == Opcode::Pong ? 1 : 0];
					if (!pending || pending->response || response->request_id != pending->request.request_id
						|| response->token != pending->request.token
						|| response->opcode != (pending->request.opcode == Opcode::Ping ? Opcode::Pong : pending->request.opcode)
						|| (response->opcode != Opcode::Read && !response->data.empty()))
						throw ConnectionError("Unmatched remote file response");
					pending->response.emplace(std::move(*response));
				}
				m_response_condition.notify_all();
			}
		} catch (...) {
			MarkFailed();
		}
	}

	bool DataPlane::Failed() const noexcept {
		return m_failed.load(Safe::MemoryOrder::Acquire);
	}

	void DataPlane::Stop() noexcept {
		{
			Safe::UniqueLock lock(m_heartbeat_mutex);
			m_stopping = true;
		}
		m_heartbeat_condition.notify_all();
		{
			Safe::UniqueLock state(m_mutex);
			m_failed.store(true, Safe::MemoryOrder::Release);
		}
		m_response_condition.notify_all();
		if (m_socket) {
			m_socket->Disconnect();
		}
		if (m_heartbeat_thread.joinable() && m_heartbeat_thread.get_id() != Safe::this_thread::get_id()) {
			m_heartbeat_thread.join();
		}
		if (m_receiver_thread.joinable() && m_receiver_thread.get_id() != Safe::this_thread::get_id())
			m_receiver_thread.join();
	}

	void DataPlane::RunHeartbeat() noexcept {
		const auto interval = std::chrono::seconds{std::max<std::uint16_t>(1, m_timeout_seconds / 3)};
		while (true) {
			{
				Safe::UniqueLock lock(m_heartbeat_mutex);
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
		{
			Safe::UniqueLock state(m_mutex);
			if (m_failed.exchange(true, Safe::MemoryOrder::AcqRel))
				return;
		}
		m_response_condition.notify_all();
		{
			Safe::UniqueLock heartbeat(m_heartbeat_mutex);
			m_stopping = true;
		}
		m_heartbeat_condition.notify_all();

		if (m_socket) {
			m_socket->Disconnect();
		}
		NotifyTokenFailures();
	}

	void DataPlane::NotifyTokenFailures() noexcept {
		Safe::UniqueLock lock(m_token_mutex);
		for (const auto& entry: m_token_failures) {
			if (entry.second)
				(void)entry.second->Call();
		}
	}

	const Safe::String& DataPlane::LocalAddress() const noexcept {
		return m_local_address;
	}

	StormByte::Safe::Shared<StormByte::System::Device> DataPlane::Device() const noexcept {
		return m_device;
	}

	std::uint16_t DataPlane::Port() const noexcept {
		return m_port;
	}
}
