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

#include <StormByte/network/connection/client.hxx>
#include <StormByte/network/event_loop.hxx>
#include <StormByte/network/remote_file_host.hxx>
#include <StormByte/network/server.hxx>
#include <StormByte/network/session.hxx>
#include <StormByte/network/socket/server.hxx>
#include <StormByte/network/worker_pool.hxx>
#include <StormByte/safe/function.hxx>
#include <StormByte/uuid.hxx>

#include <algorithm>
#include <array>
#include <cctype>
#include <filesystem>
#include <mutex>
#include <string>
#include <utility>

#ifdef UNIX
#include <fcntl.h>
#include <unistd.h>
#else
#include <winsock2.h>
#include <ws2tcpip.h>
#endif

namespace {
	std::uint8_t HexDigit(const char value) noexcept {
		if (value >= '0' && value <= '9')
			return static_cast<std::uint8_t>(value - '0');
		if (value >= 'a' && value <= 'f')
			return static_cast<std::uint8_t>(value - 'a' + 10);
		if (value >= 'A' && value <= 'F')
			return static_cast<std::uint8_t>(value - 'A' + 10);
		return 0;
	}

	StormByte::Network::RemoteFileMount::ChannelToken CreateRemoteFileToken() {
		std::array<char, 64> hexadecimal{};
		std::size_t offset = 0;
		for (int index = 0; index < 2; ++index) {
			const StormByte::Safe::String uuid = StormByte::GenerateUUIDv4();
			const std::string_view text = static_cast<std::string_view>(uuid);
			for (const char value: text) {
				if (value != '-') {
					if (offset >= hexadecimal.size()) {
						return {};
					}
					hexadecimal[offset++] = value;
				}
			}
		}

		StormByte::Network::RemoteFileMount::ChannelToken token{};
		if (offset != hexadecimal.size()) {
			return {};
		}
		for (std::size_t index = 0; index < token.size(); ++index) {
			token[index] = static_cast<std::byte>((HexDigit(hexadecimal[index * 2]) << 4)
				| HexDigit(hexadecimal[index * 2 + 1]));
		}
		return token;
	}
}

using StormByte::Size;
namespace Safe = StormByte::Safe;
using namespace StormByte::Network;

/**
 * @class StormByte::Network::Detail::WakeupChannel
 * @brief Network-owned native handles used to wake the accept loop.
 */
class StormByte::Network::Detail::WakeupChannel final {
	public:
		/**
		 * @brief Initialize invalid native handles.
		 */
		WakeupChannel() noexcept;

		/**
		 * @brief Release both native handles in Network.
		 */
		~WakeupChannel() noexcept;

		/**
		 * @brief Prevent duplicated native handle ownership.
		 * @param other Channel that cannot be copied.
		 */
		WakeupChannel(const WakeupChannel& other) = delete;

		/**
		 * @brief Prevent moving native handles outside their owner.
		 * @param other Channel that cannot be moved.
		 */
		WakeupChannel(WakeupChannel&& other) = delete;

		/**
		 * @brief Prevent duplicated native handle ownership.
		 * @param other Channel that cannot be copied.
		 * @return This channel; operation is deleted.
		 */
		WakeupChannel& operator=(const WakeupChannel& other) = delete;

		/**
		 * @brief Prevent replacing native handle ownership.
		 * @param other Channel that cannot be moved.
		 * @return This channel; operation is deleted.
		 */
		WakeupChannel& operator=(WakeupChannel&& other) = delete;

#ifdef WINDOWS
		Connection::HandlerType read{INVALID_SOCKET};	///< Native wakeup read socket.
		Connection::HandlerType write{INVALID_SOCKET};	///< Native wakeup write socket.
#else
		Connection::HandlerType read{-1};	///< Native wakeup read descriptor.
		Connection::HandlerType write{-1};	///< Native wakeup write descriptor.
#endif
};

StormByte::Network::Detail::WakeupChannel::WakeupChannel() noexcept = default;

StormByte::Network::Detail::WakeupChannel::~WakeupChannel() noexcept {
#ifdef WINDOWS
	if (read != INVALID_SOCKET)
		::closesocket(read);
	if (write != INVALID_SOCKET)
		::closesocket(write);
#else
	if (read >= 0)
		::close(read);
	if (write >= 0)
		::close(write);
#endif
}

