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
#include <StormByte/network/client.hxx>
#include <StormByte/network/remote_file_protocol.hxx>
#include <StormByte/network/socket/client.hxx>
#include <StormByte/network/transport/frame.hxx>
#include <StormByte/network/transport/packet.hxx>

#include <memory>
#include <mutex>

using namespace StormByte::Network;

class Client::Implementation final {
	public:
		/**
		 * @brief Construct empty client state with client-owned telemetry.
		 */
		Implementation() = default;

	private:
		friend class Client;

		/**
		 * @brief Active framed connection.
		 */
		std::shared_ptr<Connection::Client> connection;
		/**
		 * @brief Last connected address used to open private file channels.
		 */
		StormByte::Safe::String remote_address;
		/**
		 * @brief Address family used for private file channels.
		 */
		Connection::Protocol protocol{Connection::Protocol::IPv4};
		/**
		 * @brief Protects peer-plane creation and mount registration.
		 */
		std::mutex remote_file_mutex;
		/**
		 * @brief Peer-scoped data plane shared by remote-file leaves.
		 */
		std::shared_ptr<Detail::RemoteFile::DataPlane> remote_file_plane;
		/**
		 * @brief Counters scoped to this client instance.
		 */
		StormByte::Safe::Shared<ClientTelemetry> telemetry{StormByte::Safe::Heap::MakeShared<ClientTelemetry>()};
};

Client::Client(DeserializePacketFunction deserialize_packet_function,
	StormByte::Safe::Shared<Logger::Log> logger):
	Endpoint(std::move(deserialize_packet_function), std::move(logger)),
	m_backend(StormByte::Safe::Unique<Implementation>::MakePointer<Implementation>()) {}

Client::~Client() noexcept {
	Disconnect();
}

Client::Client(Client&& other) noexcept:
	Endpoint(std::move(other)), m_backend(std::move(other.m_backend)) {}

Client& Client::operator=(Client&& other) noexcept {
	if (this == &other) return *this;
	Disconnect();
	if (m_backend) {
		std::scoped_lock lock(m_backend->remote_file_mutex);
		if (m_backend->remote_file_plane) m_backend->remote_file_plane->Stop();
	}
	Endpoint::operator=(std::move(other));
	m_backend = std::move(other.m_backend);
	return *this;
}

bool Client::Connect(const Connection::Protocol& protocol, std::string_view address, const unsigned short& port) {
	try {
		if (!m_backend) {
			m_backend = StormByte::Safe::Unique<Implementation>::MakePointer<Implementation>();
		}
		if (m_backend->telemetry) m_backend->telemetry->RecordConnectionAttempt();
		if (m_backend->connection) {
			if (m_backend->telemetry) m_backend->telemetry->RecordConnectionFailed();
			m_logger << Logger::Level::Error << "Client is already connected." << std::endl;
			return false;
		}

		std::shared_ptr<Socket::Client> socket = std::make_shared<Socket::Client>(protocol, m_logger);
		if (!socket->Connect(address, port)) {
			if (m_backend->telemetry) m_backend->telemetry->RecordConnectionFailed();
			m_logger << Logger::Level::Error << "Failed to connect to " << std::string_view{address} << ":" << port
					<< " using protocol " << Connection::ProtocolString(protocol) << std::endl;
			return false;
		}

		auto connection = CreateConnection(socket);
		m_backend->remote_address = address;
		m_backend->protocol = protocol;
		{
			std::scoped_lock lock(m_backend->remote_file_mutex);
			if (m_backend->remote_file_plane) m_backend->remote_file_plane->Stop();
			m_backend->remote_file_plane.reset();
		}
		m_backend->connection = std::move(connection);
		if (m_backend->telemetry) m_backend->telemetry->RecordConnectionEstablished();
		m_logger << Logger::Level::LowLevel << "Successfully connected to " << address << ":" << port
				<< " using protocol " << Connection::ProtocolString(protocol) << std::endl;
		return true;
	} catch (const std::bad_alloc& bd) {
		if (m_backend && m_backend->telemetry) m_backend->telemetry->RecordConnectionFailed();
		m_logger << Logger::Level::Error << "Failed to allocate memory for socket: " << bd.what() << std::endl;
		return false;
	}
}

void Client::Disconnect() noexcept {
	if (m_backend && m_backend->connection) {
		m_logger << Logger::Level::LowLevel << "Disconnecting client." << std::endl;
		m_backend->connection.reset();
		if (m_backend->telemetry) m_backend->telemetry->RecordDisconnected();
	}
}

Connection::Status Client::Status() const noexcept {
	return m_backend && m_backend->connection ? m_backend->connection->Status() : Connection::Status::Disconnected;
}

PacketPointer Client::Send(const Transport::Packet& packet) noexcept {
	if (!m_backend || !m_backend->connection)
		return {};
	auto connection = m_backend->connection;
	const auto uuid = connection->Socket()->UUID();
	if (!AllowOutgoingOpcode(uuid, packet.Opcode())) {
		Disconnect();
		return {};
	}
	if (m_backend->telemetry)
		m_backend->telemetry->RecordRequest();
	auto sample = m_backend->telemetry ? m_backend->telemetry->MeasureRequest() : StormByte::Clock::Sample{};
	PacketPointer response;
	if (Endpoint::Reply(connection, packet)) {
		auto frame = connection->Receive(m_logger);
		if (!AllowIncomingOpcode(uuid, frame.Opcode()) || !frame.DecodeInput(connection->InputPipeline(), m_logger))
			Disconnect();
		else
			response = frame.ProcessPacket(m_deserialize_packet_function, m_logger);
	}
	if (!response)
		Disconnect();
	if (m_backend->telemetry)
		m_backend->telemetry->RecordRequestResult(sample.Stop(), static_cast<bool>(response));
	return response;
}

