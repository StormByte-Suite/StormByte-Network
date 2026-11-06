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
#include <StormByte/network/connection/resolver.hxx>
#include <StormByte/network/remote_file_protocol.hxx>
#include <StormByte/network/socket/client.hxx>
#include <StormByte/network/transport/frame.hxx>
#include <StormByte/network/transport/packet.hxx>
#include <StormByte/network/tls_configuration.hxx>
#include <StormByte/safe/function.hxx>

#include <memory>
#include <mutex>
#include <new>
#include <string>
#include <thread>
#include <utility>

using namespace StormByte;
using namespace StormByte::Network;

namespace {
	/**
	 * @brief Discard a non-replayable stream if a callback or allocation unwinds.
	 */
	struct OperationCleanup {
		Client* client; ///< Synchronous borrowed owner.
		void (*cleanup)(Client*) noexcept; ///< Provider cleanup, cleared after completion.

		/**
		 * @brief Release an abandoned operation without suppressing its exception.
		 */
		~OperationCleanup() noexcept {
			if (cleanup)
				cleanup(client);
		}
	};

	/**
	 * @brief Convert a native outcome while preserving cancellation over socket EOF.
	 * @param state Native outcome.
	 * @param control Original operation control.
	 * @return Typed application-client failure.
	 */
	Client::Error ClientFailure(Socket::OperationState state, const Socket::OperationControl& control) noexcept {
		const auto stopped = control.Check();
		if (stopped != Socket::OperationState::Complete)
			state = stopped;
		switch (state) {
			case Socket::OperationState::Cancelled: return Client::Error::Cancelled;
			case Socket::OperationState::TimedOut: return Client::Error::TimedOut;
			case Socket::OperationState::Busy: return Client::Error::ResolverBusy;
			default: return Client::Error::Transport;
		}
	}

	/**
	 * @brief Build an overflow-safe absolute budget before serializing operations.
	 * @param timeout Nonnegative duration or the unlimited sentinel.
	 * @param cancelled Borrowed sticky stream cancellation flag.
	 * @return Absolute deadline and cancellation.
	 */
	Socket::OperationControl ClientControl(std::chrono::milliseconds timeout, const Safe::Atomic<bool>& cancelled) noexcept {
		const auto now = std::chrono::steady_clock::now();
		const auto room = std::chrono::duration_cast<std::chrono::milliseconds>(
			std::chrono::steady_clock::time_point::max() - now);
		return {timeout >= room ? std::chrono::steady_clock::time_point::max() : now + timeout, &cancelled};
	}

	/**
	 * @brief Include serialized-operation queueing in the caller's original budget.
	 * @param lock Deferred operation lock, acquired only on successful return.
	 * @param control Borrowed operation budget.
	 * @return Completion, cancellation or expiry without touching another operation.
	 */
	Socket::OperationState LockOperation(std::unique_lock<Safe::Mutex>& lock, const Socket::OperationControl& control) {
		for (;;) {
			const auto state = control.Check();
			if (state != Socket::OperationState::Complete)
				return state;
			if (lock.try_lock())
				return Socket::OperationState::Complete;
			std::this_thread::sleep_for(std::chrono::milliseconds{1});
		}
	}

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
	m_resolver(Safe::MakeShared<Connection::Resolver>()),
	m_telemetry(Safe::MakeShared<ClientTelemetry>()) {}

Client::~Client() noexcept {
	Disconnect();
}

Client::Client(Client&& other) noexcept:
	Endpoint(std::move(other)),
	m_cancelled(other.m_cancelled.load(Safe::MemoryOrder::Acquire)),
	m_active_socket(std::move(other.m_active_socket)),
	m_resolver(std::move(other.m_resolver)),
	m_tls_configuration(std::move(other.m_tls_configuration)),
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
	m_cancelled.store(other.m_cancelled.load(Safe::MemoryOrder::Acquire), Safe::MemoryOrder::Release);
	m_active_socket = std::move(other.m_active_socket);
	m_resolver = std::move(other.m_resolver);
	m_tls_configuration = std::move(other.m_tls_configuration);
	m_connection = std::move(other.m_connection);
	m_remote_address = std::move(other.m_remote_address);
	m_protocol = other.m_protocol;
	m_remote_file_plane = std::move(other.m_remote_file_plane);
	m_telemetry = std::move(other.m_telemetry);
	return *this;
}

