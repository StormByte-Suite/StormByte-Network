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
#include <StormByte/uuid.hxx>
#ifdef UNIX
#include <unistd.h>
#else
#include <winsock2.h>
#include <ws2tcpip.h>
#endif
#include <algorithm>
#include <array>
#include <cctype>
#include <utility>

namespace {
	std::uint8_t HexDigit(const char value) noexcept {
		if (value >= '0' && value <= '9') return static_cast<std::uint8_t>(value - '0');
		if (value >= 'a' && value <= 'f') return static_cast<std::uint8_t>(value - 'a' + 10);
		if (value >= 'A' && value <= 'F') return static_cast<std::uint8_t>(value - 'A' + 10);
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

using namespace StormByte::Network;
Server::Server(DeserializePacketFunction deserialize_packet_function, StormByte::Safe::Shared<Logger::Log> logger):
	Endpoint(std::move(deserialize_packet_function), std::move(logger)),
	m_socket_server(nullptr),
	m_status(Connection::Status::Disconnected),
	m_accept_thread(),
#ifdef WINDOWS
	m_wakeup_read(INVALID_SOCKET),
	m_wakeup_write(INVALID_SOCKET),
	m_telemetry(StormByte::Safe::Heap::MakeShared<ServerTelemetry>())
#else
	m_wakeup_read(-1),
	m_wakeup_write(-1),
	m_telemetry(StormByte::Safe::Heap::MakeShared<ServerTelemetry>())
#endif
{}
Server::Server(Server&& other) noexcept:
	Endpoint(std::move(other)),
	m_socket_server(std::move(other.m_socket_server)),
	m_status(other.m_status.load(std::memory_order_relaxed)),
	m_accept_thread(std::move(other.m_accept_thread)),
	m_wakeup_read(other.m_wakeup_read),
	m_wakeup_write(other.m_wakeup_write),
	m_sessions(std::move(other.m_sessions)),
	m_pool(std::move(other.m_pool)),
	m_protocol(other.m_protocol),
	m_bind_address(std::move(other.m_bind_address)),
	m_telemetry(std::move(other.m_telemetry)) {
	{
		std::scoped_lock lock(m_remote_file_mutex, other.m_remote_file_mutex);
		m_remote_files = std::move(other.m_remote_files);
		m_remote_planes = std::move(other.m_remote_planes);
		m_remote_file_registry = std::move(other.m_remote_file_registry);
	}
#ifdef WINDOWS
	other.m_wakeup_read = INVALID_SOCKET;
	other.m_wakeup_write = INVALID_SOCKET;
#else
	other.m_wakeup_read = -1;
	other.m_wakeup_write = -1;
#endif
	other.m_status.store(Connection::Status::Disconnected, std::memory_order_relaxed);
}

Server::~Server() noexcept {
	Disconnect();
}

Connection::Status Server::Status() const noexcept {
	return m_status.load(std::memory_order_acquire);
}

StormByte::Safe::Shared<ServerTelemetry> Server::Telemetry() const noexcept {
	return m_telemetry;
}

Server& Server::operator=(Server&& other) noexcept {
	if (this != &other) {
		Disconnect();
		Endpoint::operator=(std::move(other));
		m_socket_server = std::move(other.m_socket_server);
		m_status.store(other.m_status.load(std::memory_order_relaxed), std::memory_order_relaxed);
		m_protocol = other.m_protocol;
		m_bind_address = std::move(other.m_bind_address);
		m_telemetry = std::move(other.m_telemetry);
		m_accept_thread = std::move(other.m_accept_thread);
		m_wakeup_read = other.m_wakeup_read;
		m_wakeup_write = other.m_wakeup_write;
		m_sessions = std::move(other.m_sessions);
		m_pool = std::move(other.m_pool);
		{
			std::scoped_lock lock(m_remote_file_mutex, other.m_remote_file_mutex);
			m_remote_files = std::move(other.m_remote_files);
			m_remote_planes = std::move(other.m_remote_planes);
			m_remote_file_registry = std::move(other.m_remote_file_registry);
		}
#ifdef WINDOWS
		other.m_wakeup_read = INVALID_SOCKET;
		other.m_wakeup_write = INVALID_SOCKET;
#else
		other.m_wakeup_read = -1;
		other.m_wakeup_write = -1;
#endif
		other.m_status.store(Connection::Status::Disconnected, std::memory_order_relaxed);
	}

	return *this;
}

bool Server::Connect(const Connection::Protocol& protocol, std::string_view address, const unsigned short& port) {
	if (!m_telemetry) m_telemetry = StormByte::Safe::Heap::MakeShared<ServerTelemetry>();
	if (m_socket_server) {
		m_logger << Logger::Level::Error << "Server is already running." << std::endl;
		return false;
	}

	try {
		if (!m_remote_file_registry) {
			m_remote_file_registry = std::make_shared<Detail::RemoteFile::MountRegistry>();
		}
		m_protocol = protocol;
		m_bind_address = address;
		m_socket_server = std::make_unique<Socket::Server>(protocol, m_logger);
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

		std::size_t worker_count = std::thread::hardware_concurrency();
		worker_count = worker_count == 0 ? 4 : std::min(worker_count, static_cast<std::size_t>(8));
		m_pool = std::make_unique<Detail::WorkerPool>(worker_count, 64,
			[this](std::string_view uuid, PacketPointer packet) {
				if (!m_telemetry) return ProcessClientPacket(uuid, std::move(packet));
				m_telemetry->RecordPacketDispatched();
				auto sample = m_telemetry->MeasureHandler();
				PacketPointer response = ProcessClientPacket(uuid, std::move(packet));
				m_telemetry->RecordHandlerResult(sample.Stop(), static_cast<bool>(response));
				return response;
			},
			[this](Detail::WorkerPool::Completion completion) {
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
						if (m_telemetry) m_telemetry->RecordHandlerError();
						break;
				}

				PostCompletion({ std::move(completion.uuid), std::move(completion.packet), reason });
			});
		m_status.store(Connection::Status::Connected);
		m_accept_thread = std::thread(&Server::AcceptClients, this);
		m_logger << Logger::Level::LowLevel << "Server is listening on " << std::string_view{address} << ":" << port
				<< " using protocol " << Connection::ProtocolString(protocol) << std::endl;
		return true;
	} catch (const std::bad_alloc& bd) {
		m_logger << Logger::Level::Error << "Failed to allocate memory for server socket: " << bd.what() << std::endl;
		return false;
	}
}

void Server::Disconnect() noexcept {
	if (!m_socket_server && !m_accept_thread.joinable() && !m_pool) {
		RevokeAllRemoteFiles();
		return;
	}

	if (m_pool && m_pool->IsWorkerThread()) {
		PostCommand({ CommandType::Stop, {} });
		return;
	}

	if (m_socket_server) {
		m_logger << Logger::Level::LowLevel
				<< "Stopping server and disconnecting all clients." << std::endl;
		m_status.store(Connection::Status::Disconnecting, std::memory_order_release);
		SignalWakeup();
	}

	// The event loop owns all sessions. It performs cleanup after observing stop.
	if (m_accept_thread.joinable() && m_accept_thread.get_id() != std::this_thread::get_id()) {
		m_accept_thread.join();
	}

	if (m_pool) {
		const bool from_worker = m_pool->IsWorkerThread();
		m_pool->Stop();
		if (!from_worker) {
			m_pool->Join();
		}
	}
	RevokeAllRemoteFiles();
}

RemoteFileMount Server::MountRemoteFileReader(std::string_view client_uuid,
	const std::filesystem::path& path, const std::uint16_t maximum_timeout_seconds) noexcept {
	if (client_uuid.empty() || path.empty() || maximum_timeout_seconds < 3 || maximum_timeout_seconds > 3600
		|| !Connection::IsConnected(m_status.load(std::memory_order_acquire))) {
		return RemoteFileMount::Failed();
	}

	try {
		std::error_code file_error;
		const bool exists = std::filesystem::exists(path, file_error);
		if (file_error) {
			return RemoteFileMount::Failed();
		}
		if (!exists) {
			return RemoteFileMount::Unavailable();
		}
		if (!std::filesystem::is_regular_file(path, file_error) || file_error) {
			return RemoteFileMount::Failed();
		}
		std::error_code path_error;
		const std::filesystem::path normalized = std::filesystem::weakly_canonical(path, path_error);
		if (path_error) {
			return RemoteFileMount::Failed();
		}
		std::string path_key = normalized.generic_string();
#ifdef WINDOWS
		std::ranges::transform(path_key, path_key.begin(), [](const unsigned char character) {
			return static_cast<char>(std::tolower(character));
		});
#endif
		auto token = CreateRemoteFileToken();
		if (std::ranges::all_of(token, [](const std::byte value) { return value == std::byte{0}; })) {
			return RemoteFileMount::Failed();
		}
		std::shared_ptr<Detail::RemoteFile::Host> host;
		bool started_plane = false;
		{
			std::scoped_lock lock(m_remote_file_mutex);
			if (!Connection::IsConnected(m_status.load(std::memory_order_acquire)) || !m_remote_file_registry) {
				return RemoteFileMount::Failed();
			}
			std::erase_if(m_remote_files, [this](const MountedRemoteFile& mounted) {
				return !m_remote_file_registry->HasToken(mounted.token);
			});
			if (std::ranges::any_of(m_remote_files, [&path_key](const MountedRemoteFile& mounted) {
				return mounted.path_key == path_key && mounted.access == RemoteFileMount::Access::Write;
			})) {
				return RemoteFileMount::FileBeingWritten();
			}
			if (m_remote_files.size() >= MAX_REMOTE_FILE_CHANNELS) {
				return RemoteFileMount::Failed();
			}
			auto plane_it = m_remote_planes.find(std::string{client_uuid});
			if (plane_it != m_remote_planes.end()) {
				host = plane_it->second;
				if (!host || host->Finished()) return RemoteFileMount::Failed();
			} else {
				host = std::make_shared<Detail::RemoteFile::Host>(m_protocol, m_bind_address,
					InputPipeline(), OutputPipeline(), maximum_timeout_seconds, m_remote_file_registry, m_logger);
				if (!host->Start()) return RemoteFileMount::Failed();
				m_remote_planes.emplace(std::string{client_uuid}, host);
				started_plane = true;
			}
			if (!m_remote_file_registry->AddMount(token, normalized, RemoteFileMount::Access::Read)) {
				return RemoteFileMount::Failed();
			}
			if (!host->RegisterToken(token)) {
				(void)m_remote_file_registry->ReleaseToken(token);
				return RemoteFileMount::Failed();
			}
			try {
				m_remote_files.push_back({ host, path_key, RemoteFileMount::Access::Read, token });
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
	const std::filesystem::path& path, const std::uint16_t maximum_timeout_seconds) noexcept {
	if (client_uuid.empty() || path.empty() || maximum_timeout_seconds < 3 || maximum_timeout_seconds > 3600
		|| !Connection::IsConnected(m_status.load(std::memory_order_acquire))) {
		return RemoteFileMount::Failed();
	}

	try {
		std::error_code path_error;
		std::filesystem::path normalized = std::filesystem::weakly_canonical(path, path_error);
		if (path_error) {
			path_error.clear();
			normalized = std::filesystem::absolute(path, path_error).lexically_normal();
		}
		if (path_error) {
			return RemoteFileMount::Failed();
		}
		std::string writer_path = normalized.generic_string();
#ifdef WINDOWS
		std::ranges::transform(writer_path, writer_path.begin(), [](const unsigned char character) {
			return static_cast<char>(std::tolower(character));
		});
#endif

		auto token = CreateRemoteFileToken();
		if (std::ranges::all_of(token, [](const std::byte value) { return value == std::byte{0}; })) {
			return RemoteFileMount::Failed();
		}
		std::shared_ptr<Detail::RemoteFile::Host> host;
		bool started_plane = false;
		{
			std::scoped_lock lock(m_remote_file_mutex);
			if (!Connection::IsConnected(m_status.load(std::memory_order_acquire)) || !m_remote_file_registry) {
				return RemoteFileMount::Failed();
			}
			std::erase_if(m_remote_files, [this](const MountedRemoteFile& mounted) {
				return !m_remote_file_registry->HasToken(mounted.token);
			});
			const auto conflict = std::ranges::find_if(m_remote_files, [&writer_path](const MountedRemoteFile& mounted) {
				return mounted.path_key == writer_path;
			});
			if (conflict != m_remote_files.end()) {
				return conflict->access == RemoteFileMount::Access::Read
					? RemoteFileMount::FileBeingRead() : RemoteFileMount::FileBeingWritten();
			}
			if (m_remote_files.size() >= MAX_REMOTE_FILE_CHANNELS) {
				return RemoteFileMount::Failed();
			}
			auto plane_it = m_remote_planes.find(std::string{client_uuid});
			if (plane_it != m_remote_planes.end()) {
				host = plane_it->second;
				if (!host || host->Finished()) return RemoteFileMount::Failed();
			} else {
				host = std::make_shared<Detail::RemoteFile::Host>(m_protocol, m_bind_address,
					InputPipeline(), OutputPipeline(), maximum_timeout_seconds, m_remote_file_registry, m_logger);
				if (!host->Start()) return RemoteFileMount::Failed();
				m_remote_planes.emplace(std::string{client_uuid}, host);
				started_plane = true;
			}
			if (!m_remote_file_registry->AddMount(token, normalized, RemoteFileMount::Access::Write)) {
				return RemoteFileMount::Failed();
			}
			if (!host->RegisterToken(token)) {
				(void)m_remote_file_registry->ReleaseToken(token);
				return RemoteFileMount::Failed();
			}
			try {
				m_remote_files.push_back({ host, writer_path, RemoteFileMount::Access::Write, token });
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
	std::unordered_map<std::string, std::shared_ptr<Detail::RemoteFile::Host>> revoked;
	{
		std::scoped_lock lock(m_remote_file_mutex);
		m_remote_files.clear();
		revoked.swap(m_remote_planes);
	}
	for (const auto& [_, host]: revoked) {
		if (host) host->Stop();
	}
}

bool Server::CreateWakeup() noexcept {
#ifdef UNIX
	int handles[2];
	if (::pipe(handles) != 0) {
		return false;
	}

	m_wakeup_read = handles[0];
	m_wakeup_write = handles[1];
	return true;
#else
	m_wakeup_read = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	m_wakeup_write = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	if (m_wakeup_read == INVALID_SOCKET || m_wakeup_write == INVALID_SOCKET) {
		CloseWakeup();
		return false;
	}

	sockaddr_in address{};
	address.sin_family = AF_INET;
	address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	address.sin_port = 0;
	if (::bind(m_wakeup_read, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) == SOCKET_ERROR) {
		CloseWakeup();
		return false;
	}

	int address_size = sizeof(address);
	if (::getsockname(m_wakeup_read, reinterpret_cast<sockaddr*>(&address), &address_size) == SOCKET_ERROR ||
		::connect(m_wakeup_write, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) == SOCKET_ERROR) {
		CloseWakeup();
		return false;
	}

	return true;
#endif
}

void Server::SignalWakeup() noexcept {
#ifdef WINDOWS
	if (m_wakeup_write == INVALID_SOCKET) {
		return;
	}
#else
	if (m_wakeup_write < 0) {
		return;
	}
#endif
	const char signal = 1;
#ifdef UNIX
	[[maybe_unused]] const ssize_t written = ::write(m_wakeup_write, &signal, sizeof(signal));
#else
	(void)::send(m_wakeup_write, &signal, sizeof(signal), 0);
#endif
}

void Server::CloseWakeup() noexcept {
#ifdef WINDOWS
	if (m_wakeup_read != INVALID_SOCKET) {
		closesocket(m_wakeup_read);
		m_wakeup_read = INVALID_SOCKET;
	}

	if (m_wakeup_write != INVALID_SOCKET) {
		closesocket(m_wakeup_write);
		m_wakeup_write = INVALID_SOCKET;
	}
#else
	if (m_wakeup_read >= 0) {
		close(m_wakeup_read);
		m_wakeup_read = -1;
	}

	if (m_wakeup_write >= 0) {
		close(m_wakeup_write);
		m_wakeup_write = -1;
	}
#endif
}

void Server::DisconnectClient(std::string_view uuid) noexcept {
	if (m_accept_thread.get_id() != std::this_thread::get_id()) {
		PostCommand({ CommandType::DisconnectClient, std::string{uuid} });
		return;
	}

	DisconnectClientOnLoop(uuid);
}

void Server::DisconnectClientOnLoop(std::string_view uuid) noexcept {
	auto session_it = m_sessions.find(std::string{uuid});
	if (session_it == m_sessions.end()) {
		return;
	}

	auto session = session_it->second;
	m_sessions.erase(session_it);
	if (m_telemetry) m_telemetry->RecordConnectionClosed();
	session->Close();
	if (session->Client() && session->Client()->Socket()) {
		session->Client()->Socket()->Disconnect();
				m_logger << Logger::Level::LowLevel << "Disconnected client: " << uuid << std::endl;
	}
}

void Server::AcceptOneClient() noexcept {
	auto expected_client = m_socket_server->Accept();
	if (!expected_client) {
		if (Connection::IsConnected(m_status.load())) {
			m_logger << Logger::Level::LowLevel << expected_client.error()->what() << std::endl;
		}

		return;
	}
#ifdef WINDOWS
	if (m_sessions.size() >= static_cast<std::size_t>(FD_SETSIZE - 2)) {
		m_logger << Logger::Level::Warning << "Windows select client limit reached; closing accepted client" << std::endl;
		expected_client.value()->Disconnect();
		return;
	}
#endif
	const std::string client_uuid = expected_client.value()->UUID();
	auto connection = CreateConnection(expected_client.value());
	const auto [_, inserted] = m_sessions.emplace(client_uuid,
		std::make_shared<Detail::Session>(client_uuid, std::move(connection)));
	if (inserted && m_telemetry) m_telemetry->RecordConnectionAccepted();
	m_logger << Logger::Level::LowLevel << "AcceptClients: accepted client uuid=" << std::string_view{client_uuid} << std::endl;
}

void Server::PostCompletion(Completion completion) noexcept {
	{
		std::scoped_lock lock(m_completion_mutex);
		m_completions.push_back(std::move(completion));
	}

	SignalWakeup();
}

void Server::PostCommand(Command command) noexcept {
	{
		std::scoped_lock lock(m_command_mutex);
		m_commands.push_back(std::move(command));
	}

	SignalWakeup();
}

void Server::DrainCommands() noexcept {
	std::deque<Command> commands;
	{
		std::scoped_lock lock(m_command_mutex);
		commands.swap(m_commands);
	}

	for (const auto& command: commands) {
		switch (command.type) {
			case CommandType::DisconnectClient:
				DisconnectClientOnLoop(command.uuid);
				break;
			case CommandType::DisconnectAll:
				{
					std::vector<std::string> uuids;
					uuids.reserve(m_sessions.size());
					for (const auto& [uuid, _]: m_sessions) {
						uuids.push_back(uuid);
					}

					for (const auto& uuid: uuids) {
						DisconnectClientOnLoop(uuid);
					}
				}

				break;
			case CommandType::Stop:
				m_status.store(Connection::Status::Disconnecting, std::memory_order_release);
				break;
		}
	}
}

void Server::DrainCompletions() noexcept {
	std::deque<Completion> completions;
	{
		std::scoped_lock lock(m_completion_mutex);
		completions.swap(m_completions);
	}

	for (auto& [_, session]: m_sessions) {
		session->SetTaskBlocked(false);
	}

	for (auto& completion: completions) {
		auto session_it = m_sessions.find(completion.uuid);
		if (session_it == m_sessions.end()) {
			continue;
		}

		auto session = session_it->second;
		session->SetInFlight(false);
		if (completion.reason != CompletionReason::Success || !completion.packet) {
			DisconnectClient(completion.uuid);
			continue;
		}

		if (!session->QueueResponse(completion.packet, m_logger)) {
			DisconnectClient(completion.uuid);
		}
	}
}

void Server::AcceptClients() noexcept {
	m_logger << Logger::Level::LowLevel << "Started accept event loop" << std::endl;
	Detail::EventLoop event_loop(*m_socket_server, m_wakeup_read, m_status, m_logger);
	event_loop.Run(
		[this]() noexcept { AcceptOneClient(); },
		[this]() {
			Detail::EventLoop::SessionList sessions;
			sessions.reserve(m_sessions.size());
			for (const auto& [_, session]: m_sessions) {
				sessions.push_back(session);
			}

			return sessions;
		},
		[this](const std::shared_ptr<Detail::Session>& session, bool readable, bool writable) noexcept {
			if (!session) {
				return;
			}

			ProcessSession(session, readable, writable);
		},
		[this]() {
			Detail::EventLoop::PlaneList planes;
			std::scoped_lock lock(m_remote_file_mutex);
			planes.reserve(m_remote_planes.size());
			for (const auto& [_, host]: m_remote_planes) {
				if (host && !host->Finished()) planes.push_back(host);
			}
			return planes;
		},
		[this](const std::shared_ptr<Detail::RemoteFile::Host>& host, bool readable, bool writable) noexcept {
			ProcessRemotePlane(host, readable, writable);
		},
		[this]() noexcept {
			DrainCommands();
			DrainCompletions();
			std::scoped_lock lock(m_remote_file_mutex);
			for (const auto& [_, host]: m_remote_planes) {
				if (host) host->SetTaskBlocked(false);
			}
		}

	);
	for (auto& [uuid, session]: m_sessions) {
		(void)uuid;
		if (m_telemetry) m_telemetry->RecordConnectionClosed();
		session->Close();
		if (session->Client() && session->Client()->Socket()) {
			session->Client()->Socket()->Disconnect();
		}
	}

	m_sessions.clear();
	m_socket_server->Disconnect();
	m_socket_server.reset();
	CloseWakeup();
	m_status.store(Connection::Status::Disconnected, std::memory_order_release);
	m_logger << Logger::Level::LowLevel << "Stopped accept event loop" << std::endl;
}

void Server::ProcessSession(const std::shared_ptr<Detail::Session>& session, bool readable, bool writable) noexcept {
	if (!session || session->Closed() || session->InFlight() || !session->Client() || !m_pool) {
		return;
	}

	if (writable && session->HasOutput()) {
		auto flushed = session->FlushOutput();
		if (!flushed) {
			DisconnectClient(session->UUID());
			return;
		}

		if (!flushed.value()) {
			return;
		}
	}

	if (!readable || session->InFlight()) {
		return;
	}

	const std::string client_uuid = session->UUID();
	if (!session->HasPendingFrame()) {
		auto expected_frames = session->ReadReady(session->Client()->InputPipeline(), m_logger);
		if (!expected_frames) {
			DisconnectClient(client_uuid);
			return;
		}

		session->QueueFrames(std::move(expected_frames.value()));
	}

	if (!session->HasPendingFrame()) {
		return;
	}

	if (!m_pool->HasCapacity()) {
		if (m_telemetry) m_telemetry->RecordWorkerQueueBackpressure();
		session->SetTaskBlocked(true);
		return;
	}

	Transport::Frame frame = session->TakeFrame();
	PacketPointer packet = frame.ProcessPacket(m_deserialize_packet_function, m_logger);
	if (!packet) {
		DisconnectClient(client_uuid);
		return;
	}

	session->SetInFlight(true);
	if (!m_pool->Submit({ client_uuid, std::move(packet) })) {
		session->SetInFlight(false);
		session->SetTaskBlocked(true);
	}
}

void Server::ProcessRemotePlane(const std::shared_ptr<Detail::RemoteFile::Host>& host,
	const bool readable, const bool writable) noexcept {
	if (!host || host->Finished()) return;
	if (host->Expired()) {
		host->Stop();
		return;
	}
	if (host->WaitingForAccept()) {
		if (readable) (void)host->AcceptReady();
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
	if (!host->ReadyForProcessing()) return;
	if (!m_pool || !m_pool->HasCapacity()) {
		host->SetTaskBlocked(true);
		return;
	}
	Detail::RemoteFile::Message request = host->TakeRequest();
	if (request.request_id == 0) return;
	auto queued_request = std::make_shared<Detail::RemoteFile::Message>(std::move(request));
	if (!m_pool->Submit({{}, nullptr, [this, host, queued_request]() {
		const Detail::RemoteFile::Message response = host->ProcessRequest(*queued_request);
		if (!host->QueueResponse(response)) host->Stop();
		SignalWakeup();
	}})) {
		host->RequeueRequest(std::move(*queued_request));
	}
}
