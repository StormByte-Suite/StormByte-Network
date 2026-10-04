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
#include <atomic>
#include <cctype>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

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

class Server::Implementation final {
	public:
		/**
		 * @brief Construct an empty event-loop engine with its telemetry.
		 */
		Implementation() = default;

	private:
		friend class Server;

		/**
		 * @brief Listening socket owned by this server engine.
		 */
		std::unique_ptr<Socket::Server> socket_server;
		/**
		 * @brief Current listener lifecycle state.
		 */
		std::atomic<Connection::Status> status{Connection::Status::Disconnected};
		/**
		 * @brief Thread running the accept and event loop.
		 */
		std::thread accept_thread;
#ifdef WINDOWS
		/**
		 * @brief Read end of the event-loop wakeup socket.
		 */
		Connection::HandlerType wakeup_read{INVALID_SOCKET};
		/**
		 * @brief Write end of the event-loop wakeup socket.
		 */
		Connection::HandlerType wakeup_write{INVALID_SOCKET};
#else
		/**
		 * @brief Read end of the event-loop wakeup pipe.
		 */
		Connection::HandlerType wakeup_read{-1};
		/**
		 * @brief Write end of the event-loop wakeup pipe.
		 */
		Connection::HandlerType wakeup_write{-1};
#endif
		/**
		 * @brief Active parser sessions keyed by client UUID.
		 */
		std::unordered_map<std::string, std::shared_ptr<Detail::Session>> sessions;
		/**
		 * @brief Bounded pool that executes application packet handlers.
		 */
		std::unique_ptr<Detail::WorkerPool> pool;
		/**
		 * @brief Responses posted by worker threads for the event loop.
		 */
		std::deque<Server::Completion> completions;
		/**
		 * @brief Address family used by the listener and private file planes.
		 */
		Connection::Protocol protocol{Connection::Protocol::IPv4};
		/**
		 * @brief Bind address retained for private file planes.
		 */
		StormByte::Safe::String bind_address;
		/**
		 * @brief Authorized remote-file mounts.
		 */
		std::vector<Server::MountedRemoteFile> remote_files;
		/**
		 * @brief Private data plane shared by each connected peer.
		 */
		std::unordered_map<std::string, std::shared_ptr<Detail::RemoteFile::Host>> remote_planes;
		/**
		 * @brief Registry for path reservations and remote-file capability tokens.
		 */
		std::shared_ptr<Detail::RemoteFile::MountRegistry> remote_file_registry;
		/**
		 * @brief Protects remote-file registrations and peer planes.
		 */
		std::mutex remote_file_mutex;
		/**
		 * @brief Protects worker completion queue access.
		 */
		std::mutex completion_mutex;
		/**
		 * @brief Commands posted by workers and application callbacks.
		 */
		std::deque<Server::Command> commands;
		/**
		 * @brief Protects event-loop command queue access.
		 */
		std::mutex command_mutex;
		/**
		 * @brief Aggregate counters and handler latency for this server.
		 */
		StormByte::Safe::Shared<ServerTelemetry> telemetry{StormByte::Safe::Heap::MakeShared<ServerTelemetry>()};
};

Server::Server(DeserializePacketFunction deserialize_packet_function, StormByte::Safe::Shared<Logger::Log> logger):
	Endpoint(std::move(deserialize_packet_function), std::move(logger)),
	m_engine(StormByte::Safe::Unique<Implementation>::MakePointer<Implementation>()) {}
Server::Server(Server&& other) noexcept:
	Endpoint(std::move(other)), m_engine(std::move(other.m_engine)) {}

Server::~Server() noexcept {
	Disconnect();
}

Connection::Status Server::Status() const noexcept {
	return m_engine ? m_engine->status.load(std::memory_order_acquire) : Connection::Status::Disconnected;
}

StormByte::Safe::Shared<ServerTelemetry> Server::Telemetry() const noexcept {
	return m_engine ? m_engine->telemetry : StormByte::Safe::Shared<ServerTelemetry>{};
}

Server& Server::operator=(Server&& other) noexcept {
	if (this != &other) {
		Disconnect();
		Endpoint::operator=(std::move(other));
		m_engine = std::move(other.m_engine);
	}

	return *this;
}