Server::Server(DeserializePacketFunction deserialize_packet_function, StormByte::Safe::Shared<Logger::Log> logger):
	Endpoint(std::move(deserialize_packet_function), std::move(logger)),
	m_telemetry(Safe::MakeShared<ServerTelemetry>()) {}

Server::Server(Server&& other) noexcept:
	Endpoint(StopForMove(other)) {
	MoveStoppedState(other);
}

Server::~Server() noexcept {
	Disconnect();
}

Connection::Status Server::Status() const noexcept {
	return m_status.load(Safe::MemoryOrder::Acquire);
}

StormByte::Safe::Shared<ServerTelemetry> Server::Telemetry() const noexcept {
	return m_telemetry;
}

Server& Server::operator=(Server&& other) noexcept {
	if (this != &other) {
		Disconnect();
		StopForMove(other);
		Endpoint::operator=(std::move(other));
		MoveStoppedState(other);
	}

	return *this;
}

Server&& Server::StopForMove(Server& other) noexcept {
	other.Disconnect();
	return std::move(other);
}

void Server::MoveStoppedState(Server& other) noexcept {
	m_protocol = other.m_protocol;
	m_bind_address = std::move(other.m_bind_address);
	m_remote_file_registry = std::move(other.m_remote_file_registry);
	m_telemetry = std::move(other.m_telemetry);
	m_status.store(Connection::Status::Disconnected, Safe::MemoryOrder::Release);
}

bool Server::Connect(const Connection::Protocol& protocol, std::string_view address, const unsigned short& port) {
	try {
		if (m_socket_server || m_accept_thread.joinable() || m_pool) {
			m_logger << Logger::Level::Error << "Server is already running." << std::endl;
			return false;
		}
		if (!m_telemetry)
			m_telemetry = Safe::MakeShared<ServerTelemetry>();
		if (!m_remote_file_registry)
			m_remote_file_registry = Safe::MakeShared<Detail::RemoteFile::MountRegistry>();
		m_protocol = protocol;
		m_bind_address = address;
		m_socket_server = Safe::MakeUnique<Socket::Server>(protocol, m_logger);
		if (!m_socket_server->Listen(address, port)) {
			m_logger << Logger::Level::Error << "Failed to listen on " << std::string_view{address} << ":" << port
					<< " using protocol " << Connection::ProtocolString(protocol) << std::endl;
			m_socket_server.reset();
			return false;
		}

		if (!CreateWakeup()) {
			m_logger << Logger::Level::Error << "Failed to create server wakeup channel" << std::endl;
			m_socket_server->Disconnect();
			m_socket_server.reset();
			return false;
		}

		const unsigned available_workers = Safe::Thread::hardware_concurrency();
		const Size worker_count{available_workers == 0 ? 4u : std::min(available_workers, 8u)};
		m_pool = Safe::MakeUnique<Detail::WorkerPool>(worker_count, Size{64},
			Detail::WorkerCallbackFactory<PacketPointer(const Safe::String&, PacketPointer)>::Make([this](const Safe::String& uuid, PacketPointer packet) {
				if (!m_telemetry)
					return ProcessClientPacket(uuid, std::move(packet));
				m_telemetry->RecordPacketDispatched();
				auto sample = m_telemetry->MeasureHandler();
				PacketPointer response = ProcessClientPacket(uuid, std::move(packet));
				m_telemetry->RecordHandlerResult(sample.Stop(), static_cast<bool>(response));
				return response;
			}),
			Detail::WorkerCallbackFactory<void(Detail::WorkerPool::Completion)>::Make([this](Detail::WorkerPool::Completion completion) {
				CompletionReason reason = CompletionReason::Error;
				switch (completion.reason) {
					case Detail::WorkerPool::CompletionReason::Success:
						reason = CompletionReason::Success;
						break;
					case Detail::WorkerPool::CompletionReason::NullHandler:
						reason = CompletionReason::NullHandler;
						break;
					case Detail::WorkerPool::CompletionReason::Error:
						reason = CompletionReason::Error;
						if (m_telemetry)
							m_telemetry->RecordHandlerError();
						break;
				}

				PostCompletion({ std::move(completion.uuid), std::move(completion.packet), reason });
			}));
		m_status.store(Connection::Status::Connected, Safe::MemoryOrder::Release);
		m_accept_thread = Safe::Thread(Detail::WorkerCallbackFactory<void()>::Make([this]() noexcept { AcceptClients(); }));
		m_logger << Logger::Level::LowLevel << "Server is listening on " << std::string_view{address} << ":" << port
				<< " using protocol " << Connection::ProtocolString(protocol) << std::endl;
		return true;
	} catch (...) {
		Disconnect();
		m_logger << Logger::Level::Error << "Failed to start server" << std::endl;
		return false;
	}
}