bool Client::Connect(const Connection::Protocol& protocol, std::string_view address, const unsigned short& port) {
	const auto result = Connect(protocol, address, port, std::chrono::seconds{10});
	if (!result)
		m_logger << Logger::Level::Error << "Client connection failed (error "
			<< static_cast<int>(*result.error()) << ")." << std::endl;
	return static_cast<bool>(result);
}

Expected<void, Client::Error> Client::Connect(const Connection::Protocol& protocol, std::string_view address,
	unsigned short port, std::chrono::milliseconds timeout) {
	Safe::String detail;
	const auto result = Connect(protocol, address, port, timeout, detail);
	if (!detail.empty())
		m_logger << Logger::Level::Error << "TLS connection failed: " << std::string_view(detail) << std::endl;
	return result;
}

Expected<void, Client::Error> Client::Connect(const Connection::Protocol& protocol, std::string_view address,
	unsigned short port, std::chrono::milliseconds timeout, Safe::String& detail) {
	detail.clear();
	if (timeout.count() < 0)
		return Unexpected(Error::InvalidTimeout);
	const auto control = ClientControl(timeout, m_cancelled);
	std::unique_lock operation(m_operation_mutex, std::defer_lock);
	const auto queued = LockOperation(operation, {control.deadline});
	if (queued != Socket::OperationState::Complete)
		return Unexpected(ClientFailure(queued, {control.deadline}));
	if (m_cancelled.load(Safe::MemoryOrder::Acquire))
		DropConnection();
	{
		std::scoped_lock state(m_state_mutex);
		if (m_connection)
			return Unexpected(Error::AlreadyConnected);
		m_cancelled.store(false, Safe::MemoryOrder::Release);
	}
	OperationCleanup cleanup{this, [](Client* client) noexcept { client->DropConnection(); }};
	auto fail = [&](Error error) -> Expected<void, Error> {
		const auto state = control.Check();
		if (state != Socket::OperationState::Complete)
			error = ClientFailure(state, control);
		if (error != Error::TLS)
			detail.clear();
		DropConnection();
		if (m_telemetry)
			m_telemetry->RecordConnectionFailed();
		return Unexpected(error);
	};
	try {
		{
			std::scoped_lock state(m_state_mutex);
			if (!m_telemetry)
				m_telemetry = Safe::MakeShared<ClientTelemetry>();
		}
		if (!m_resolver)
			m_resolver = Safe::MakeShared<Connection::Resolver>();
		m_telemetry->RecordConnectionAttempt();
		auto socket = Socket::Client::Create(protocol, m_logger);
		if (!socket)
			return fail(Error::Allocation);
		auto connected = socket->Connect(address, port, *m_resolver, control);
		if (connected.state != Socket::OperationState::Complete)
			return fail(ClientFailure(connected.state, control));
		{
			std::scoped_lock state(m_state_mutex);
			m_active_socket = socket;
		}
		if (m_tls_configuration) {
			auto session = m_tls_configuration->Create();
			if (!session) {
				detail = std::string_view{Crypto::TLS::DescribeSessionError(*session.error())};
				return fail(Error::TLS);
			}
			const auto handshake = socket->StartTLS(std::move(*session), control, detail);
			if (handshake.state != Socket::OperationState::Complete) {
				const auto error = ClientFailure(handshake.state, control);
				return fail(error == Error::Transport ? Error::TLS : error);
			}
		}

		auto connection = Safe::MakeShared<Connection::Client>(socket, Buffer::Pipeline{}, Buffer::Pipeline{});
		m_remote_address = address;
		m_protocol = protocol;
		{
			std::scoped_lock lock(m_remote_file_mutex);
			if (m_remote_file_plane)
				m_remote_file_plane->Stop();
			m_remote_file_plane.reset();
		}
		{
			std::scoped_lock state(m_state_mutex);
			if (control.Check() == Socket::OperationState::Complete)
				m_connection = std::move(connection);
		}
		if (!m_connection)
			return fail(ClientFailure(control.Check(), control));
		m_telemetry->RecordConnectionEstablished();
		m_logger << Logger::Level::LowLevel << "Successfully connected to " << address << ":" << port
				<< " using protocol " << Connection::ProtocolString(protocol) << std::endl;
		cleanup.cleanup = nullptr;
		return {};
	} catch (const std::bad_alloc&) {
		return fail(Error::Allocation);
	}
}

