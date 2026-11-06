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

#include <StormByte/network/connection/handler.hxx>
#include <StormByte/network/socket/client.hxx>
#include <StormByte/network/socket/tls.hxx>
#include <StormByte/safe/unique_lock.hxx>

#include <algorithm>
#include <array>
#include <chrono>
#include <span>
#include <utility>

#ifdef UNIX
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>
#else
#include <winsock2.h>
#include <ws2tcpip.h>
#endif

constexpr StormByte::ByteSize MAX_SINGLE_IO{4 * 1024 * 1024};
constexpr StormByte::ByteSize DEFAULT_IO_CHUNK{64 * 1024};
using namespace StormByte;
using namespace StormByte::Logger;
using namespace StormByte::Network;
namespace {
	/**
	 * @brief Bound one I/O operation to the configured capacity and remaining bytes.
	 * @param preferred Configured socket capacity, or zero for the default.
	 * @param remaining Remaining byte count, or zero when uncapped.
	 * @return Nonzero byte count no larger than the per-call limit.
	 */
	ByteSize ClampChunk(ByteSize preferred, ByteSize remaining) noexcept {
		if (preferred == 0)
			preferred = DEFAULT_IO_CHUNK;
		preferred = std::min(preferred, MAX_SINGLE_IO);
		if (remaining > 0)
			preferred = std::min(preferred, remaining);
		return std::max(preferred, ByteSize{1});
	}

	/**
	 * @brief Wait for a non-blocking socket to become writable.
	 * @param handle Native socket handle.
	 * @return 1 when writable, 0 on timeout, or -1 on error.
	 */
	int WaitForWritable(Connection::HandlerType handle) noexcept {
#ifdef UNIX
		pollfd pfd{ .fd = handle, .events = POLLOUT, .revents = 0 };
		const int result = poll(&pfd, 1, 50);
		if (result <= 0)
			return result;
		return (pfd.revents & (POLLOUT | POLLERR | POLLHUP)) != 0 ? 1 : 0;
#else
		fd_set write_fds;
		FD_ZERO(&write_fds);
		FD_SET(handle, &write_fds);
		TIMEVAL timeout{ .tv_sec = 0, .tv_usec = 50000 };
		const int result = select(0, nullptr, &write_fds, nullptr, &timeout);
		if (result <= 0)
			return result == SOCKET_ERROR ? -1 : 0;
		return FD_ISSET(handle, &write_fds) ? 1 : 0;
#endif
	}
}

Socket::Client::Client(const Connection::Protocol& protocol, StormByte::Safe::Shared<Logger::Log> logger)
	: Socket(protocol, std::move(logger)) {
	m_logger << Logger::Level::LowLevel << "Created client socket with UUID: " << std::string_view{m_UUID} << std::endl;
}

Socket::Client::Client(Client&& other) noexcept:
	Socket(std::move(other)), m_peer_address(std::move(other.m_peer_address)), m_tls(std::move(other.m_tls)),
	m_tls_enabled(std::exchange(other.m_tls_enabled, false)),
	m_tls_buffered(std::exchange(other.m_tls_buffered, false)),
	m_tls_pending(std::exchange(other.m_tls_pending, false)),
	m_tls_output(std::move(other.m_tls_output)), m_tls_offset(other.m_tls_offset),
	m_tls_deadline(other.m_tls_deadline) {}

Socket::Client::~Client() noexcept = default;

Socket::Client& Socket::Client::operator=(Client&& other) noexcept {
	if (this != &other) {
		Socket::operator=(std::move(other));
		m_peer_address = std::move(other.m_peer_address);
		m_tls = std::move(other.m_tls);
		m_tls_enabled = std::exchange(other.m_tls_enabled, false);
		m_tls_buffered = std::exchange(other.m_tls_buffered, false);
		m_tls_pending = std::exchange(other.m_tls_pending, false);
		m_tls_output = std::move(other.m_tls_output);
		m_tls_offset = other.m_tls_offset;
		m_tls_deadline = other.m_tls_deadline;
	}
	return *this;
}

void Socket::Client::SetPeerAddress(PeerAddress address) noexcept {
	m_peer_address = std::move(address);
}

const PeerAddress& Socket::Client::RemotePeerAddress() const noexcept {
	return m_peer_address;
}

Socket::Client::Pointer Socket::Client::Create(const Connection::Protocol& protocol, Safe::Shared<Logger::Log> logger) noexcept {
	try {
		return Safe::MakeShared<Client>(protocol, std::move(logger));
	} catch (...) {
		return {};
	}
}

bool Socket::Client::StartTLS(Crypto::TLS::Session session, bool synchronous) noexcept {
	if (m_tls_enabled || session.Status() != Crypto::TLS::State::Handshaking)
		return false;
	m_tls_enabled = true;
	m_tls_pending = true;
	m_tls = std::move(session);
	m_tls_deadline = std::chrono::steady_clock::now() + std::chrono::seconds{10};
	try {
		if (synchronous) {
			const auto result = TLS::Handshake(*this, m_tls, {m_tls_deadline});
			if (!result.detail.empty())
				m_logger << Logger::Level::Error << "TLS handshake failed: " << std::string_view(result.detail) << std::endl;
			if (result.state != OperationState::Complete)
				return false;
			m_tls_pending = false;
		}
		else if (!PumpTLS(false))
			return false;
		return true;
	} catch (...) {
		m_logger << Logger::Level::Error << "Failed to initialize TLS handshake." << std::endl;
		Disconnect();
		return false;
	}
}

bool Socket::Client::TLSReady() const noexcept {
	return !m_tls_enabled || (m_tls.HandshakeComplete() && m_tls.Status() == Crypto::TLS::State::Open);
}