void Server::Disconnect() noexcept {
	if (m_pool && m_pool->IsWorkerThread()) {
		PostCommand({ CommandType::Stop, {} });
		return;
	}

	if (m_accept_thread.get_id() == Safe::this_thread::get_id()) {
		m_status.store(Connection::Status::Disconnecting, Safe::MemoryOrder::Release);
		return;
	}

	if (m_socket_server) {
		m_logger << Logger::Level::LowLevel
				<< "Stopping server and disconnecting all clients." << std::endl;
		m_status.store(Connection::Status::Disconnecting, Safe::MemoryOrder::Release);
		SignalWakeup();
	}

	if (m_accept_thread.joinable())
		m_accept_thread.join();
	if (m_pool) {
		m_pool->Stop();
		m_pool->Join();
		m_pool.reset();
	}
	RevokeAllRemoteFiles();
	if (m_socket_server) {
		m_socket_server->Disconnect();
		m_socket_server.reset();
	}
	CloseWakeup();
	{
		std::scoped_lock lock(m_completion_mutex);
		m_completions.clear();
	}
	{
		std::scoped_lock lock(m_command_mutex);
		m_commands.clear();
	}
	m_status.store(Connection::Status::Disconnected, Safe::MemoryOrder::Release);
}

RemoteFileMount Server::MountRemoteFileReader(std::string_view client_uuid,
	std::string_view path, const std::uint16_t maximum_timeout_seconds) noexcept {
	if (client_uuid.empty() || path.empty() || maximum_timeout_seconds < 3 || maximum_timeout_seconds > 3600
		|| !Connection::IsConnected(m_status.load(Safe::MemoryOrder::Acquire))) {
		return RemoteFileMount::Failed();
	}

	try {
		const std::filesystem::path local_path{path};
		std::error_code file_error;
		const bool exists = std::filesystem::exists(local_path, file_error);
		if (file_error) {
			return RemoteFileMount::Failed();
		}
		if (!exists) {
			return RemoteFileMount::Unavailable();
		}
		if (!std::filesystem::is_regular_file(local_path, file_error) || file_error) {
			return RemoteFileMount::Failed();
		}
		std::error_code path_error;
		const std::filesystem::path normalized = std::filesystem::weakly_canonical(local_path, path_error);
		if (path_error) {
			return RemoteFileMount::Failed();
		}
		const std::string normalized_path = normalized.generic_string();
		std::string path_key = normalized_path;
#ifdef WINDOWS
		std::ranges::transform(path_key, path_key.begin(), [](const unsigned char character) {
			return static_cast<char>(std::tolower(character));
		});
#endif
		auto token = CreateRemoteFileToken();
		if (std::ranges::all_of(token, [](const std::byte value) { return value == std::byte{0}; })) {
			return RemoteFileMount::Failed();
		}
		Safe::Shared<Detail::RemoteFile::Host> host;
		bool started_plane = false;
		{
			std::scoped_lock lock(m_remote_file_mutex);
			if (!Connection::IsConnected(m_status.load(Safe::MemoryOrder::Acquire)) || !m_remote_file_registry) {
				return RemoteFileMount::Failed();
			}
			for (auto mounted = m_remote_files.begin(); mounted != m_remote_files.end();) {
				if (!m_remote_file_registry->HasToken(mounted->token))
					mounted = m_remote_files.erase(mounted);
				else
					++mounted;
			}
			if (std::ranges::any_of(m_remote_files, [&path_key](const MountedRemoteFile& mounted) {
				return static_cast<std::string_view>(mounted.path_key) == path_key && mounted.access == RemoteFileMount::Access::Write;
			})) {
				return RemoteFileMount::FileBeingWritten();
			}
			if (m_remote_files.size() >= MAX_REMOTE_FILE_CHANNELS) {
				return RemoteFileMount::Failed();
			}
			const Safe::String uuid{client_uuid};
			auto plane_it = m_remote_planes.find(uuid);
			if (plane_it != m_remote_planes.end()) {
				host = plane_it->second.host;
				if (!host || host->Finished()) return RemoteFileMount::Failed();
			} else {
				const auto connection = m_configurable_connections.find(uuid);
				if (connection == m_configurable_connections.end())
					return RemoteFileMount::Failed();
				Safe::Pair<Buffer::Pipeline, Buffer::Pipeline> pipelines = connection->second.connection->FilePipelines();
				host = Safe::MakeShared<Detail::RemoteFile::Host>(m_protocol, m_bind_address,
					std::move(pipelines.first), std::move(pipelines.second), maximum_timeout_seconds, m_remote_file_registry, m_logger);
				if (!host->Start()) return RemoteFileMount::Failed();
				m_remote_planes.emplace(uuid, Detail::RemotePlaneRegistration{host});
				started_plane = true;
			}
			if (!m_remote_file_registry->AddMount(token, std::string_view{normalized_path}, RemoteFileMount::Access::Read)) {
				return RemoteFileMount::Failed();
			}
			if (!host->RegisterToken(token)) {
				(void)m_remote_file_registry->ReleaseToken(token);
				return RemoteFileMount::Failed();
			}
			try {
				m_remote_files.push_back({ host, Safe::String{std::string_view{path_key}}, RemoteFileMount::Access::Read, token });
			} catch (...) {
				host->UnregisterToken(token);
				throw;
			}
		}
		if (started_plane) SignalWakeup();

		return RemoteFileMount{RemoteFileMount::Status::Authorized, std::move(token), host->Port(),
		host->TimeoutSeconds(), RemoteFileMount::Access::Read};
	} catch (...) {
		return RemoteFileMount::Failed();
	}
}