void Client::Cancel() noexcept {
	std::scoped_lock state(m_state_mutex);
	m_cancelled.store(true, Safe::MemoryOrder::Release);
	if (m_active_socket)
		m_active_socket->Interrupt();
}

void Client::DropConnection() noexcept {
	Safe::Shared<Socket::Client> socket;
	{
		std::scoped_lock state(m_state_mutex);
		socket = std::move(m_active_socket);
		const bool connected = static_cast<bool>(m_connection);
		m_connection.reset();
		if (connected && m_telemetry)
			m_telemetry->RecordDisconnected();
	}
	if (socket)
		socket->Disconnect();
}

void Client::Disconnect() noexcept {
	Cancel();
	std::unique_lock operation(m_operation_mutex, std::try_to_lock);
	if (operation.owns_lock())
		DropConnection();
}

Connection::Status Client::Status() const noexcept {
	std::scoped_lock state(m_state_mutex);
	if (m_cancelled.load(Safe::MemoryOrder::Acquire))
		return Connection::Status::Disconnected;
	return m_connection ? m_connection->Status() : Connection::Status::Disconnected;
}

PacketPointer Client::Send(const Transport::Packet& packet) noexcept {
	try {
		auto result = Send(packet, std::chrono::milliseconds::max());
		if (!result)
			m_logger << Logger::Level::Error << "Client request failed (error "
				<< static_cast<int>(*result.error()) << ")." << std::endl;
		return result ? std::move(*result) : PacketPointer{};
	} catch (const std::bad_alloc&) {
		m_logger << Logger::Level::Error << "Client request allocation failed." << std::endl;
		return {};
	}
}

