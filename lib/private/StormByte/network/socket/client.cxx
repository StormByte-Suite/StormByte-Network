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

#include <algorithm>
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

Socket::Client::Client(Client&& other) noexcept = default;

Socket::Client::~Client() noexcept = default;

Socket::Client& Socket::Client::operator=(Client&& other) noexcept = default;

Socket::Client::Pointer Socket::Client::Create(const Connection::Protocol& protocol, Safe::Shared<Logger::Log> logger) noexcept {
	try {
		return Safe::MakeShared<Client>(protocol, std::move(logger));
	} catch (...) {
		return {};
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

ExpectedVoid Socket::Client::Send(const Safe::Binary& data) noexcept {
	return Send(data.span());
}

ExpectedVoid Socket::Client::Send(std::span<const std::byte> data) noexcept {
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

StormByte::Expected<Safe::Binary, ConnectionError> Socket::Client::TryRead(bool& would_block) noexcept {
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