RemoteFileMount Server::MountRemoteFileWriter(std::string_view client_uuid,
	std::string_view path, const std::uint16_t maximum_timeout_seconds) noexcept {
	if (client_uuid.empty() || path.empty() || maximum_timeout_seconds < 3 || maximum_timeout_seconds > 3600
		|| !Connection::IsConnected(m_status.load(Safe::MemoryOrder::Acquire))) {
		return RemoteFileMount::Failed();
	}

	try {
		const std::filesystem::path local_path{path};
		std::error_code path_error;
		std::filesystem::path normalized = std::filesystem::weakly_canonical(local_path, path_error);
		if (path_error) {
			path_error.clear();
			normalized = std::filesystem::absolute(local_path, path_error).lexically_normal();
		}
		if (path_error) {
			return RemoteFileMount::Failed();
		}
		const std::string normalized_path = normalized.generic_string();
		std::string writer_path = normalized_path;
#ifdef WINDOWS
		std::ranges::transform(writer_path, writer_path.begin(), [](const unsigned char character) {
			return static_cast<char>(std::tolower(character));
		});
#endif

		auto token = CreateRemoteFileToken();
		if (std::ranges::all_of(token, [](const std::byte value) { return value == std::byte{0}; })) {
			return RemoteFileMount::Failed();
		}
		Safe::Shared<Detail::RemoteFile::Host> host;
		bool started_plane = false;
		{
			std::scoped_lock lock(m_remote_file_mutex);
			if (!Connection::IsConnected(m_status.load(Safe::MemoryOrder::Acquire)) || !m_remote_file_registry) {
				return RemoteFileMount::Failed();
			}
			for (auto mounted = m_remote_files.begin(); mounted != m_remote_files.end();) {
				if (!m_remote_file_registry->HasToken(mounted->token))
					mounted = m_remote_files.erase(mounted);
				else
					++mounted;
			}
			const auto conflict = std::ranges::find_if(m_remote_files, [&writer_path](const MountedRemoteFile& mounted) {
				return static_cast<std::string_view>(mounted.path_key) == writer_path;
			});
			if (conflict != m_remote_files.end()) {
				return conflict->access == RemoteFileMount::Access::Read
					? RemoteFileMount::FileBeingRead() : RemoteFileMount::FileBeingWritten();
			}
			if (m_remote_files.size() >= MAX_REMOTE_FILE_CHANNELS) {
				return RemoteFileMount::Failed();
			}
			const Safe::String uuid{client_uuid};
			auto plane_it = m_remote_planes.find(uuid);
			if (plane_it != m_remote_planes.end()) {
				host = plane_it->second.host;
				if (!host || host->Finished()) return RemoteFileMount::Failed();
			} else {
				const auto connection = m_configurable_connections.find(uuid);
				if (connection == m_configurable_connections.end())
					return RemoteFileMount::Failed();
				Safe::Pair<Buffer::Pipeline, Buffer::Pipeline> pipelines = connection->second.connection->FilePipelines();
				host = Safe::MakeShared<Detail::RemoteFile::Host>(m_protocol, m_bind_address,
					std::move(pipelines.first), std::move(pipelines.second), maximum_timeout_seconds, m_remote_file_registry, m_logger);
				if (!host->Start()) return RemoteFileMount::Failed();
				m_remote_planes.emplace(uuid, Detail::RemotePlaneRegistration{host});
				started_plane = true;
			}
			if (!m_remote_file_registry->AddMount(token, std::string_view{normalized_path}, RemoteFileMount::Access::Write)) {
				return RemoteFileMount::Failed();
			}
			if (!host->RegisterToken(token)) {
				(void)m_remote_file_registry->ReleaseToken(token);
				return RemoteFileMount::Failed();
			}
			try {
				m_remote_files.push_back({ host, Safe::String{std::string_view{writer_path}}, RemoteFileMount::Access::Write, token });
			} catch (...) {
				host->UnregisterToken(token);
				throw;
			}
		}
		if (started_plane) SignalWakeup();

		return RemoteFileMount{RemoteFileMount::Status::Authorized, std::move(token), host->Port(),
			host->TimeoutSeconds(), RemoteFileMount::Access::Write};
	} catch (...) {
		return RemoteFileMount::Failed();
	}
}