Expected<PacketPointer, Client::Error> Client::Send(const Transport::Packet& packet, std::chrono::milliseconds timeout) {
	if (timeout.count() < 0)
		return Unexpected(Error::InvalidTimeout);
	const auto control = ClientControl(timeout, m_cancelled);
	std::unique_lock operation(m_operation_mutex, std::defer_lock);
	const auto queued = LockOperation(operation, control);
	if (queued != Socket::OperationState::Complete)
		return Unexpected(ClientFailure(queued, control));
	if (control.Check() != Socket::OperationState::Complete) {
		if (m_cancelled.load(Safe::MemoryOrder::Acquire))
			DropConnection();
		return Unexpected(ClientFailure(control.Check(), control));
	}
	if (!m_connection)
		return Unexpected(Error::NotConnected);
	auto connection = m_connection;
	OperationCleanup cleanup{this, [](Client* client) noexcept { client->DropConnection(); }};
	const std::string_view uuid = connection->Socket()->UUID();
	if (m_telemetry)
		m_telemetry->RecordRequest();
	auto sample = m_telemetry ? m_telemetry->MeasureRequest() : StormByte::Clock::Sample{};
	auto finish = [&](Expected<PacketPointer, Error> result) {
		const auto state = control.Check();
		if (state != Socket::OperationState::Complete)
			result = Unexpected(ClientFailure(state, control));
		if (!result)
			DropConnection();
		if (m_telemetry)
			m_telemetry->RecordRequestResult(sample.Stop(), static_cast<bool>(result));
		cleanup.cleanup = nullptr;
		return result;
	};
	try {
		if (!AllowOutgoingOpcode(uuid, packet.Opcode()))
			return finish(Unexpected(Error::Packet));
		Transport::Frame outgoing(packet);
		auto bytes = outgoing.ProcessOutput(connection->OutputPipeline(), m_logger);
		while (!bytes.EoF()) {
			if (control.Check() != Socket::OperationState::Complete)
				return finish(Unexpected(ClientFailure(control.Check(), control)));
			Safe::Binary chunk;
			if (!bytes.Extract(ByteSize{0}, chunk))
				return finish(Unexpected(Error::Packet));
			if (chunk.empty())
				continue;
			const auto sent = connection->Socket()->SendApplication(chunk.span(), control);
			if (sent.state != Socket::OperationState::Complete)
				return finish(Unexpected(ClientFailure(sent.state, control)));
		}
		if (!bytes.IsReadable())
			return finish(Unexpected(Error::Packet));
		auto frame = Transport::Frame::ProcessInput(connection->Socket(), control);
		if (!frame)
			return finish(Unexpected(ClientFailure(*frame.error(), control)));
		if (!AllowIncomingOpcode(uuid, frame->Opcode()) || !frame->DecodeInput(connection->InputPipeline(), m_logger))
			return finish(Unexpected(Error::Packet));
		if (control.Check() != Socket::OperationState::Complete)
			return finish(Unexpected(ClientFailure(control.Check(), control)));
		auto response = frame->ProcessPacket(m_deserialize_packet_function, m_logger);
		if (control.Check() != Socket::OperationState::Complete)
			return finish(Unexpected(ClientFailure(control.Check(), control)));
		if (!response)
			return finish(Unexpected(Error::Packet));
		return finish(std::move(response));
	} catch (const std::bad_alloc&) {
		return finish(Unexpected(Error::Allocation));
	}
}

bool Client::ConfigureTLS(const Crypto::TLS::ClientConfiguration& configuration,
	bool refresh_validation_time) noexcept {
	try {
		Safe::String detail;
		const bool configured = ConfigureTLS(configuration, detail, refresh_validation_time);
		if (!configured)
			m_logger << Logger::Level::Error << "Client TLS configuration failed: " << std::string_view(detail) << std::endl;
		return configured;
	} catch (...) {
		m_logger << Logger::Level::Error << "Cannot allocate client TLS configuration diagnostics." << std::endl;
		return false;
	}
}

bool Client::ConfigureTLS(const Crypto::TLS::ClientConfiguration& configuration,
	Safe::String& detail, bool refresh_validation_time) {
	detail.clear();
	try {
		std::scoped_lock operation(m_operation_mutex);
		std::scoped_lock lock(m_remote_file_mutex);
		if (m_connection || m_remote_file_plane) {
			detail = std::string_view{"Disconnect application and remote-file channels before changing TLS configuration."};
			return false;
		}
		auto snapshot = Safe::MakeShared<Detail::TLSConfiguration>(configuration, refresh_validation_time);
		const auto created = snapshot->Create();
		if (!created) {
			detail = std::string_view{Crypto::TLS::DescribeSessionError(*created.error())};
			return false;
		}
		m_tls_configuration = std::move(snapshot);
		return true;
	} catch (...) {
		detail = std::string_view{"Cannot allocate or copy the client TLS configuration."};
		return false;
	}
}

bool Client::DisableTLS() noexcept {
	std::scoped_lock operation(m_operation_mutex);
	std::scoped_lock lock(m_remote_file_mutex);
	if (m_connection || m_remote_file_plane)
		return false;
	m_tls_configuration.reset();
	return true;
}

bool Client::AllowIncomingOpcode(std::string_view, Transport::Packet::OpcodeType) const noexcept {
	return true;
}

