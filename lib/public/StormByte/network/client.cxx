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
using namespace StormByte::Network;
Client::Client(DeserializePacketFunction deserialize_packet_function,
	StormByte::Safe::Shared<Logger::Log> logger) noexcept:
	Endpoint(std::move(deserialize_packet_function), std::move(logger)),
	m_connection(nullptr) {}

Client::~Client() noexcept {
	Disconnect();
}

Client::Client(Client&& other) noexcept:
	Endpoint(std::move(other)), m_connection(std::move(other.m_connection)),
	m_remote_address(std::move(other.m_remote_address)), m_protocol(other.m_protocol) {
	std::scoped_lock lock(other.m_remote_file_mutex);
	m_remote_file_plane = std::move(other.m_remote_file_plane);
}

Client& Client::operator=(Client&& other) noexcept {
	if (this == &other) return *this;
	Disconnect();
	{
		std::scoped_lock lock(m_remote_file_mutex);
		if (m_remote_file_plane) m_remote_file_plane->Stop();
		m_remote_file_plane.reset();
	}
	std::scoped_lock lock(m_remote_file_mutex, other.m_remote_file_mutex);
	Endpoint::operator=(std::move(other));
	m_connection = std::move(other.m_connection);
	m_remote_address = std::move(other.m_remote_address);
	m_protocol = other.m_protocol;
	m_remote_file_plane = std::move(other.m_remote_file_plane);
	return *this;
}

bool Client::Connect(const Connection::Protocol& protocol, std::string_view address, const unsigned short& port) {
	if (m_connection) {
		m_logger << Logger::Level::Error << "Client is already connected." << std::endl;
		return false;
	}

	try {
		std::shared_ptr<Socket::Client> socket = std::make_shared<Socket::Client>(protocol, m_logger);
		if (!socket->Connect(address, port)) {
			m_logger << Logger::Level::Error << "Failed to connect to " << std::string_view{address} << ":" << port
					<< " using protocol " << Connection::ProtocolString(protocol) << std::endl;
			return false;
		}

		auto connection = CreateConnection(socket);
		m_remote_address = address;
		m_protocol = protocol;
		{
			std::scoped_lock lock(m_remote_file_mutex);
			if (m_remote_file_plane) m_remote_file_plane->Stop();
			m_remote_file_plane.reset();
		}
		m_connection = std::move(connection);
		m_logger << Logger::Level::LowLevel << "Successfully connected to " << address << ":" << port
				<< " using protocol " << Connection::ProtocolString(protocol) << std::endl;
		return true;
	} catch (const std::bad_alloc& bd) {
		m_logger << Logger::Level::Error << "Failed to allocate memory for socket: " << bd.what() << std::endl;
		return false;
	}
}

void Client::Disconnect() noexcept {
	if (m_connection) {
		m_logger << Logger::Level::LowLevel << "Disconnecting client." << std::endl;
		m_connection.reset();
	}
}

Connection::Status Client::Status() const noexcept {
	return m_connection ? m_connection->Status() : Connection::Status::Disconnected;
}

PacketPointer Client::Send(const Transport::Packet& packet) noexcept {
	return Endpoint::Send(m_connection, packet);
}

RemoteFileReaderHandle Client::CreateRemoteFileReader(const RemoteFileMount& mount) noexcept {
	if (mount.Result() != RemoteFileMount::Status::Authorized
		|| mount.Mode() != RemoteFileMount::Access::Read || mount.Port() == 0
		|| mount.MaximumTimeoutSeconds() < 3 || m_remote_address.empty()) {
		return {};
	}

	try {
		std::scoped_lock lock(m_remote_file_mutex);
		std::shared_ptr<Detail::RemoteFile::DataPlane> plane = m_remote_file_plane;
		const bool created = !plane;
		if (plane && (plane->Failed() || plane->Port() != mount.Port())) return {};
		if (!plane) {
			auto socket = std::make_shared<Socket::Client>(m_protocol, m_logger);
			if (!socket->Connect(m_remote_address, mount.Port())) return {};
			std::string local_address = socket->LocalAddress();
			if (local_address.empty()) {
				socket->Disconnect();
				return {};
			}
			auto device = Detail::RemoteFile::CreateNetworkDevice(local_address);
			plane = std::make_shared<Detail::RemoteFile::DataPlane>(std::move(socket),
				InputPipeline(), OutputPipeline(), mount.MaximumTimeoutSeconds(), m_logger,
				std::move(local_address), std::move(device), mount.Port());
		}

		const std::string locator = "remote://" + m_remote_address + ":" + std::to_string(mount.Port());
		RemoteFileReaderHandle reader{new BufferedRemoteFileReader(StormByte::Safe::String{locator}, plane, mount.Token())};
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
			m_remote_file_plane = plane;
		}
		return reader;
	} catch (...) {
		return {};
	}
}

RemoteFileWriterHandle Client::CreateRemoteFileWriter(const RemoteFileMount& mount) noexcept {
	if (mount.Result() != RemoteFileMount::Status::Authorized
		|| mount.Mode() != RemoteFileMount::Access::Write || mount.Port() == 0
		|| mount.MaximumTimeoutSeconds() < 3 || m_remote_address.empty()) {
		return {};
	}

	try {
		std::scoped_lock lock(m_remote_file_mutex);
		std::shared_ptr<Detail::RemoteFile::DataPlane> plane = m_remote_file_plane;
		const bool created = !plane;
		if (plane && (plane->Failed() || plane->Port() != mount.Port())) return {};
		if (!plane) {
			auto socket = std::make_shared<Socket::Client>(m_protocol, m_logger);
			if (!socket->Connect(m_remote_address, mount.Port())) return {};
			std::string local_address = socket->LocalAddress();
			if (local_address.empty()) {
				socket->Disconnect();
				return {};
			}
			auto device = Detail::RemoteFile::CreateNetworkDevice(local_address);
			plane = std::make_shared<Detail::RemoteFile::DataPlane>(std::move(socket),
				InputPipeline(), OutputPipeline(), mount.MaximumTimeoutSeconds(), m_logger,
				std::move(local_address), std::move(device), mount.Port());
		}

		const std::string locator = "remote://" + m_remote_address + ":" + std::to_string(mount.Port());
		RemoteFileWriterHandle writer{new BufferedRemoteFileWriter(StormByte::Safe::String{locator}, plane, mount.Token())};
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
			m_remote_file_plane = plane;
		}
		return writer;
	} catch (...) {
		return {};
	}
}