Socket::OperationResult Socket::Client::StartTLS(Crypto::TLS::Session session, const OperationControl& control) {
	Safe::String detail;
	const auto result = StartTLS(std::move(session), control, detail);
	if (!detail.empty())
		m_logger << Logger::Level::Error << "TLS handshake failed: " << std::string_view(detail) << std::endl;
	return result;
}

Socket::OperationResult Socket::Client::StartTLS(Crypto::TLS::Session session, const OperationControl& control,
	Safe::String& detail) {
	detail.clear();
	if (m_tls_enabled || session.Status() != Crypto::TLS::State::Handshaking)
		return {OperationState::Error};
	m_tls_enabled = true;
	m_tls_pending = true;
	m_tls = std::move(session);
	m_tls_deadline = control.deadline;
	const auto result = TLS::Handshake(*this, m_tls, control);
	detail = result.detail;
	if (result.state == OperationState::Complete)
		m_tls_pending = false;
	return {result.state, result.bytes, result.native_error};
}

Socket::OperationResult Socket::Client::SendApplication(std::span<const std::byte> data,
	const OperationControl& control) {
	if (!m_tls_enabled)
		return Send(data, control);
	const auto result = TLS::Write(*this, m_tls, data, control);
	if (!result.detail.empty())
		m_logger << Logger::Level::Error << "TLS write failed: " << std::string_view(result.detail) << std::endl;
	return {result.state, result.bytes, result.native_error};
}

Socket::OperationResult Socket::Client::ReceiveApplication(ByteSize size, Safe::Binary& out,
	const OperationControl& control) {
	std::array<std::byte, 16384> bytes{};
	ByteSize total{0};
	while (total < size) {
		const auto state = control.Check();
		if (state != OperationState::Complete)
			return {state, total};
		const auto count = static_cast<std::size_t>(std::min(size - total, ByteSize{bytes.size()}));
		OperationResult result;
		if (m_tls_enabled) {
			const auto read = TLS::Read(*this, m_tls, std::span{bytes}.first(count), control);
			if (!read.detail.empty())
				m_logger << Logger::Level::Error << "TLS read failed: " << std::string_view(read.detail) << std::endl;
			result = {read.state, read.bytes, read.native_error};
		}
		else {
			result = ReadSome(std::span{bytes}.first(count));
			if (result.state == OperationState::WouldBlock) {
				result = WaitReadable(m_handle, control);
				if (result.state == OperationState::Complete)
					continue;
			}
		}
		if (result.state != OperationState::Complete)
			return {result.state, total, result.native_error};
		if (result.bytes == 0)
			return {OperationState::PeerClosed, total};
		out.append(std::span{bytes}.first(static_cast<std::size_t>(result.bytes)));
		total += result.bytes;
	}
	return {control.Check(), total};
}

bool Socket::Client::TLSHasOutput() const noexcept {
	return m_tls_enabled && (!m_tls_output.empty() || m_tls_pending);
}

bool Socket::Client::TLSExpired() const noexcept {
	return m_tls_enabled && !m_tls.HandshakeComplete() && std::chrono::steady_clock::now() >= m_tls_deadline;
}

bool Socket::Client::TLSBuffered() const noexcept {
	return m_tls_enabled && m_tls_buffered;
}

bool Socket::Client::UsingTLS() const noexcept {
	return m_tls_enabled;
}

bool Socket::Client::TLSFailure(Crypto::TLS::SessionError error) noexcept {
	m_logger << Logger::Level::Error << "TLS transport failed: "
		<< TLSFailureDetail(error) << std::endl;
	return false;
}

std::string_view Socket::Client::TLSFailureDetail(Crypto::TLS::SessionError error) const noexcept {
	const auto detail = m_tls.FailureDetail();
	return detail.empty() ? std::string_view{Crypto::TLS::DescribeSessionError(error)} : detail;
}

bool Socket::Client::PumpTLS(bool readable) noexcept {
	if (!m_tls_enabled)
		return true;
	try {
		if (TLSExpired())
			return false;
		if (m_tls.Status() == Crypto::TLS::State::Failed)
			return TLSFailure(m_tls.Failure());
		if (readable) {
			std::array<std::byte, 16384> encrypted{};
			const auto received = ReadSome(encrypted);
			if (received.state == OperationState::Complete) {
				auto input = std::span<const std::byte>(encrypted).first(static_cast<std::size_t>(received.bytes));
				auto fed = m_tls.FeedEncrypted(input);
				if (!fed)
					return TLSFailure(*fed.error());
				if (*fed != input.size())
					return TLSFailure(Crypto::TLS::SessionError::BufferLimit);
				m_tls_buffered = m_tls.HandshakeComplete();
				m_tls_pending = true;
			}
			else if (received.state != OperationState::WouldBlock)
				return false;
		}
		if (m_tls_output.empty()) {
			m_tls_output.resize(ByteSize{16384});
			auto drained = m_tls.DrainEncrypted(m_tls_output.span());
			if (!drained)
				return TLSFailure(*drained.error());
			m_tls_output.resize(ByteSize{*drained});
			m_tls_pending = *drained != 0;
			m_tls_offset = 0;
		}
		if (!m_tls_output.empty()) {
			const auto remaining = m_tls_output.span().subspan(static_cast<std::size_t>(m_tls_offset));
#ifdef LINUX
			const int flags = MSG_NOSIGNAL;
#else
			const int flags = 0;
#endif
			const int written = static_cast<int>(::send(m_handle,
				reinterpret_cast<const char*>(remaining.data()), static_cast<int>(remaining.size()), flags));
			if (written > 0) {
				m_tls_offset += ByteSize{written};
				if (m_tls_offset == m_tls_output.size())
					m_tls_output.clear();
			}
			else {
				const int error = Connection::Handler::Instance().LastErrorCode();
#ifdef WINDOWS
				if (error != WSAEWOULDBLOCK && error != WSAEINTR)
#else
				if (error != EAGAIN && error != EWOULDBLOCK && error != EINTR)
#endif
					return false;
			}
		}
		if (m_tls.Status() == Crypto::TLS::State::Failed)
			return TLSFailure(m_tls.Failure());
		return m_tls.Status() == Crypto::TLS::State::Handshaking || m_tls.Status() == Crypto::TLS::State::Open;
	} catch (...) {
		m_logger << Logger::Level::Error << "Failed to advance incremental TLS transport." << std::endl;
		return false;
	}
}