void Server::RevokeAllRemoteFiles() noexcept {
	Safe::Map<Safe::String, Detail::RemotePlaneRegistration> revoked;
	{
		std::scoped_lock lock(m_remote_file_mutex);
		m_remote_files.clear();
		revoked.swap(m_remote_planes);
	}
	for (const auto& entry: revoked) {
		if (entry.second.host)
			entry.second.host->Stop();
	}
}

bool Server::CreateWakeup() noexcept {
	try {
		m_wakeup = Safe::Unique<WakeupChannel>::MakePointer<WakeupChannel>();
#ifdef UNIX
		int handles[2];
		if (::pipe(handles) != 0) {
			CloseWakeup();
			return false;
		}

		m_wakeup->read = handles[0];
		m_wakeup->write = handles[1];
		const int flags = ::fcntl(m_wakeup->write, F_GETFL, 0);
		if (flags < 0 || ::fcntl(m_wakeup->write, F_SETFL, flags | O_NONBLOCK) < 0) {
			CloseWakeup();
			return false;
		}
		return true;
#else
		m_wakeup->read = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
		m_wakeup->write = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
		if (m_wakeup->read == INVALID_SOCKET || m_wakeup->write == INVALID_SOCKET) {
			CloseWakeup();
			return false;
		}

		sockaddr_in address{};
		address.sin_family = AF_INET;
		address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
		address.sin_port = 0;
		if (::bind(m_wakeup->read, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) == SOCKET_ERROR) {
			CloseWakeup();
			return false;
		}

		int address_size = sizeof(address);
		if (::getsockname(m_wakeup->read, reinterpret_cast<sockaddr*>(&address), &address_size) == SOCKET_ERROR ||
			::connect(m_wakeup->write, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) == SOCKET_ERROR) {
			CloseWakeup();
			return false;
		}

		u_long nonblocking = 1;
		if (::ioctlsocket(m_wakeup->write, FIONBIO, &nonblocking) == SOCKET_ERROR) {
			CloseWakeup();
			return false;
		}
		return true;
#endif
	} catch (...) {
		CloseWakeup();
		return false;
	}
}

void Server::SignalWakeup() noexcept {
	if (!m_wakeup)
		return;
#ifdef WINDOWS
	if (m_wakeup->write == INVALID_SOCKET) {
		return;
	}
#else
	if (m_wakeup->write < 0) {
		return;
	}
#endif
	const char signal = 1;
#ifdef UNIX
	[[maybe_unused]] const ssize_t written = ::write(m_wakeup->write, &signal, sizeof(signal));
#else
	(void)::send(m_wakeup->write, &signal, sizeof(signal), 0);
#endif
}