bool Server::Connect(const Connection::Protocol& protocol, std::string_view address, const unsigned short& port) {
	try {
		if (!m_engine) {
			m_engine = StormByte::Safe::Unique<Implementation>::MakePointer<Implementation>();
		}
		if (!m_engine->telemetry) m_engine->telemetry = StormByte::Safe::Heap::MakeShared<ServerTelemetry>();
		if (m_engine->socket_server) {
			m_logger << Logger::Level::Error << "Server is already running." << std::endl;
			return false;
		}

		if (!m_engine->remote_file_registry) {
			m_engine->remote_file_registry = std::make_shared<Detail::RemoteFile::MountRegistry>();
		}
		m_engine->protocol = protocol;
		m_engine->bind_address = address;
		m_engine->socket_server = std::make_unique<Socket::Server>(protocol, m_logger);
		if (!m_engine->socket_server->Listen(address, port)) {
			m_logger << Logger::Level::Error << "Failed to listen on " << std::string_view{address} << ":" << port
					<< " using protocol " << Connection::ProtocolString(protocol) << std::endl;
			m_engine->socket_server.reset();
			return false;
		}

		if (!CreateWakeup()) {
			m_logger << Logger::Level::Error << "Failed to create server wakeup channel" << std::endl;
			m_engine->socket_server->Disconnect();
			m_engine->socket_server.reset();
			return false;
		}

		std::size_t worker_count = std::thread::hardware_concurrency();
		worker_count = worker_count == 0 ? 4 : std::min(worker_count, static_cast<std::size_t>(8));
		m_engine->pool = std::make_unique<Detail::WorkerPool>(worker_count, 64,
			[this](std::string_view uuid, PacketPointer packet) {
				if (!m_engine->telemetry) return ProcessClientPacket(uuid, std::move(packet));
				m_engine->telemetry->RecordPacketDispatched();
				auto sample = m_engine->telemetry->MeasureHandler();
				PacketPointer response = ProcessClientPacket(uuid, std::move(packet));
				m_engine->telemetry->RecordHandlerResult(sample.Stop(), static_cast<bool>(response));
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
						if (m_engine->telemetry) m_engine->telemetry->RecordHandlerError();
						break;
				}

				PostCompletion({ std::move(completion.uuid), std::move(completion.packet), reason });
			});
		m_engine->status.store(Connection::Status::Connected);
		m_engine->accept_thread = std::thread(&Server::AcceptClients, this);
		m_logger << Logger::Level::LowLevel << "Server is listening on " << std::string_view{address} << ":" << port
				<< " using protocol " << Connection::ProtocolString(protocol) << std::endl;
		return true;
	} catch (const std::bad_alloc& bd) {
		m_logger << Logger::Level::Error << "Failed to allocate memory for server socket: " << bd.what() << std::endl;
		return false;
	}
}

void Server::Disconnect() noexcept {
	if (!m_engine) return;

	if (!m_engine->socket_server && !m_engine->accept_thread.joinable() && !m_engine->pool) {
		RevokeAllRemoteFiles();
		return;
	}

	if (m_engine->pool && m_engine->pool->IsWorkerThread()) {
		PostCommand({ CommandType::Stop, {} });
		return;
	}

	if (m_engine->socket_server) {
		m_logger << Logger::Level::LowLevel
				<< "Stopping server and disconnecting all clients." << std::endl;
		m_engine->status.store(Connection::Status::Disconnecting, std::memory_order_release);
		SignalWakeup();
	}

	// The event loop owns all sessions. It performs cleanup after observing stop.
	if (m_engine->accept_thread.joinable() && m_engine->accept_thread.get_id() != std::this_thread::get_id()) {
		m_engine->accept_thread.join();
	}

	if (m_engine->pool) {
		const bool from_worker = m_engine->pool->IsWorkerThread();
		m_engine->pool->Stop();
		if (!from_worker) {
			m_engine->pool->Join();
		}
	}
	RevokeAllRemoteFiles();
}