ExpectedVoid Socket::Client::Connect(std::string_view hostname, const unsigned short& port) noexcept {
	m_logger << Logger::Level::LowLevel << "Connecting to " << std::string_view{hostname} << ":" << port << std::endl;
	if (m_status.load(Safe::MemoryOrder::Acquire) != Connection::Status::Disconnected) {
		m_logger << Logger::Level::Error << "Client is already connected" << std::endl;
		return Unexpected<ConnectionError>("Client is already connected");
	}

	EnsureIsClosed();
	auto fail = [this](ExpectedVoid error) noexcept -> ExpectedVoid {
		EnsureIsClosed();
		m_conn_info.reset();
		m_status.store(Connection::Status::Disconnected, Safe::MemoryOrder::Release);
		return error;
	};
	try {
		m_status.store(Connection::Status::Connecting, Safe::MemoryOrder::Release);
		auto expected_socket = CreateSocket();
		if (!expected_socket) {
			m_logger << Logger::Level::Error << "Failed to create socket: " << expected_socket.error()->what() << std::endl;
			return fail(Unexpected<ConnectionError>(expected_socket.error()->what()));
		}

		m_handle = expected_socket.value();
		auto expected_conn_info = Connection::Info::FromHost(hostname, port, m_protocol);
		if (!expected_conn_info) {
			m_logger << Logger::Level::Error << "Failed to resolve host: " << expected_conn_info.error()->what() << std::endl;
			return fail(Unexpected<ConnectionError>(expected_conn_info.error()->what()));
		}

		m_conn_info = Safe::MakeUnique<Connection::Info>(std::move(expected_conn_info.value()));
		if (::connect(m_handle, m_conn_info->SockAddr().get(),
#ifdef WINDOWS
			static_cast<int>(m_conn_info->SockAddrSize())) == SOCKET_ERROR) {
#else
			static_cast<socklen_t>(m_conn_info->SockAddrSize())) == -1) {
#endif
			m_logger << Logger::Level::Error << "Failed to connect: " << std::string_view{Connection::Handler::Instance().LastError()} << std::endl;
			return fail(Unexpected<ConnectionError>("{}", std::string_view{Connection::Handler::Instance().LastError()}));
		}

		InitializeAfterConnect();
		m_logger << Logger::Level::LowLevel << "Successfully connected to " << hostname << ":" << port << std::endl;
		return {};
	} catch (...) {
		return fail(Unexpected<ConnectionError>("Failed to allocate connection state"));
	}
}

ExpectedVoid Socket::Client::Send(const Buffer::FIFO& buffer) noexcept {
	return Send(buffer.Data().span());
}

Socket::OperationResult Socket::Client::Connect(std::string_view hostname,
	unsigned short port, Connection::Resolver& resolver, const OperationControl& control) noexcept {
	if (m_protocol != Connection::Protocol::IPv4 && m_protocol != Connection::Protocol::IPv6)
		return {OperationState::Error};
	return Connect(hostname, port, resolver, control, m_protocol == Connection::Protocol::IPv4
		? Connection::AddressPolicy::IPv4Only : Connection::AddressPolicy::IPv6Only);
}

Socket::OperationResult Socket::Client::Connect(std::string_view hostname, unsigned short port,
	Connection::Resolver& resolver, const OperationControl& control, Connection::AddressPolicy policy) noexcept {
	if (m_status.load(Safe::MemoryOrder::Acquire) != Connection::Status::Disconnected)
		return {OperationState::Error};
	OperationState state;
	auto addresses = resolver.Resolve(hostname, port, policy, control, state);
	if (!addresses)
		return {state};
	return ConnectResolved(addresses.value(), control);
}