void Server::CloseWakeup() noexcept {
	m_wakeup.reset();
}

void Server::DisconnectClient(std::string_view uuid) noexcept {
	if (m_accept_thread.get_id() != Safe::this_thread::get_id()) {
		try {
			PostCommand({ CommandType::DisconnectClient, Safe::String{uuid} });
		} catch (...) {
			m_status.store(Connection::Status::Disconnecting, Safe::MemoryOrder::Release);
			SignalWakeup();
		}
		return;
	}

	DisconnectClientOnLoop(uuid);
}

void Server::DisconnectClientOnLoop(std::string_view uuid) noexcept {
	auto session_it = std::ranges::find_if(m_sessions, [uuid](const auto& entry) {
		return static_cast<std::string_view>(entry.first) == uuid;
	});
	if (session_it == m_sessions.end()) {
		return;
	}

	auto session = session_it->second.session;
	const std::string_view session_uuid = session->UUID();
	m_sessions.erase(session_it);
	{
		std::scoped_lock lock(m_remote_file_mutex);
		m_configurable_connections.erase(session->UUID());
	}
	if (m_telemetry)
		m_telemetry->RecordConnectionClosed();
	session->Close();
	if (session->Client() && session->Client()->Socket()) {
		session->Client()->Socket()->Disconnect();
		m_logger << Logger::Level::LowLevel << "Disconnected client: " << session_uuid << std::endl;
	}
	OnClientDisconnected(session_uuid);
}

void Server::DisconnectClientAfterReply(std::string_view uuid) noexcept {
	try {
		PostCommand({CommandType::DisconnectAfterReply, Safe::String{uuid}});
	} catch (...) {
		m_status.store(Connection::Status::Disconnecting, Safe::MemoryOrder::Release);
		SignalWakeup();
	}
}

bool Server::OnClientConnected(std::string_view) noexcept {
	return true;
}

bool Server::ConfigureClientPipelines(std::string_view uuid, Buffer::Pipeline input, Buffer::Pipeline output) noexcept {
	try {
		std::scoped_lock lock(m_remote_file_mutex);
		const Safe::String key{uuid};
		const auto connection = m_configurable_connections.find(key);
		return connection != m_configurable_connections.end()
			&& !m_remote_planes.contains(key)
			&& connection->second.connection->ConfigurePipelines(std::move(input), std::move(output));
	} catch (...) {
		return false;
	}
}

bool Server::AllowIncomingOpcode(std::string_view, Transport::Packet::OpcodeType) const noexcept {
	return true;
}

bool Server::AllowOutgoingOpcode(std::string_view, Transport::Packet::OpcodeType) const noexcept {
	return true;
}

void Server::OnClientDisconnected(std::string_view) noexcept {
}

void Server::AcceptOneClient() noexcept {
	try {
		auto expected_client = m_socket_server->Accept();
		if (!expected_client) {
			if (Connection::IsConnected(m_status.load()))
				m_logger << Logger::Level::LowLevel << expected_client.error()->what() << std::endl;

			return;
		}
#ifdef WINDOWS
		if (m_sessions.size() >= Size{FD_SETSIZE - 2}) {
			m_logger << Logger::Level::Warning << "Windows select client limit reached; closing accepted client" << std::endl;
			expected_client.value()->Disconnect();
			return;
		}
#endif
		const Safe::String client_uuid = expected_client.value()->UUID();
		auto connection = CreateConnection(expected_client.value());
		{
			std::scoped_lock lock(m_remote_file_mutex);
			m_configurable_connections.emplace(client_uuid, Detail::ConfigurableConnection{connection});
		}
		const auto [position, inserted] = m_sessions.emplace(client_uuid,
			Detail::SessionRegistration{Safe::MakeShared<Detail::Session>(client_uuid, std::move(connection))});
		(void)position;
		if (inserted && m_telemetry)
			m_telemetry->RecordConnectionAccepted();
		if (inserted && !OnClientConnected(client_uuid)) {
			DisconnectClientOnLoop(client_uuid);
			return;
		}
		m_logger << Logger::Level::LowLevel << "AcceptClients: accepted client uuid=" << std::string_view{client_uuid} << std::endl;
	} catch (...) {
		m_status.store(Connection::Status::Disconnecting, Safe::MemoryOrder::Release);
	}
}