bool Client::AllowOutgoingOpcode(std::string_view, Transport::Packet::OpcodeType) const noexcept {
	return true;
}

StormByte::Safe::Shared<ClientTelemetry> Client::Telemetry() const noexcept {
	std::scoped_lock state(m_state_mutex);
	return m_telemetry;
}

RemoteFileReaderHandle Client::CreateRemoteFileReader(const RemoteFileMount& mount) noexcept {
	std::scoped_lock operation(m_operation_mutex);
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
			auto socket = Socket::Client::Create(m_protocol, m_logger);
			if (!socket || !socket->Connect(m_remote_address, mount.Port()))
				return {};
			if (m_tls_configuration) {
				auto session = m_tls_configuration->Create();
				if (!session)
					m_logger << Logger::Level::Error << "Remote-file TLS setup failed: "
						<< Crypto::TLS::DescribeSessionError(*session.error()) << std::endl;
				if (!session || !socket->StartTLS(std::move(session.value()), true)) {
					socket->Disconnect();
					return {};
				}
			}
			Safe::String local_address = socket->LocalAddress();
			if (local_address.empty()) {
				socket->Disconnect();
				return {};
			}
			auto device = Detail::RemoteFile::CreateNetworkDevice(local_address);
			plane = Safe::MakeShared<Detail::RemoteFile::DataPlane>(std::move(socket),
				Buffer::Pipeline{}, Buffer::Pipeline{}, mount.MaximumTimeoutSeconds(), m_logger,
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
	std::scoped_lock operation(m_operation_mutex);
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
			auto socket = Socket::Client::Create(m_protocol, m_logger);
			if (!socket || !socket->Connect(m_remote_address, mount.Port()))
				return {};
			if (m_tls_configuration) {
				auto session = m_tls_configuration->Create();
				if (!session)
					m_logger << Logger::Level::Error << "Remote-file TLS setup failed: "
						<< Crypto::TLS::DescribeSessionError(*session.error()) << std::endl;
				if (!session || !socket->StartTLS(std::move(session.value()), true)) {
					socket->Disconnect();
					return {};
				}
			}
			Safe::String local_address = socket->LocalAddress();
			if (local_address.empty()) {
				socket->Disconnect();
				return {};
			}
			auto device = Detail::RemoteFile::CreateNetworkDevice(local_address);
			plane = Safe::MakeShared<Detail::RemoteFile::DataPlane>(std::move(socket),
				Buffer::Pipeline{}, Buffer::Pipeline{}, mount.MaximumTimeoutSeconds(), m_logger,
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

#include <StormByte/network/discovery_transport.hxx>

bool Client::Discover(const Connection::Protocol& protocol, unsigned short discovery_port,
	std::chrono::milliseconds timeout,
	StormByte::Safe::Function<bool(const DiscoveryReply&)> on_reply) noexcept {
	try {
		const Safe::Binary query = MakeDiscoveryQuery();
		if (query.empty() || !on_reply.HasValue())
			return false;

		Detail::DiscoveryTransport transport(protocol);
		if (!transport.Open() || !transport.SendQuery(query, discovery_port))
			return false;

		const auto deadline = std::chrono::steady_clock::now() + timeout;
		while (std::chrono::steady_clock::now() < deadline) {
			const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
				deadline - std::chrono::steady_clock::now());
			if (remaining.count() <= 0)
				break;
			DiscoveryReply reply;
			if (!transport.Receive(remaining, reply))
				continue;
			bool stop = false;
			if (on_reply.Call(stop, reply) != Safe::Status::Success)
				continue;
			if (stop)
				break;
		}
		return true;
	} catch (...) {
		return false;
	}
}

Safe::Binary Client::MakeDiscoveryQuery() const noexcept {
	return {};
}

bool Client::InterpretDiscoveryReply(const Safe::Binary&, Safe::String&, unsigned short&) const noexcept {
	return false;
}