Socket::OperationResult Socket::Client::ConnectResolved(
	std::span<const Safe::Shared<const sockaddr>> addresses, const OperationControl& control) noexcept {
	if (m_status.load(Safe::MemoryOrder::Acquire) != Connection::Status::Disconnected)
		return {OperationState::Error};
	const auto original_protocol = m_protocol;
	auto fail = [&](OperationResult result) noexcept {
		EnsureIsClosed();
		m_conn_info.reset();
		m_protocol = original_protocol;
		m_status.store(Connection::Status::Disconnected, Safe::MemoryOrder::Release);
		return result;
	};
	EnsureIsClosed();
	m_conn_info.reset();
	OperationResult last{OperationState::Error};
	try {
		m_status.store(Connection::Status::Connecting, Safe::MemoryOrder::Release);
		for (const auto& address: addresses) {
			const auto state = control.Check();
			if (state != OperationState::Complete)
				return fail({state});
			EnsureIsClosed();
			auto info = Connection::Info::FromSockAddr(address);
			if (!info) {
				m_logger << Logger::Level::Error << "Invalid resolved address: " << info.error()->what() << std::endl;
				continue;
			}
			m_protocol = address->sa_family == AF_INET ? Connection::Protocol::IPv4 : Connection::Protocol::IPv6;
			auto handle = CreateSocket();
			if (!handle) {
				last = {OperationState::Error, ByteSize{0}, Connection::Handler::Instance().LastErrorCode()};
				m_logger << Logger::Level::Error << "Failed to create resolved socket: " << handle.error()->what() << std::endl;
				continue;
			}
			m_handle = handle.value();
			m_status.store(Connection::Status::Connecting, Safe::MemoryOrder::Release);
#ifdef WINDOWS
			u_long mode = 1;
			if (ioctlsocket(m_handle, FIONBIO, &mode) != 0) {
				last = {OperationState::Error, ByteSize{0}, WSAGetLastError()};
				continue;
			}
#else
			const int flags = fcntl(m_handle, F_GETFL, 0);
			if (flags < 0 || fcntl(m_handle, F_SETFL, flags | O_NONBLOCK) != 0) {
				last = {OperationState::Error, ByteSize{0}, errno};
				continue;
			}
#endif
			const int connected = ::connect(m_handle, info->SockAddr().get(),
#ifdef WINDOWS
				static_cast<int>(info->SockAddrSize()));
#else
				static_cast<socklen_t>(info->SockAddrSize()));
#endif
			if (connected != 0) {
				const int error = Connection::Handler::Instance().LastErrorCode();
#ifdef WINDOWS
				const bool pending = error == WSAEWOULDBLOCK || error == WSAEINPROGRESS || error == WSAEINTR;
#else
				const bool pending = error == EINPROGRESS || error == EWOULDBLOCK || error == EINTR;
#endif
				if (!pending) {
					last = {OperationState::Error, ByteSize{0}, error};
					continue;
				}
				last = WaitWritable(m_handle, control);
				if (last.state == OperationState::TimedOut || last.state == OperationState::Cancelled)
					return fail(last);
				if (last.state != OperationState::Complete)
					continue;
			}
			int socket_error = 0;
#ifdef WINDOWS
			int length = sizeof(socket_error);
#else
			socklen_t length = sizeof(socket_error);
#endif
			if (getsockopt(m_handle, SOL_SOCKET, SO_ERROR, reinterpret_cast<char*>(&socket_error), &length) != 0) {
				last = {OperationState::Error, ByteSize{0}, Connection::Handler::Instance().LastErrorCode()};
				continue;
			}
			if (socket_error != 0) {
				last = {OperationState::Error, ByteSize{0}, socket_error};
				continue;
			}
			const auto before_initialization = control.Check();
			if (before_initialization != OperationState::Complete)
				return fail({before_initialization});
			m_conn_info = Safe::MakeUnique<Connection::Info>(std::move(info.value()));
			InitializeAfterConnect();
			m_protocol = original_protocol;
			const auto after_initialization = control.Check();
			if (after_initialization != OperationState::Complete)
				return fail({after_initialization});
			return {};
		}
		if (last.state == OperationState::Error)
			m_logger << Logger::Level::Error << "All resolved connection attempts failed; last native error: "
				<< last.native_error << std::endl;
		return fail(last);
	} catch (...) {
		return fail({OperationState::Error});
	}
}

ExpectedVoid Socket::Client::Send(const Safe::Binary& data) noexcept {
	return Send(data.span());
}

