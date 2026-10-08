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
#include <StormByte/network/connection/client.hxx>
#include <StormByte/network/remote_file_protocol.hxx>
#include <StormByte/network/socket/client.hxx>
#include <StormByte/network/transport/frame.hxx>
#include <StormByte/network/transport/packet.hxx>
#include <StormByte/safe/function.hxx>

#include <memory>
#include <mutex>
#include <string>
#include <utility>

using namespace StormByte;
using namespace StormByte::Network;

namespace {
	/**
	 * @brief Own a mount-failure callback context entirely inside Network.
	 * @tparam Callable Copyable callback target.
	 * @param callable Failure notification invoked synchronously by the plane.
	 * @return Callback with Network allocation, clone and release operations.
	 */
	template<typename Callable>
	Safe::Function<void()> MakeFailureCallback(Callable callable) {
		auto context = std::make_unique<Callable>(std::move(callable));
		Safe::Function<void()> callback(context.get(),
			[](void* state) -> Safe::Status {
				(*static_cast<Callable*>(state))();
				return Safe::Status::Success;
			},
			[](const void* state) noexcept -> void* {
				try {
					return new Callable(*static_cast<const Callable*>(state));
				} catch (...) {
					return nullptr;
				}
			},
			[](void* state) noexcept {
				delete static_cast<Callable*>(state);
			});
		(void)context.release();
		return callback;
	}
}

Client::Client(DeserializePacketFunction deserialize_packet_function,
	StormByte::Safe::Shared<Logger::Log> logger):
	Endpoint(std::move(deserialize_packet_function), std::move(logger)),
	m_telemetry(Safe::MakeShared<ClientTelemetry>()) {}

Client::~Client() noexcept {
	Disconnect();
}

Client::Client(Client&& other) noexcept:
	Endpoint(std::move(other)),
	m_connection(std::move(other.m_connection)),
	m_remote_address(std::move(other.m_remote_address)),
	m_protocol(other.m_protocol),
	m_remote_file_plane(std::move(other.m_remote_file_plane)),
	m_telemetry(std::move(other.m_telemetry)) {}

Client& Client::operator=(Client&& other) noexcept {
	if (this == &other)
		return *this;
	Disconnect();
	{
		std::scoped_lock lock(m_remote_file_mutex);
		if (m_remote_file_plane)
			m_remote_file_plane->Stop();
	}
	Endpoint::operator=(std::move(other));
	m_connection = std::move(other.m_connection);
	m_remote_address = std::move(other.m_remote_address);
	m_protocol = other.m_protocol;
	m_remote_file_plane = std::move(other.m_remote_file_plane);
	m_telemetry = std::move(other.m_telemetry);
	return *this;
}

bool Client::Connect(const Connection::Protocol& protocol, std::string_view address, const unsigned short& port) {
	try {
		if (!m_telemetry)
			m_telemetry = Safe::MakeShared<ClientTelemetry>();
		m_telemetry->RecordConnectionAttempt();
		if (m_connection) {
			m_telemetry->RecordConnectionFailed();
			m_logger << Logger::Level::Error << "Client is already connected." << std::endl;
			return false;
		}

		auto socket = Socket::Client::Create(protocol, m_logger);
		if (!socket || !socket->Connect(address, port)) {
			m_telemetry->RecordConnectionFailed();
			m_logger << Logger::Level::Error << "Failed to connect to " << std::string_view{address} << ":" << port
					<< " using protocol " << Connection::ProtocolString(protocol) << std::endl;
			return false;
		}

		auto connection = Safe::MakeShared<Connection::Client>(std::move(socket), Buffer::Pipeline{}, Buffer::Pipeline{});
		m_remote_address = address;
		m_protocol = protocol;
		{
			std::scoped_lock lock(m_remote_file_mutex);
			if (m_remote_file_plane)
				m_remote_file_plane->Stop();
			m_remote_file_plane.reset();
		}
		m_connection = std::move(connection);
		m_telemetry->RecordConnectionEstablished();
		m_logger << Logger::Level::LowLevel << "Successfully connected to " << address << ":" << port
				<< " using protocol " << Connection::ProtocolString(protocol) << std::endl;
		return true;
	} catch (...) {
		if (m_telemetry)
			m_telemetry->RecordConnectionFailed();
		m_logger << Logger::Level::Error << "Failed to initialize client connection state." << std::endl;
		return false;
	}
}

void Client::Disconnect() noexcept {
	if (m_connection) {
		m_logger << Logger::Level::LowLevel << "Disconnecting client." << std::endl;
		m_connection.reset();
		if (m_telemetry)
			m_telemetry->RecordDisconnected();
	}
}