void Server::PostCompletion(Completion completion) noexcept {
	try {
		std::scoped_lock lock(m_completion_mutex);
		m_completions.push_back(std::move(completion));
	} catch (...) {
		m_status.store(Connection::Status::Disconnecting, Safe::MemoryOrder::Release);
	}

	SignalWakeup();
}

void Server::PostCommand(Command command) noexcept {
	try {
		std::scoped_lock lock(m_command_mutex);
		m_commands.push_back(std::move(command));
	} catch (...) {
		m_status.store(Connection::Status::Disconnecting, Safe::MemoryOrder::Release);
	}

	SignalWakeup();
}

void Server::DrainCommands() noexcept {
	Safe::Deque<Command> commands;
	{
		std::scoped_lock lock(m_command_mutex);
		commands.swap(m_commands);
	}

	for (const auto& command: commands) {
		switch (command.type) {
			case CommandType::DisconnectClient:
				DisconnectClientOnLoop(command.uuid);
				break;
			case CommandType::DisconnectAfterReply: {
				const auto session = m_sessions.find(command.uuid);
				if (session != m_sessions.end())
					session->second.session->CloseAfterReply();
				break;
			}
			case CommandType::DisconnectAll:
				{
					while (!m_sessions.empty()) {
						DisconnectClientOnLoop(m_sessions.begin()->first);
					}
				}

				break;
			case CommandType::Stop:
				m_status.store(Connection::Status::Disconnecting, Safe::MemoryOrder::Release);
				break;
		}
	}
}

void Server::DrainCompletions() noexcept {
	Safe::Deque<Completion> completions;
	{
		std::scoped_lock lock(m_completion_mutex);
		completions.swap(m_completions);
	}

	for (const auto& entry: m_sessions) {
		entry.second.session->SetTaskBlocked(false);
	}

	for (auto& completion: completions) {
		auto session_it = m_sessions.find(completion.uuid);
		if (session_it == m_sessions.end()) {
			continue;
		}

		auto session = session_it->second.session;
		session->SetInFlight(false);
		if (completion.reason != CompletionReason::Success || !completion.packet) {
			DisconnectClient(completion.uuid);
			continue;
		}

		if (!AllowOutgoingOpcode(completion.uuid, completion.packet->Opcode())
			|| !session->QueueResponse(completion.packet, m_logger)) {
			DisconnectClient(completion.uuid);
		}
	}
}

void Server::AcceptClients() noexcept {
	m_logger << Logger::Level::LowLevel << "Started accept event loop" << std::endl;
	try {
		Detail::EventLoop event_loop(*m_socket_server, m_wakeup->read, m_status, m_logger);
		event_loop.Run(
			Detail::WorkerCallbackFactory<void()>::Make([this]() noexcept { AcceptOneClient(); }),
			Detail::WorkerCallbackFactory<Detail::EventLoop::SessionList()>::Make([this]() {
				Detail::EventLoop::SessionList sessions;
				sessions.reserve(m_sessions.size());
				for (const auto& entry: m_sessions)
					sessions.push_back(entry.second.session);
				return sessions;
			}),
			Detail::WorkerCallbackFactory<void(const Safe::Shared<Detail::Session>&, bool, bool)>::Make(
				[this](const Safe::Shared<Detail::Session>& session, bool readable, bool writable) noexcept {
					ProcessSession(session, readable, writable);
				}),
			Detail::WorkerCallbackFactory<Detail::EventLoop::PlaneList()>::Make([this]() {
				Detail::EventLoop::PlaneList planes;
				std::scoped_lock lock(m_remote_file_mutex);
				planes.reserve(m_remote_planes.size());
				for (const auto& entry: m_remote_planes) {
					if (entry.second.host && !entry.second.host->Finished())
						planes.push_back(entry.second.host);
				}
				return planes;
			}),
			Detail::WorkerCallbackFactory<void(const Safe::Shared<Detail::RemoteFile::Host>&, bool, bool)>::Make(
				[this](const Safe::Shared<Detail::RemoteFile::Host>& host, bool readable, bool writable) noexcept {
					ProcessRemotePlane(host, readable, writable);
				}),
			Detail::WorkerCallbackFactory<void()>::Make([this]() noexcept {
				DrainCommands();
				DrainCompletions();
				std::scoped_lock lock(m_remote_file_mutex);
				for (const auto& entry: m_remote_planes) {
					if (entry.second.host)
						entry.second.host->SetTaskBlocked(false);
				}
			}));
	} catch (...) {
		m_logger << Logger::Level::Error << "Failed to run server event loop" << std::endl;
	}
	m_status.store(Connection::Status::Disconnecting, Safe::MemoryOrder::Release);
	if (m_pool)
		m_pool->Stop();
	while (!m_sessions.empty())
		DisconnectClientOnLoop(m_sessions.begin()->first);
	{
		std::scoped_lock lock(m_remote_file_mutex);
		m_configurable_connections.clear();
	}
	m_socket_server->Disconnect();
	m_status.store(Connection::Status::Disconnected, Safe::MemoryOrder::Release);
	m_logger << Logger::Level::LowLevel << "Stopped accept event loop" << std::endl;
}