ExpectedVoid Socket::Client::Send(std::span<const std::byte> data) noexcept {
	if (m_tls_enabled) {
		try {
			while (!data.empty()) {
				bool blocked = false;
				auto written = TryWrite(data, blocked);
				if (!written)
					return Unexpected(written.error());
				data = data.subspan(static_cast<std::size_t>(*written));
				if (blocked && WaitForWritable(m_handle) < 0)
					return Unexpected<ConnectionError>("TLS write wait failed");
			}
			for (;;) {
				{
					Safe::UniqueLock lock(m_tls_mutex);
					if (!PumpTLS(false))
						return Unexpected<ConnectionError>("TLS output failed");
					if (!TLSHasOutput())
						return {};
				}
				if (WaitForWritable(m_handle) < 0)
					return Unexpected<ConnectionError>("TLS output wait failed");
			}
		} catch (...) {
			m_logger << Logger::Level::Error << "Failed to send TLS application bytes." << std::endl;
			Disconnect();
		}
		return Unexpected<ConnectionError>("TLS write failed");
	}
	if (m_status.load(Safe::MemoryOrder::Acquire) != Connection::Status::Connected) {
		return Unexpected<ConnectionError>("Failed to send: Client is not connected");
	}

	if (!HasHandle()) {
		return Unexpected<ConnectionError>("Failed to send: Invalid socket handle");
	}

	ByteSize total_bytes_sent{0};
	const ByteSize preferred = (m_effective_send_buf > 0)
		? m_effective_send_buf
		: DEFAULT_IO_CHUNK;
	while (!data.empty()) {
		const ByteSize chunk_size = ClampChunk(preferred, ByteSize{data.size()});
		std::span<const std::byte> chunk = data.subspan(0, static_cast<std::size_t>(chunk_size));
#ifdef LINUX
		const int send_flags = MSG_NOSIGNAL;
		const ssize_t written = ::send(m_handle,
#else
		const int send_flags = 0;
		const int written = ::send(m_handle,
#endif
			reinterpret_cast<const char*>(chunk.data()),
			static_cast<int>(chunk.size()), send_flags);
		if (written <= 0) {
#ifdef WINDOWS
			const int wsa = Connection::Handler::Instance().LastErrorCode();
			if (wsa == WSAEWOULDBLOCK) {
				const int wait_result = WaitForWritable(m_handle);
				if (wait_result > 0)
					continue;
				if (wait_result == 0)
					continue;
			}
#else
			if (errno == EAGAIN || errno == EWOULDBLOCK) {
				const int wait_result = WaitForWritable(m_handle);
				if (wait_result >= 0)
					continue;
			}
#endif
			int sys_errno = errno;
			m_logger << Logger::Level::Error << "Send failed: " << std::string_view{Connection::Handler::Instance().LastError()}
					<< " (code: " << Connection::Handler::Instance().LastErrorCode() << ")"
					<< " errno: " << sys_errno << " (" << std::string_view{Connection::Handler::Instance().ErrnoToString(sys_errno)} << ")" << std::endl;
			return Unexpected<ConnectionError>(
				"Failed to write: {} (error code: {})",
				std::string_view{Connection::Handler::Instance().LastError()},
				Connection::Handler::Instance().LastErrorCode());
		}

		total_bytes_sent += ByteSize{written};
		data = data.subspan(static_cast<std::size_t>(written));
	}

	m_logger << Logger::Level::LowLevel << "All data sent successfully! Total bytes sent: "
			<< humanreadable_bytes << total_bytes_sent << nohumanreadable << std::endl;
	return {};
}

ExpectedVoid Socket::Client::Send(Buffer::Consumer data) noexcept {
	if (m_status.load(Safe::MemoryOrder::Acquire) != Connection::Status::Connected) {
		return Unexpected<ConnectionError>("Failed to send: Client is not connected");
	}

	if (!HasHandle()) {
		return Unexpected<ConnectionError>("Failed to send: Invalid socket handle");
	}

	while (true) {
		Safe::Binary byte_data;
		if (!data.Extract(ByteSize{0}, byte_data) || byte_data.empty()) {
			if (data.EoF())
				break;
			continue;
		}

		auto expected_send = Send(byte_data);
		if (!expected_send) {
			return Unexpected(expected_send.error());
		}
	}

	return {};
}

Socket::OperationResult Socket::Client::Send(
	std::span<const std::byte> data, const OperationControl& control) noexcept {
	ByteSize total{0};
	if (m_status.load(Safe::MemoryOrder::Acquire) != Connection::Status::Connected || !HasHandle())
		return {OperationState::Error};
	while (!data.empty()) {
		const auto state = control.Check();
		if (state != OperationState::Complete)
			return {state, total};
#ifdef LINUX
		const int flags = MSG_NOSIGNAL;
#else
		const int flags = 0;
#endif
		const int written = static_cast<int>(::send(m_handle,
			reinterpret_cast<const char*>(data.data()),
			static_cast<int>(ClampChunk(m_effective_send_buf, ByteSize{data.size()})), flags));
		if (written > 0) {
			total += ByteSize{written};
			data = data.subspan(static_cast<std::size_t>(written));
			continue;
		}
		if (written == 0)
			return {OperationState::PeerClosed, total};
		const int error = Connection::Handler::Instance().LastErrorCode();
#ifdef WINDOWS
		if (error == WSAEINTR)
			continue;
		const bool blocked = error == WSAEWOULDBLOCK;
#else
		if (error == EINTR)
			continue;
		const bool blocked = error == EAGAIN || error == EWOULDBLOCK;
#endif
		if (!blocked)
			return {OperationState::Error, total, error};
		auto result = WaitWritable(m_handle, control);
		if (result.state != OperationState::Complete) {
			result.bytes = total;
			return result;
		}
	}
	return {OperationState::Complete, total};
}

Socket::OperationResult Socket::Client::ReadSome(std::span<std::byte> data) noexcept {
	if (data.empty() || m_status.load(Safe::MemoryOrder::Acquire) != Connection::Status::Connected || !HasHandle())
		return {OperationState::Error};
	const int received = static_cast<int>(::recv(m_handle,
		reinterpret_cast<char*>(data.data()),
		static_cast<int>(ClampChunk(m_effective_recv_buf, ByteSize{data.size()})), 0));
	if (received > 0)
		return {OperationState::Complete, ByteSize{received}};
	if (received == 0)
		return {OperationState::PeerClosed};
	const int error = Connection::Handler::Instance().LastErrorCode();
#ifdef WINDOWS
	if (error == WSAEWOULDBLOCK)
#else
	if (error == EAGAIN || error == EWOULDBLOCK)
#endif
		return {OperationState::WouldBlock};
	return {OperationState::Error, ByteSize{0}, error};
}

bool Socket::Client::HasShutdownRequest() noexcept {
	char buffer[1];
#ifdef UNIX
	ssize_t result = ::recv(m_handle, buffer, sizeof(buffer), MSG_PEEK | MSG_DONTWAIT);
#else
	int result = ::recv(m_handle, buffer, sizeof(buffer), MSG_PEEK);
	if (result == SOCKET_ERROR) {
		if (Connection::Handler::Instance().LastErrorCode() == WSAEWOULDBLOCK) {
			return false;
		} else {
			return true;
		}
	}
#endif
	if (result == 0) {
		return true;
	} else if (result < 0) {
#ifdef UNIX
		if (Connection::Handler::Instance().LastErrorCode() == EAGAIN ||
			Connection::Handler::Instance().LastErrorCode() == EWOULDBLOCK) {
			return false;
		} else {
			return true;
		}
#endif
	}

	return false;
}

ExpectedBuffer Socket::Client::Receive(const ByteSize& max_size) noexcept {
	return Receive(max_size, static_cast<unsigned short>(0));
}

ExpectedBuffer Socket::Client::Peek(const ByteSize& size) const noexcept {
	return const_cast<Client*>(this)->ReadOnce(size, MSG_PEEK);
}

ExpectedBuffer Socket::Client::ReadOnce(const ByteSize& size, int flags) noexcept {
	if (m_tls_enabled)
		return Unexpected<ConnectionError>("TLS does not support raw peek");
	if (size == 0) {
		return Unexpected<ConnectionError>("Read failed: size must be greater than 0");
	}

	if (!HasHandle()) {
		return Unexpected<ConnectionError>("Read failed: Invalid socket handle");
	}

	const ByteSize preferred = (m_effective_recv_buf > 0)
		? m_effective_recv_buf
		: DEFAULT_IO_CHUNK;
	const ByteSize bytes_to_read = ClampChunk(preferred, size);
	Safe::Binary internal_buffer;
	try {
		internal_buffer.resize(bytes_to_read);
	} catch (...) {
		return Unexpected<ConnectionError>("Read failed: unable to allocate receive bytes");
	}
#ifdef UNIX
	const ssize_t valread = ::recv(m_handle, internal_buffer.data(), static_cast<std::size_t>(bytes_to_read), flags);
#else
	const int valread = ::recv(m_handle, reinterpret_cast<char*>(internal_buffer.data()), static_cast<int>(bytes_to_read), flags);
#endif
	if (valread > 0) {
		Buffer::FIFO buffer;
		if (!buffer.Write(internal_buffer.span().first(static_cast<std::size_t>(valread))))
			return Unexpected<ConnectionError>("Read failed: unable to retain received bytes");
		return buffer;
	} else if (valread == 0) {
		return Unexpected<ConnectionError>("Read failed: connection closed by peer");
	} else {
#ifdef UNIX
		if (Connection::Handler::Instance().LastErrorCode() == EAGAIN ||
			Connection::Handler::Instance().LastErrorCode() == EWOULDBLOCK) {
			return Unexpected<ConnectionError>("Read would block: no data available");
		}
#else
		if (Connection::Handler::Instance().LastErrorCode() == WSAEWOULDBLOCK) {
			return Unexpected<ConnectionError>("Read would block: no data available");
		}
#endif
		return Unexpected<ConnectionError>("Read failed: {}", std::string_view{Connection::Handler::Instance().LastError()});
	}
}

ExpectedVoid Socket::Client::ReceiveLoop(const ByteSize& max_size, Safe::Binary& out,
	const unsigned short& timeout_seconds, bool require_exact) noexcept {
	if (m_tls_enabled) {
		try {
			OperationControl control;
			if (timeout_seconds != 0)
				control.deadline = std::chrono::steady_clock::now() + std::chrono::seconds{timeout_seconds};
			ByteSize total{0};
			if (require_exact && max_size == 0)
				return {};
			for (;;) {
				if (control.Check() != OperationState::Complete)
					return Unexpected<ConnectionError>("TLS read timed out");
				bool blocked = false;
				auto read = TryRead(blocked, max_size > 0 ? max_size - total : ByteSize{16384});
				if (!read)
					return Unexpected(read.error());
				if (blocked) {
					OperationControl step{std::min(control.deadline,
						std::chrono::steady_clock::now() + std::chrono::milliseconds{50})};
					auto waited = WaitReadable(m_handle, step);
					if (waited.state != OperationState::Complete && waited.state != OperationState::TimedOut)
						return Unexpected<ConnectionError>("TLS read wait failed");
					continue;
				}
				out.append(read->span());
				total += read->size();
				if (max_size == 0 || total >= max_size)
					break;
			}
			return {};
		} catch (...) {
			m_logger << Logger::Level::Error << "Failed to receive TLS application bytes." << std::endl;
			Disconnect();
			return Unexpected<ConnectionError>("TLS read failed");
		}
	}
	if (!HasHandle()) {
		return Unexpected<ConnectionError>("Receive failed: Invalid socket handle");
	}

	if (require_exact && max_size == 0) {
		return {};
	}

	const ByteSize preferred = (m_effective_recv_buf > 0)
		? m_effective_recv_buf
		: DEFAULT_IO_CHUNK;
	ByteSize total_bytes_read{0};
	const auto start_time = std::chrono::steady_clock::now();
	const ByteSize buf_cap = ClampChunk(preferred, max_size > 0 ? max_size : MAX_SINGLE_IO);
	Safe::Binary internal_buffer;
	try {
		if (max_size > 0)
			out.reserve(out.size() + max_size);
		internal_buffer.resize(buf_cap);
	} catch (...) {
		return Unexpected<ConnectionError>("Receive failed: unable to allocate receive bytes");
	}
	auto timed_out = [&]() -> bool {
		if (timeout_seconds == 0) {
			return false;
		}

		const auto now = std::chrono::steady_clock::now();
		return std::chrono::duration_cast<std::chrono::seconds>(now - start_time).count() >= timeout_seconds;
	};
	while (true) {
		if (max_size > 0 && total_bytes_read >= max_size) {
			break;
		}

		const ByteSize remaining = (max_size > 0) ? (max_size - total_bytes_read) : buf_cap;
		const ByteSize bytes_to_read = ClampChunk(preferred, remaining);
#ifdef UNIX
		const ssize_t valread = recv(m_handle, internal_buffer.data(), static_cast<std::size_t>(bytes_to_read), 0);
#else
		const int valread = recv(m_handle, reinterpret_cast<char*>(internal_buffer.data()), static_cast<int>(bytes_to_read), 0);
#endif
		if (valread > 0) {
			m_logger << Logger::Level::Debug << "Chunk received. Size: "
					<< humanreadable_bytes << valread << nohumanreadable << std::endl;
			try {
				out.append(internal_buffer.span().first(static_cast<std::size_t>(valread)));
			} catch (...) {
				return Unexpected<ConnectionError>("Receive failed: unable to retain received bytes");
			}
			total_bytes_read += ByteSize{valread};
			continue;
		}

		if (valread == 0) {
			m_logger << Logger::Level::Debug << "Connection closed by peer. Exiting read loop." << std::endl;
			if (require_exact && total_bytes_read < max_size) {
				return Unexpected<ConnectionError>(
					"Receive failed: connection closed by peer (got {} of {} bytes)",
					static_cast<std::size_t>(total_bytes_read), static_cast<std::size_t>(max_size));
			}

			break;
		}
#ifdef WINDOWS
		if (Connection::Handler::Instance().LastErrorCode() == WSAEWOULDBLOCK) {
#else
		if (Connection::Handler::Instance().LastErrorCode() == EAGAIN ||
			Connection::Handler::Instance().LastErrorCode() == EWOULDBLOCK) {
#endif
			if (timed_out()) {
				return Unexpected<ConnectionError>("Receive timed out");
			}

			auto wait_res = WaitForData(100000);
			if (!wait_res) {
				if (require_exact) {
					return Unexpected<ConnectionError>("Receive failed: wait error");
				}

				break;
			}

			if (wait_res.value() == Connection::Read::Result::Timeout) {
				if (timed_out()) {
					return Unexpected<ConnectionError>("Receive timed out");
				}

				if (!require_exact && max_size == 0 && total_bytes_read > 0) {
					break;
				}

				continue;
			}

			continue;
		}

		return Unexpected<ConnectionError>("Receive failed: {}", std::string_view{Connection::Handler::Instance().LastError()});
	}

	m_logger << Logger::Level::LowLevel << "Total data received: "
			<< humanreadable_bytes << total_bytes_read << nohumanreadable << std::endl;
	return {};
}

ExpectedBuffer Socket::Client::Receive(const ByteSize& max_size, const unsigned short& timeout_seconds) noexcept {
	m_logger << Logger::Level::LowLevel << "Starting to read data with max_size: "
			<< humanreadable_bytes << max_size << nohumanreadable << std::endl;
	Safe::Binary bytes;
	auto loop = ReceiveLoop(max_size, bytes, timeout_seconds, false);
	if (!loop) {
		return Unexpected(loop.error());
	}

	Buffer::FIFO buffer;
	if (!bytes.empty()) {
		if (!buffer.Write(bytes.span()))
			return Unexpected<ConnectionError>("Receive failed: unable to retain received bytes");
	}

	return buffer;
}

ExpectedVoid Socket::Client::ReceiveInto(const ByteSize& max_size, Safe::Binary& out,
	const unsigned short& timeout_seconds) noexcept {
	m_logger << Logger::Level::LowLevel << "Starting ReceiveInto with max_size: "
			<< humanreadable_bytes << max_size << nohumanreadable << std::endl;
	return ReceiveLoop(max_size, out, timeout_seconds, true);
}

ExpectedVoid Socket::Client::Write(std::span<const std::byte> data, const ByteSize& size) noexcept {
	if (m_tls_enabled)
		return Send(data.first(static_cast<std::size_t>(std::min(size, ByteSize{data.size()}))));
	m_logger << Logger::Level::LowLevel << "Starting to write data..." << std::endl;
	if (m_status.load(Safe::MemoryOrder::Acquire) != Connection::Status::Connected) {
		m_logger << Logger::Level::LowLevel << "Failed to write: Client is not connected" << std::endl;
		return Unexpected<ConnectionError>("Failed to write: Client is not connected");
	}

	ByteSize bytes_to_write = std::min(size, ByteSize{data.size()});
	ByteSize total_written{0};
	const ByteSize preferred = (m_effective_send_buf > 0)
		? m_effective_send_buf
		: DEFAULT_IO_CHUNK;
	while (total_written < bytes_to_write) {
		auto current_data = data.subspan(static_cast<std::size_t>(total_written));
		ByteSize to_write = ClampChunk(preferred, bytes_to_write - total_written);
		to_write = std::min(to_write, ByteSize{current_data.size()});
		auto chunk = current_data.subspan(0, static_cast<std::size_t>(to_write));
#ifdef LINUX
		const int send_flags = MSG_NOSIGNAL;
		const ssize_t written = ::send(m_handle,
#else
		const int send_flags = 0;
		const int written = ::send(m_handle,
#endif
			reinterpret_cast<const char*>(chunk.data()),
			static_cast<int>(chunk.size()), send_flags);
		if (written <= 0) {
#ifdef WINDOWS
			if (Connection::Handler::Instance().LastErrorCode() == WSAEWOULDBLOCK) {
				const int wait_result = WaitForWritable(m_handle);
				if (wait_result >= 0)
					continue;
			}
#else
			if (errno == EAGAIN || errno == EWOULDBLOCK) {
				const int wait_result = WaitForWritable(m_handle);
				if (wait_result >= 0)
					continue;
			}
#endif
			int sys_errno = errno;
			m_logger << Logger::Level::Error << "Write failed: " << std::string_view{Connection::Handler::Instance().LastError()}
					<< " (code: " << Connection::Handler::Instance().LastErrorCode() << ")"
					<< " errno: " << sys_errno << " (" << std::string_view{Connection::Handler::Instance().ErrnoToString(sys_errno)} << ")" << std::endl;
			return Unexpected<ConnectionError>(
				"Write failed: {} (error code: {})",
				std::string_view{Connection::Handler::Instance().LastError()},
				Connection::Handler::Instance().LastErrorCode());
		}

		total_written += ByteSize{written};
	}

	m_logger << Logger::Level::LowLevel << "Write of size " << humanreadable_bytes << bytes_to_write
			<< nohumanreadable << " bytes completed successfully" << std::endl;
	return {};
}

StormByte::Expected<ByteSize, ConnectionError> Socket::Client::TryWrite(
	std::span<const std::byte> data, bool& would_block) noexcept {
	if (m_tls_enabled) {
		would_block = false;
		try {
			Safe::UniqueLock lock(m_tls_mutex);
			if (!PumpTLS(false) || !TLSReady())
				return Unexpected<ConnectionError>("TLS stream is not authenticated");
			if (TLSHasOutput()) {
				would_block = true;
				return ByteSize{0};
			}
			auto written = m_tls.WritePlaintext(data.first(std::min(data.size(), std::size_t{16384})));
			m_tls_pending = true;
			if (!written)
				return Unexpected<ConnectionError>("TLS write failed: {}", TLSFailureDetail(*written.error()));
			if (!PumpTLS(false))
				return Unexpected<ConnectionError>("TLS write failed");
			would_block = *written == 0;
			return ByteSize{*written};
		} catch (...) {
			m_logger << Logger::Level::Error << "Failed to allocate or encrypt TLS output." << std::endl;
			return Unexpected<ConnectionError>("TLS write allocation failed");
		}
	}
	would_block = false;
	if (m_status.load(Safe::MemoryOrder::Acquire) != Connection::Status::Connected || !HasHandle()) {
		return Unexpected<ConnectionError>("Failed to write: Client is not connected");
	}

	if (data.empty()) {
		return ByteSize{0};
	}
#ifdef LINUX
	const int send_flags = MSG_NOSIGNAL;
	const ssize_t written = ::send(m_handle, reinterpret_cast<const char*>(data.data()),
		static_cast<int>(std::min(ByteSize{data.size()}, MAX_SINGLE_IO)), send_flags);
#else
	const int send_flags = 0;
	const int written = ::send(m_handle, reinterpret_cast<const char*>(data.data()),
		static_cast<int>(std::min(ByteSize{data.size()}, MAX_SINGLE_IO)), send_flags);
#endif
	if (written > 0) {
		return ByteSize{written};
	}
#ifdef WINDOWS
	if (Connection::Handler::Instance().LastErrorCode() == WSAEWOULDBLOCK) {
#else
	if (errno == EAGAIN || errno == EWOULDBLOCK) {
#endif
		would_block = true;
		return ByteSize{0};
	}

	return Unexpected<ConnectionError>("Failed to write: {}", std::string_view{Connection::Handler::Instance().LastError()});
}

StormByte::Expected<Safe::Binary, ConnectionError> Socket::Client::TryRead(bool& would_block, ByteSize maximum) noexcept {
	if (m_tls_enabled) {
		would_block = false;
		try {
			Safe::UniqueLock lock(m_tls_mutex);
			if (!TLSReady())
				return Unexpected<ConnectionError>("TLS stream is not authenticated");
			Safe::Binary data;
			data.resize(std::min(maximum, ByteSize{16384}));
			auto read = m_tls.ReadPlaintext(data.span());
			m_tls_pending = true;
			if (!read)
				return Unexpected<ConnectionError>("TLS read failed: {}", TLSFailureDetail(*read.error()));
			if (*read == 0) {
				m_tls_buffered = false;
				if (!PumpTLS(true))
					return Unexpected<ConnectionError>("TLS transport failed or truncated");
				read = m_tls.ReadPlaintext(data.span());
				if (!read)
					return Unexpected<ConnectionError>("TLS read failed: {}", TLSFailureDetail(*read.error()));
			}
			if (*read == 0 && m_tls.Status() != Crypto::TLS::State::Open)
				return Unexpected<ConnectionError>("TLS stream closed");
			data.resize(ByteSize{*read});
			m_tls_buffered = *read != 0;
			would_block = *read == 0;
			return data;
		} catch (...) {
			m_logger << Logger::Level::Error << "Failed to allocate or decrypt TLS input." << std::endl;
			return Unexpected<ConnectionError>("TLS read allocation failed");
		}
	}
	would_block = false;
	if (m_status.load(Safe::MemoryOrder::Acquire) != Connection::Status::Connected || !HasHandle()) {
		return Unexpected<ConnectionError>("Failed to read: Client is not connected");
	}

	const ByteSize size = ClampChunk(
		m_effective_recv_buf > 0 ? m_effective_recv_buf : DEFAULT_IO_CHUNK,
		MAX_SINGLE_IO);
	Safe::Binary data;
	try {
		data.resize(size);
	} catch (...) {
		return Unexpected<ConnectionError>("Read failed: unable to allocate receive bytes");
	}
#ifdef UNIX
	const ssize_t received = ::recv(m_handle, data.data(), static_cast<std::size_t>(data.size()), 0);
#else
	const int received = ::recv(m_handle, reinterpret_cast<char*>(data.data()), static_cast<int>(data.size()), 0);
#endif
	if (received > 0) {
		data.resize(ByteSize{received});
		return data;
	}

	if (received == 0) {
		return Unexpected<ConnectionError>("Read failed: connection closed by peer");
	}
#ifdef WINDOWS
	if (Connection::Handler::Instance().LastErrorCode() == WSAEWOULDBLOCK) {
#else
	if (errno == EAGAIN || errno == EWOULDBLOCK) {
#endif
		would_block = true;
		data.clear();
		return data;
	}

	return Unexpected<ConnectionError>("Failed to read: {}", std::string_view{Connection::Handler::Instance().LastError()});
}

bool Socket::Client::Ping() noexcept {
	if (m_status.load(Safe::MemoryOrder::Acquire) != Connection::Status::Connected) {
		return false;
	}

	bool ping_success = false;
	char buffer[1];
#ifdef UNIX
	ssize_t result = ::recv(m_handle, buffer, sizeof(buffer), MSG_PEEK | MSG_DONTWAIT);
#else
	int result = ::recv(m_handle, buffer, sizeof(buffer), MSG_PEEK);
	if (result == SOCKET_ERROR) {
		if (Connection::Handler::Instance().LastErrorCode() == WSAEWOULDBLOCK) {
			ping_success = true;
		}
	}
#endif
	if (result > 0) {
		ping_success = true;
	} else if (result == 0) {
		ping_success = false;
	} else {
#ifdef UNIX
		if (Connection::Handler::Instance().LastErrorCode() == EAGAIN ||
			Connection::Handler::Instance().LastErrorCode() == EWOULDBLOCK) {
			ping_success = true;
		} else {
			ping_success = false;
		}
#endif
	}

	if (ping_success) {
		m_logger << Logger::Level::LowLevel << "Ping successful" << std::endl;
	} else {
		m_logger << Logger::Level::LowLevel << "Ping failed" << std::endl;
		m_status.store(Connection::Status::Disconnected, Safe::MemoryOrder::Release);
	}

	return ping_success;
}