bool Client::ConfigurePipelines(Buffer::Pipeline input, Buffer::Pipeline output) noexcept {
	try {
		if (!m_backend)
			return false;
		std::scoped_lock lock(m_backend->remote_file_mutex);
		return m_backend->connection && !m_backend->remote_file_plane
			&& m_backend->connection->ConfigurePipelines(std::move(input), std::move(output));
	} catch (...) {
		return false;
	}
}

bool Client::AllowIncomingOpcode(std::string_view, Transport::Packet::OpcodeType) const noexcept {
	return true;
}

bool Client::AllowOutgoingOpcode(std::string_view, Transport::Packet::OpcodeType) const noexcept {
	return true;
}

StormByte::Safe::Shared<ClientTelemetry> Client::Telemetry() const noexcept {
	return m_backend ? m_backend->telemetry : StormByte::Safe::Shared<ClientTelemetry>{};
}

RemoteFileReaderHandle Client::CreateRemoteFileReader(const RemoteFileMount& mount) noexcept {
	if (!m_backend || mount.Result() != RemoteFileMount::Status::Authorized
		|| mount.Mode() != RemoteFileMount::Access::Read || mount.Port() == 0
		|| mount.MaximumTimeoutSeconds() < 3 || m_backend->remote_address.empty()) {
		return {};
	}

	try {
		std::scoped_lock lock(m_backend->remote_file_mutex);
		std::shared_ptr<Detail::RemoteFile::DataPlane> plane = m_backend->remote_file_plane;
		const bool created = !plane;
		if (plane && (plane->Failed() || plane->Port() != mount.Port())) return {};
		if (!plane) {
			if (!m_backend->connection)
				return {};
			auto pipelines = m_backend->connection->FilePipelines();
			auto socket = std::make_shared<Socket::Client>(m_backend->protocol, m_logger);
			if (!socket->Connect(m_backend->remote_address, mount.Port())) return {};
			std::string local_address = socket->LocalAddress();
			if (local_address.empty()) {
				socket->Disconnect();
				return {};
			}
			auto device = Detail::RemoteFile::CreateNetworkDevice(local_address);
			plane = std::make_shared<Detail::RemoteFile::DataPlane>(std::move(socket),
				std::move(pipelines.first), std::move(pipelines.second), mount.MaximumTimeoutSeconds(), m_logger,
				std::move(local_address), std::move(device), mount.Port());
		}

		const std::string locator = "remote://" + std::string{static_cast<std::string_view>(m_backend->remote_address)}
			+ ":" + std::to_string(mount.Port());
		auto reader = StormByte::Safe::Heap::MakeUnique<BufferedRemoteFileReader>(StormByte::Safe::String{locator}, plane, mount.Token());
		if (!plane->RegisterToken(mount.Token(), [instance = reader.get()]() noexcept { instance->MarkFailed(); })) {
			reader->m_token_released = true;
			reader.reset();
			if (created) plane->Stop();
			return {};
		}
		if (created) {
			if (!plane->StartHeartbeat()) {
				(void)plane->ReleaseToken(mount.Token());
				reader->m_token_released = true;
				reader.reset();
				plane->Stop();
				return {};
			}
			m_backend->remote_file_plane = plane;
		}
		return reader;
	} catch (...) {
		return {};
	}
}

RemoteFileWriterHandle Client::CreateRemoteFileWriter(const RemoteFileMount& mount) noexcept {
	if (!m_backend || mount.Result() != RemoteFileMount::Status::Authorized
		|| mount.Mode() != RemoteFileMount::Access::Write || mount.Port() == 0
		|| mount.MaximumTimeoutSeconds() < 3 || m_backend->remote_address.empty()) {
		return {};
	}

	try {
		std::scoped_lock lock(m_backend->remote_file_mutex);
		std::shared_ptr<Detail::RemoteFile::DataPlane> plane = m_backend->remote_file_plane;
		const bool created = !plane;
		if (plane && (plane->Failed() || plane->Port() != mount.Port())) return {};
		if (!plane) {
			if (!m_backend->connection)
				return {};
			auto pipelines = m_backend->connection->FilePipelines();
			auto socket = std::make_shared<Socket::Client>(m_backend->protocol, m_logger);
			if (!socket->Connect(m_backend->remote_address, mount.Port())) return {};
			std::string local_address = socket->LocalAddress();
			if (local_address.empty()) {
				socket->Disconnect();
				return {};
			}
			auto device = Detail::RemoteFile::CreateNetworkDevice(local_address);
			plane = std::make_shared<Detail::RemoteFile::DataPlane>(std::move(socket),
				std::move(pipelines.first), std::move(pipelines.second), mount.MaximumTimeoutSeconds(), m_logger,
				std::move(local_address), std::move(device), mount.Port());
		}

		const std::string locator = "remote://" + std::string{static_cast<std::string_view>(m_backend->remote_address)}
			+ ":" + std::to_string(mount.Port());
		auto writer = StormByte::Safe::Heap::MakeUnique<BufferedRemoteFileWriter>(StormByte::Safe::String{locator}, plane, mount.Token());
		if (!plane->RegisterToken(mount.Token(), [instance = writer.get()]() noexcept { instance->MarkFailed(); })) {
			writer->m_token_released = true;
			writer.reset();
			if (created) plane->Stop();
			return {};
		}
		if (created) {
			if (!plane->StartHeartbeat()) {
				(void)plane->ReleaseToken(mount.Token());
				writer->m_token_released = true;
				writer.reset();
				plane->Stop();
				return {};
			}
			m_backend->remote_file_plane = plane;
		}
		return writer;
	} catch (...) {
		return {};
	}
}