void Server::ProcessSession(const Safe::Shared<Detail::Session>& session, bool readable, bool writable) noexcept {
	if (!session || session->Closed() || session->InFlight() || !session->Client() || !m_pool) {
		return;
	}

	try {
		if (writable && session->HasOutput()) {
			auto flushed = session->FlushOutput();
			if (!flushed) {
				DisconnectClient(session->UUID());
				return;
			}

			if (!flushed.value())
				return;
		}
		if (session->ClosingAfterReply()) {
			if (!session->HasOutput())
				DisconnectClient(session->UUID());
			return;
		}

		if (!readable || session->InFlight())
			return;

		const Safe::String& client_uuid = session->UUID();
		if (!session->HasPendingFrame()) {
			auto expected_frames = session->ReadReady(session->Client()->InputPipeline(), m_logger);
			if (!expected_frames) {
				DisconnectClient(client_uuid);
				return;
			}

			session->QueueFrames(std::move(expected_frames.value()));
		}

		if (!session->HasPendingFrame())
			return;

		if (!m_pool->HasCapacity()) {
			if (m_telemetry)
				m_telemetry->RecordWorkerQueueBackpressure();
			session->SetTaskBlocked(true);
			return;
		}

		Transport::Frame frame = session->TakeFrame();
		if (!AllowIncomingOpcode(client_uuid, frame.Opcode())
			|| !frame.DecodeInput(session->Client()->InputPipeline(), m_logger)) {
			DisconnectClient(client_uuid);
			return;
		}
		PacketPointer packet = frame.ProcessPacket(m_deserialize_packet_function, m_logger);
		if (!packet) {
			DisconnectClient(client_uuid);
			return;
		}

		session->SetInFlight(true);
		if (!m_pool->Submit({ client_uuid, std::move(packet), {} })) {
			session->SetInFlight(false);
			session->SetTaskBlocked(true);
		}
	} catch (...) {
		DisconnectClient(session->UUID());
	}
}

void Server::ProcessRemotePlane(const Safe::Shared<Detail::RemoteFile::Host>& host,
	const bool readable, const bool writable) noexcept {
	if (!host || host->Finished())
		return;
	if (host->Expired()) {
		host->Stop();
		return;
	}
	if (host->WaitingForAccept()) {
		if (readable)
			(void)host->AcceptReady();
		return;
	}
	if (writable && host->HasOutput()) {
		auto flushed = host->FlushOutput();
		if (!flushed) {
			host->Stop();
			return;
		}
	}
	if (readable && host->CanRead()) {
		if (!host->ReadReady()) {
			host->Stop();
			return;
		}
	}
	if (!host->ReadyForProcessing())
		return;
	if (!m_pool || !m_pool->HasCapacity()) {
		host->SetTaskBlocked(true);
		return;
	}
	Detail::RemoteFile::Message request = host->TakeRequest();
	if (request.request_id == 0)
		return;
	try {
		auto queued_request = Safe::MakeShared<Detail::RemoteFile::Message>(std::move(request));
		auto operation = Detail::WorkerCallbackFactory<void()>::Make([this, host, queued_request]() {
			try {
				const Detail::RemoteFile::Message response = host->ProcessRequest(*queued_request);
				if (!host->QueueResponse(response))
					host->Stop();
			} catch (...) {
				host->Stop();
			}
			SignalWakeup();
		});
		if (!m_pool->Submit({{}, nullptr, std::move(operation)}))
			host->RequeueRequest(std::move(*queued_request));
	} catch (...) {
		host->Stop();
	}
}