Connection::Status Client::Status() const noexcept {
	return m_connection ? m_connection->Status() : Connection::Status::Disconnected;
}

PacketPointer Client::Send(const Transport::Packet& packet) noexcept {
	if (!m_connection)
		return {};
	auto connection = m_connection;
	const std::string_view uuid = connection->Socket()->UUID();
	if (!AllowOutgoingOpcode(uuid, packet.Opcode())) {
		Disconnect();
		return {};
	}
	if (m_telemetry)
		m_telemetry->RecordRequest();
	auto sample = m_telemetry ? m_telemetry->MeasureRequest() : StormByte::Clock::Sample{};
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
	if (m_telemetry)
		m_telemetry->RecordRequestResult(sample.Stop(), static_cast<bool>(response));
	return response;
}

bool Client::ConfigurePipelines(Buffer::Pipeline input, Buffer::Pipeline output) noexcept {
	try {
		std::scoped_lock lock(m_remote_file_mutex);
		return m_connection && !m_remote_file_plane
			&& m_connection->ConfigurePipelines(std::move(input), std::move(output));
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
	return m_telemetry;
}

RemoteFileReaderHandle Client::CreateRemoteFileReader(const RemoteFileMount& mount) noexcept {
	if (mount.Result() != RemoteFileMount::Status::Authorized
		|| mount.Mode() != RemoteFileMount::Access::Read || mount.Port() == 0
		|| mount.MaximumTimeoutSeconds() < 3 || m_remote_address.empty()) {
		return {};
	}

	try {
		std::scoped_lock lock(m_remote_file_mutex);
		Safe::Shared<Detail::RemoteFile::DataPlane> plane = m_remote_file_plane;
		const bool created = !plane;
		if (plane && (plane->Failed() || plane->Port() != mount.Port())) return {};
		if (!plane) {
			if (!m_connection)
				return {};
			Safe::Pair<Buffer::Pipeline, Buffer::Pipeline> pipelines = m_connection->FilePipelines();
			auto socket = Socket::Client::Create(m_protocol, m_logger);
			if (!socket || !socket->Connect(m_remote_address, mount.Port()))
				return {};
			Safe::String local_address = socket->LocalAddress();
			if (local_address.empty()) {
				socket->Disconnect();
				return {};
			}
			auto device = Detail::RemoteFile::CreateNetworkDevice(local_address);
			plane = Safe::MakeShared<Detail::RemoteFile::DataPlane>(std::move(socket),
				std::move(pipelines.first), std::move(pipelines.second), mount.MaximumTimeoutSeconds(), m_logger,
				std::move(local_address), std::move(device), mount.Port());
		}

		const std::string locator = "remote://" + std::string{static_cast<std::string_view>(m_remote_address)}
			+ ":" + std::to_string(mount.Port());
		auto reader = Safe::MakeUnique<BufferedRemoteFileReader>(Safe::String{locator}, plane, mount.Token());
		if (!plane->RegisterToken(mount.Token(), MakeFailureCallback([instance = reader.get()]() noexcept {
			instance->MarkFailed();
		}))) {
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
		Safe::Shared<Detail::RemoteFile::DataPlane> plane = m_remote_file_plane;
		const bool created = !plane;
		if (plane && (plane->Failed() || plane->Port() != mount.Port())) return {};
		if (!plane) {
			if (!m_connection)
				return {};
			Safe::Pair<Buffer::Pipeline, Buffer::Pipeline> pipelines = m_connection->FilePipelines();
			auto socket = Socket::Client::Create(m_protocol, m_logger);
			if (!socket || !socket->Connect(m_remote_address, mount.Port()))
				return {};
			Safe::String local_address = socket->LocalAddress();
			if (local_address.empty()) {
				socket->Disconnect();
				return {};
			}
			auto device = Detail::RemoteFile::CreateNetworkDevice(local_address);
			plane = Safe::MakeShared<Detail::RemoteFile::DataPlane>(std::move(socket),
				std::move(pipelines.first), std::move(pipelines.second), mount.MaximumTimeoutSeconds(), m_logger,
				std::move(local_address), std::move(device), mount.Port());
		}

		const std::string locator = "remote://" + std::string{static_cast<std::string_view>(m_remote_address)}
			+ ":" + std::to_string(mount.Port());
		auto writer = Safe::MakeUnique<BufferedRemoteFileWriter>(Safe::String{locator}, plane, mount.Token());
		if (!plane->RegisterToken(mount.Token(), MakeFailureCallback([instance = writer.get()]() noexcept {
			instance->MarkFailed();
		}))) {
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