RemoteFileMount Server::MountRemoteFileReader(std::string_view client_uuid,
	const std::filesystem::path& path, const std::uint16_t maximum_timeout_seconds) noexcept {
	if (!m_engine || client_uuid.empty() || path.empty() || maximum_timeout_seconds < 3 || maximum_timeout_seconds > 3600
		|| !Connection::IsConnected(m_engine->status.load(std::memory_order_acquire))) {
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
			std::scoped_lock lock(m_engine->remote_file_mutex);
			if (!Connection::IsConnected(m_engine->status.load(std::memory_order_acquire)) || !m_engine->remote_file_registry) {
				return RemoteFileMount::Failed();
			}
			std::erase_if(m_engine->remote_files, [this](const MountedRemoteFile& mounted) {
				return !m_engine->remote_file_registry->HasToken(mounted.token);
			});
			if (std::ranges::any_of(m_engine->remote_files, [&path_key](const MountedRemoteFile& mounted) {
				return mounted.path_key == path_key && mounted.access == RemoteFileMount::Access::Write;
			})) {
				return RemoteFileMount::FileBeingWritten();
			}
			if (m_engine->remote_files.size() >= MAX_REMOTE_FILE_CHANNELS) {
				return RemoteFileMount::Failed();
			}
			auto plane_it = m_engine->remote_planes.find(std::string{client_uuid});
			if (plane_it != m_engine->remote_planes.end()) {
				host = plane_it->second;
				if (!host || host->Finished()) return RemoteFileMount::Failed();
			} else {
				host = std::make_shared<Detail::RemoteFile::Host>(m_engine->protocol,
					std::string{static_cast<std::string_view>(m_engine->bind_address)},
					InputPipeline(), OutputPipeline(), maximum_timeout_seconds, m_engine->remote_file_registry, m_logger);
				if (!host->Start()) return RemoteFileMount::Failed();
				m_engine->remote_planes.emplace(std::string{client_uuid}, host);
				started_plane = true;
			}
			if (!m_engine->remote_file_registry->AddMount(token, normalized, RemoteFileMount::Access::Read)) {
				return RemoteFileMount::Failed();
			}
			if (!host->RegisterToken(token)) {
				(void)m_engine->remote_file_registry->ReleaseToken(token);
				return RemoteFileMount::Failed();
			}
			try {
				m_engine->remote_files.push_back({ host, path_key, RemoteFileMount::Access::Read, token });
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
	if (!m_engine || client_uuid.empty() || path.empty() || maximum_timeout_seconds < 3 || maximum_timeout_seconds > 3600
		|| !Connection::IsConnected(m_engine->status.load(std::memory_order_acquire))) {
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
			std::scoped_lock lock(m_engine->remote_file_mutex);
			if (!Connection::IsConnected(m_engine->status.load(std::memory_order_acquire)) || !m_engine->remote_file_registry) {
				return RemoteFileMount::Failed();
			}
			std::erase_if(m_engine->remote_files, [this](const MountedRemoteFile& mounted) {
				return !m_engine->remote_file_registry->HasToken(mounted.token);
			});
			const auto conflict = std::ranges::find_if(m_engine->remote_files, [&writer_path](const MountedRemoteFile& mounted) {
				return mounted.path_key == writer_path;
			});
			if (conflict != m_engine->remote_files.end()) {
				return conflict->access == RemoteFileMount::Access::Read
					? RemoteFileMount::FileBeingRead() : RemoteFileMount::FileBeingWritten();
			}
			if (m_engine->remote_files.size() >= MAX_REMOTE_FILE_CHANNELS) {
				return RemoteFileMount::Failed();
			}
			auto plane_it = m_engine->remote_planes.find(std::string{client_uuid});
			if (plane_it != m_engine->remote_planes.end()) {
				host = plane_it->second;
				if (!host || host->Finished()) return RemoteFileMount::Failed();
			} else {
				host = std::make_shared<Detail::RemoteFile::Host>(m_engine->protocol,
					std::string{static_cast<std::string_view>(m_engine->bind_address)},
					InputPipeline(), OutputPipeline(), maximum_timeout_seconds, m_engine->remote_file_registry, m_logger);
				if (!host->Start()) return RemoteFileMount::Failed();
				m_engine->remote_planes.emplace(std::string{client_uuid}, host);
				started_plane = true;
			}
			if (!m_engine->remote_file_registry->AddMount(token, normalized, RemoteFileMount::Access::Write)) {
				return RemoteFileMount::Failed();
			}
			if (!host->RegisterToken(token)) {
				(void)m_engine->remote_file_registry->ReleaseToken(token);
				return RemoteFileMount::Failed();
			}
			try {
				m_engine->remote_files.push_back({ host, writer_path, RemoteFileMount::Access::Write, token });
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
		std::scoped_lock lock(m_engine->remote_file_mutex);
		m_engine->remote_files.clear();
		revoked.swap(m_engine->remote_planes);
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

	m_engine->wakeup_read = handles[0];
	m_engine->wakeup_write = handles[1];
	return true;
#else
	m_engine->wakeup_read = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	m_engine->wakeup_write = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	if (m_engine->wakeup_read == INVALID_SOCKET || m_engine->wakeup_write == INVALID_SOCKET) {
		CloseWakeup();
		return false;
	}

	sockaddr_in address{};
	address.sin_family = AF_INET;
	address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	address.sin_port = 0;
	if (::bind(m_engine->wakeup_read, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) == SOCKET_ERROR) {
		CloseWakeup();
		return false;
	}

	int address_size = sizeof(address);
	if (::getsockname(m_engine->wakeup_read, reinterpret_cast<sockaddr*>(&address), &address_size) == SOCKET_ERROR ||
		::connect(m_engine->wakeup_write, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) == SOCKET_ERROR) {
		CloseWakeup();
		return false;
	}

	return true;
#endif
}

void Server::SignalWakeup() noexcept {
#ifdef WINDOWS
	if (m_engine->wakeup_write == INVALID_SOCKET) {
		return;
	}
#else
	if (m_engine->wakeup_write < 0) {
		return;
	}
#endif
	const char signal = 1;
#ifdef UNIX
	[[maybe_unused]] const ssize_t written = ::write(m_engine->wakeup_write, &signal, sizeof(signal));
#else
	(void)::send(m_engine->wakeup_write, &signal, sizeof(signal), 0);
#endif
}

void Server::CloseWakeup() noexcept {
#ifdef WINDOWS
	if (m_engine->wakeup_read != INVALID_SOCKET) {
		closesocket(m_engine->wakeup_read);
		m_engine->wakeup_read = INVALID_SOCKET;
	}

	if (m_engine->wakeup_write != INVALID_SOCKET) {
		closesocket(m_engine->wakeup_write);
		m_engine->wakeup_write = INVALID_SOCKET;
	}
#else
	if (m_engine->wakeup_read >= 0) {
		close(m_engine->wakeup_read);
		m_engine->wakeup_read = -1;
	}

	if (m_engine->wakeup_write >= 0) {
		close(m_engine->wakeup_write);
		m_engine->wakeup_write = -1;
	}
#endif
}

void Server::DisconnectClient(std::string_view uuid) noexcept {
	if (!m_engine) return;

	if (m_engine->accept_thread.get_id() != std::this_thread::get_id()) {
		PostCommand({ CommandType::DisconnectClient, std::string{uuid} });
		return;
	}

	DisconnectClientOnLoop(uuid);
}

void Server::DisconnectClientOnLoop(std::string_view uuid) noexcept {
	auto session_it = m_engine->sessions.find(std::string{uuid});
	if (session_it == m_engine->sessions.end()) {
		return;
	}

	auto session = session_it->second;
	m_engine->sessions.erase(session_it);
	if (m_engine->telemetry) m_engine->telemetry->RecordConnectionClosed();
	session->Close();
	if (session->Client() && session->Client()->Socket()) {
		session->Client()->Socket()->Disconnect();
				m_logger << Logger::Level::LowLevel << "Disconnected client: " << uuid << std::endl;
	}
}

void Server::AcceptOneClient() noexcept {
	auto expected_client = m_engine->socket_server->Accept();
	if (!expected_client) {
		if (Connection::IsConnected(m_engine->status.load())) {
			m_logger << Logger::Level::LowLevel << expected_client.error()->what() << std::endl;
		}

		return;
	}
#ifdef WINDOWS
	if (m_engine->sessions.size() >= static_cast<std::size_t>(FD_SETSIZE - 2)) {
		m_logger << Logger::Level::Warning << "Windows select client limit reached; closing accepted client" << std::endl;
		expected_client.value()->Disconnect();
		return;
	}
#endif
	const std::string client_uuid = expected_client.value()->UUID();
	auto connection = CreateConnection(expected_client.value());
	const auto [_, inserted] = m_engine->sessions.emplace(client_uuid,
		std::make_shared<Detail::Session>(client_uuid, std::move(connection)));
	if (inserted && m_engine->telemetry) m_engine->telemetry->RecordConnectionAccepted();
	m_logger << Logger::Level::LowLevel << "AcceptClients: accepted client uuid=" << std::string_view{client_uuid} << std::endl;
}

void Server::PostCompletion(Completion completion) noexcept {
	{
		std::scoped_lock lock(m_engine->completion_mutex);
		m_engine->completions.push_back(std::move(completion));
	}

	SignalWakeup();
}

void Server::PostCommand(Command command) noexcept {
	{
		std::scoped_lock lock(m_engine->command_mutex);
		m_engine->commands.push_back(std::move(command));
	}

	SignalWakeup();
}

void Server::DrainCommands() noexcept {
	std::deque<Command> commands;
	{
		std::scoped_lock lock(m_engine->command_mutex);
		commands.swap(m_engine->commands);
	}

	for (const auto& command: commands) {
		switch (command.type) {
			case CommandType::DisconnectClient:
				DisconnectClientOnLoop(command.uuid);
				break;
			case CommandType::DisconnectAll:
				{
					std::vector<std::string> uuids;
					uuids.reserve(m_engine->sessions.size());
					for (const auto& [uuid, _]: m_engine->sessions) {
						uuids.push_back(uuid);
					}

					for (const auto& uuid: uuids) {
						DisconnectClientOnLoop(uuid);
					}
				}

				break;
			case CommandType::Stop:
				m_engine->status.store(Connection::Status::Disconnecting, std::memory_order_release);
				break;
		}
	}
}

void Server::DrainCompletions() noexcept {
	std::deque<Completion> completions;
	{
		std::scoped_lock lock(m_engine->completion_mutex);
		completions.swap(m_engine->completions);
	}

	for (auto& [_, session]: m_engine->sessions) {
		session->SetTaskBlocked(false);
	}

	for (auto& completion: completions) {
		auto session_it = m_engine->sessions.find(completion.uuid);
		if (session_it == m_engine->sessions.end()) {
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
	Detail::EventLoop event_loop(*m_engine->socket_server, m_engine->wakeup_read, m_engine->status, m_logger);
	event_loop.Run(
		[this]() noexcept { AcceptOneClient(); },
		[this]() {
			Detail::EventLoop::SessionList sessions;
			sessions.reserve(m_engine->sessions.size());
			for (const auto& [_, session]: m_engine->sessions) {
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
			std::scoped_lock lock(m_engine->remote_file_mutex);
			planes.reserve(m_engine->remote_planes.size());
			for (const auto& [_, host]: m_engine->remote_planes) {
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
			std::scoped_lock lock(m_engine->remote_file_mutex);
			for (const auto& [_, host]: m_engine->remote_planes) {
				if (host) host->SetTaskBlocked(false);
			}
		}

	);
	for (auto& [uuid, session]: m_engine->sessions) {
		(void)uuid;
		if (m_engine->telemetry) m_engine->telemetry->RecordConnectionClosed();
		session->Close();
		if (session->Client() && session->Client()->Socket()) {
			session->Client()->Socket()->Disconnect();
		}
	}

	m_engine->sessions.clear();
	m_engine->socket_server->Disconnect();
	m_engine->socket_server.reset();
	CloseWakeup();
	m_engine->status.store(Connection::Status::Disconnected, std::memory_order_release);
	m_logger << Logger::Level::LowLevel << "Stopped accept event loop" << std::endl;
}

void Server::ProcessSession(const std::shared_ptr<Detail::Session>& session, bool readable, bool writable) noexcept {
	if (!session || session->Closed() || session->InFlight() || !session->Client() || !m_engine->pool) {
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

	if (!m_engine->pool->HasCapacity()) {
		if (m_engine->telemetry) m_engine->telemetry->RecordWorkerQueueBackpressure();
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
	if (!m_engine->pool->Submit({ client_uuid, std::move(packet), {} })) {
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
	if (!m_engine->pool || !m_engine->pool->HasCapacity()) {
		host->SetTaskBlocked(true);
		return;
	}
	Detail::RemoteFile::Message request = host->TakeRequest();
	if (request.request_id == 0) return;
	auto queued_request = std::make_shared<Detail::RemoteFile::Message>(std::move(request));
	if (!m_engine->pool->Submit({{}, nullptr, [this, host, queued_request]() {
		const Detail::RemoteFile::Message response = host->ProcessRequest(*queued_request);
		if (!host->QueueResponse(response)) host->Stop();
		SignalWakeup();
	}})) {
		host->RequeueRequest(std::move(*queued_request));
	}
}
