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
#include <StormByte/network/socket/socket.hxx>
#include <StormByte/system/this_thread.hxx>
#include <StormByte/uuid.hxx>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstring>
#include <memory>
#include <string>
#include <utility>

#ifdef UNIX
#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <unistd.h>
#include <errno.h>
#include <poll.h>
#else
#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>
#endif
#ifdef LINUX
#include <fstream>
#endif
constexpr const int SOCKET_BUFFER_SIZE = 262144; // 256 KiB desired minimum
constexpr const std::size_t MAX_SINGLE_IO = 4 * 1024 * 1024; // must match client.cxx
using namespace StormByte::Network::Socket;
Socket::Socket(const Connection::Protocol& protocol, StormByte::Safe::Shared<Logger::Log> logger):
	m_protocol(protocol), m_status(Connection::Status::Disconnected),
	m_handle(static_cast<Connection::HandlerType>(-1)), m_conn_info(nullptr), m_mtu(DEFAULT_MTU), m_logger(std::move(logger)),
	m_UUID(StormByte::GenerateUUIDv4()) {
	(void)StormByte::Network::Connection::Handler::Instance();
}

Socket::Socket(Socket&& other) noexcept:
	m_protocol(other.m_protocol),
	m_status(other.m_status.load(StormByte::Safe::MemoryOrder::Relaxed)),
	m_handle(std::exchange(other.m_handle, static_cast<Connection::HandlerType>(-1))),
	m_conn_info(std::move(other.m_conn_info)),
	m_mtu(other.m_mtu),
	m_logger(other.m_logger),
	m_effective_send_buf(other.m_effective_send_buf),
	m_effective_recv_buf(other.m_effective_recv_buf),
	m_UUID(std::move(other.m_UUID)) {
	other.m_status.store(Connection::Status::Disconnected, StormByte::Safe::MemoryOrder::Relaxed);
	other.m_effective_send_buf = StormByte::ByteSize{0};
	other.m_effective_recv_buf = StormByte::ByteSize{0};
}

Socket& Socket::operator=(Socket&& other) noexcept {
	if (this != &other) {
		Disconnect();
		m_protocol = other.m_protocol;
		m_status.store(other.m_status.load(StormByte::Safe::MemoryOrder::Relaxed), StormByte::Safe::MemoryOrder::Relaxed);
		m_handle = std::exchange(other.m_handle, static_cast<Connection::HandlerType>(-1));
		m_conn_info = std::move(other.m_conn_info);
		m_mtu = other.m_mtu;
		m_logger = std::move(other.m_logger);
		m_UUID = std::move(other.m_UUID);
		m_effective_send_buf = other.m_effective_send_buf;
		m_effective_recv_buf = other.m_effective_recv_buf;
		other.m_status.store(Connection::Status::Disconnected, StormByte::Safe::MemoryOrder::Relaxed);
		other.m_effective_send_buf = StormByte::ByteSize{0};
		other.m_effective_recv_buf = StormByte::ByteSize{0};
	}

	return *this;
}

Socket::~Socket() noexcept {
	Disconnect();
}

void Socket::Disconnect() noexcept {
	auto prev = m_status.exchange(Connection::Status::Disconnecting,
		StormByte::Safe::MemoryOrder::AcqRel);
	if (prev == Connection::Status::Disconnecting) {
		return;
	}
	if (prev == Connection::Status::Disconnected && !HasHandle()) {
		m_status.store(Connection::Status::Disconnected, StormByte::Safe::MemoryOrder::Release);
		return;
	}

	if (HasHandle()) {
#ifdef UNIX
		shutdown(m_handle, SHUT_RDWR);
#else
		shutdown(m_handle, SD_BOTH);
#endif
		StormByte::System::ThisThread::Sleep(std::chrono::milliseconds(100));
	}

	EnsureIsClosed();
	m_conn_info.reset();
	m_status.store(Connection::Status::Disconnected, StormByte::Safe::MemoryOrder::Release);
	m_logger << Logger::Level::LowLevel << "Disconnected socket " << std::string_view{m_UUID} << std::endl;
}

void Socket::EnsureIsClosed() noexcept {
	if (!HasHandle())
		return;
	const auto handle = std::exchange(m_handle, static_cast<Connection::HandlerType>(-1));
#ifdef UNIX
	::close(handle);
#else
	::closesocket(handle);
#endif
}

bool Socket::HasHandle() const noexcept {
#ifdef WINDOWS
	return m_handle != INVALID_SOCKET;
#else
	return m_handle >= 0;
#endif
}

StormByte::Safe::String Socket::LocalAddress() const noexcept {
	if (!HasHandle())
		return {};
	struct sockaddr_storage address{};
#ifdef WINDOWS
	int address_size = static_cast<int>(sizeof(address));
#else
	socklen_t address_size = static_cast<socklen_t>(sizeof(address));
#endif
	if (::getsockname(m_handle, reinterpret_cast<struct sockaddr*>(&address), &address_size) != 0)
		return {};

	std::array<char, INET6_ADDRSTRLEN> buffer{};
	const void* source = nullptr;
	int family = address.ss_family;
	if (family == AF_INET) {
		source = &reinterpret_cast<const struct sockaddr_in*>(&address)->sin_addr;
	} else if (family == AF_INET6) {
		source = &reinterpret_cast<const struct sockaddr_in6*>(&address)->sin6_addr;
	} else {
		return {};
	}
	if (!inet_ntop(family, source, buffer.data(), buffer.size()))
		return {};
	return StormByte::Safe::String{buffer.data()};
}

StormByte::Network::ExpectedReadResult Socket::WaitForData(const long long& usecs) noexcept {
	if (!Connection::IsConnected(m_status.load(StormByte::Safe::MemoryOrder::Acquire))) {
		return Unexpected<ConnectionClosed>("Failed to wait for data: Invalid connection status");
	}

	auto start_time = std::chrono::steady_clock::now();
	const std::chrono::microseconds requested_usecs = std::chrono::microseconds(usecs);
	constexpr std::chrono::microseconds MIN_WAIT = std::chrono::microseconds(10000); // 10ms
	const std::chrono::microseconds effective_usecs =
		(usecs > 0) ? std::max(requested_usecs, MIN_WAIT) : std::chrono::microseconds::zero();
	const auto deadline = (usecs > 0) ? (start_time + effective_usecs)
									: std::chrono::steady_clock::time_point::max();
	// Progress log: first after 5s idle, then every 5s, with elapsed time for this wait.
	constexpr auto PROGRESS_INTERVAL = std::chrono::seconds(5);
	auto next_progress_log = start_time + PROGRESS_INTERVAL;
	bool logged_waiting = false;
	auto elapsed_ms = [&]() {
		return std::chrono::duration_cast<std::chrono::milliseconds>(
			std::chrono::steady_clock::now() - start_time).count();
	};
	auto log_progress_if_due = [&]() {
		const auto now = std::chrono::steady_clock::now();
		if (now < next_progress_log)
			return;
		logged_waiting = true;
		m_logger << Logger::Level::LowLevel
				<< "Still waiting for data on socket " << std::string_view{m_UUID}
				<< " (elapsed " << elapsed_ms() << " ms)" << std::endl;
		next_progress_log = now + PROGRESS_INTERVAL;
	};
	auto log_wait_done = [&](const char* reason) {
		const auto ms = elapsed_ms();
		if (!logged_waiting && ms < 1000)
			return;
		m_logger << Logger::Level::LowLevel
				<< "Wait for data on socket " << std::string_view{m_UUID} << ": " << reason
				<< " after " << ms << " ms" << std::endl;
	};
	while (Connection::IsConnected(m_status.load(StormByte::Safe::MemoryOrder::Acquire))) {
		log_progress_if_due();
	#ifdef UNIX
		// macOS / other POSIX: poll (máxima portabilidad)
		int timeout_ms = -1;
		if (usecs > 0) {
			auto now2 = std::chrono::steady_clock::now();
			if (now2 >= deadline) {
				log_wait_done("timeout");
				return Connection::Read::Result::Timeout;
			}

			auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now2);
			timeout_ms = static_cast<int>(remaining.count());
			if (timeout_ms < 0) timeout_ms = 0;
		}

		struct pollfd pfd;
		pfd.fd = m_handle;
		pfd.events = POLLIN | POLLPRI | POLLHUP | POLLERR;
#ifdef POLLRDHUP
		pfd.events |= POLLRDHUP;		// solo si el sistema lo define
#endif
		pfd.revents = 0;
		int nfds = poll(&pfd, 1, timeout_ms);
		if (nfds > 0) {
			if (pfd.revents & POLLERR) {
				return Unexpected<ConnectionClosed>("Socket error while waiting for data");
			}

			if (pfd.revents & (POLLHUP
#ifdef POLLRDHUP
				| POLLRDHUP
#endif
				)) {
				if (m_status.load(StormByte::Safe::MemoryOrder::Acquire) != Connection::Status::Connected)
					return Connection::Read::Result::Closed;
				if (pfd.revents & POLLIN) {
					char tmp;
					ssize_t r = recv(m_handle, &tmp, 1, MSG_PEEK | MSG_DONTWAIT);
					if (r > 0) {
						log_wait_done("data available");
						return Connection::Read::Result::Success;
					} else if (r == 0) {
						log_wait_done("peer shutdown");
						return Connection::Read::Result::ShutdownRequest;
					} else {
						if (errno == EWOULDBLOCK || errno == EAGAIN) {
							log_wait_done("data available");
							return Connection::Read::Result::Success;
						}

						return Unexpected<ConnectionClosed>("recv(MSG_PEEK) error while checking shutdown");
					}
				}

				log_wait_done("peer shutdown");
				return Connection::Read::Result::ShutdownRequest;
			}

			if (m_status.load(StormByte::Safe::MemoryOrder::Acquire) != Connection::Status::Connected)
				return Connection::Read::Result::Closed;
			if (pfd.revents & (POLLIN | POLLPRI)) {
				log_wait_done("data available");
				return Connection::Read::Result::Success;
			}

			return Unexpected<ConnectionClosed>("Unknown poll event while waiting for data");
		} else if (nfds == 0) {
			log_wait_done("timeout");
			return Connection::Read::Result::Timeout;
		} else {
			if (errno == ECONNRESET || errno == EBADF) {
				return Unexpected<ConnectionClosed>("Connection closed or invalid socket");
			}

			return Unexpected<ConnectionClosed>("Failed to wait for data: poll error");
		}
#else
		// Windows
		int timeout_ms = -1;
		if (usecs > 0) {
			auto now2 = std::chrono::steady_clock::now();
			if (now2 >= deadline) {
				log_wait_done("timeout");
				return Connection::Read::Result::Timeout;
			}

			auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now2);
			timeout_ms = static_cast<int>(remaining.count());
			if (timeout_ms < 0) timeout_ms = 0;
		}

		fd_set read_fds;
		FD_ZERO(&read_fds);
		FD_SET(m_handle, &read_fds);
		timeval timeout{ .tv_sec = timeout_ms / 1000, .tv_usec = (timeout_ms % 1000) * 1000 };
		timeval* timeout_ptr = timeout_ms < 0 ? nullptr : &timeout;
		const int wait_res = select(0, &read_fds, nullptr, nullptr, timeout_ptr);
		if (wait_res == 0) {
			log_wait_done("timeout");
			return Connection::Read::Result::Timeout;
		} else if (wait_res == SOCKET_ERROR) {
			int wsa_err = WSAGetLastError();
			if (wsa_err == WSAECONNRESET || wsa_err == WSAENOTSOCK) {
				return Unexpected<ConnectionClosed>("Connection closed or invalid socket");
			}

			return Unexpected<ConnectionClosed>("select failed");
		} else {
			if (!FD_ISSET(m_handle, &read_fds))
				return Unexpected<ConnectionClosed>("Unknown select event while waiting for data");
			if (m_status.load(StormByte::Safe::MemoryOrder::Acquire) != Connection::Status::Connected)
				return Connection::Read::Result::Closed;
			char tmp;
			const int bytes_read = recv(m_handle, &tmp, 1, MSG_PEEK);
			if (bytes_read > 0) {
				log_wait_done("data available");
				return Connection::Read::Result::Success;
			}

			if (bytes_read == 0) {
				log_wait_done("peer shutdown");
				return Connection::Read::Result::ShutdownRequest;
			}

			if (WSAGetLastError() == WSAEWOULDBLOCK) {
				log_wait_done("data available");
				return Connection::Read::Result::Success;
			}

			return Unexpected<ConnectionClosed>("recv(MSG_PEEK) failed while checking shutdown");
		}
#endif
	}

	return Unexpected<ConnectionClosed>("Failed to wait for data: Unknown error occurred");
}

StormByte::Expected<StormByte::Network::Connection::HandlerType, StormByte::Network::ConnectionError>
Socket::CreateSocket() noexcept {
	(void)StormByte::Network::Connection::Handler::Instance();
	Connection::HandlerType handle = ::socket(Connection::ProtocolInt(m_protocol), SOCK_STREAM, 0);
#ifdef WINDOWS
	if (handle == INVALID_SOCKET) {
#else
	if (handle == -1) {
#endif
		m_status.store(Connection::Status::Disconnected, StormByte::Safe::MemoryOrder::Release);
		return Unexpected<ConnectionError>("{}", std::string_view{Connection::Handler::Instance().LastError()});
	}

	return handle;
}

void Socket::InitializeAfterConnect() {
	m_status.store(Connection::Status::Connecting, StormByte::Safe::MemoryOrder::Release);
	m_mtu = GetMTU();
	SetNonBlocking();
	int desired_buf = SOCKET_BUFFER_SIZE;
	int rc = 0;
#ifdef LINUX
	auto read_proc_int = [](const char* path) -> int {
		std::ifstream f(path);
		if (!f.is_open()) return -1;
		std::string s;
		std::getline(f, s);
		try {
			return std::stoi(s);
		} catch (...) {
			return -1;
		}
	};
	int sys_wmem_max = read_proc_int("/proc/sys/net/core/wmem_max");
	int sys_rmem_max = read_proc_int("/proc/sys/net/core/rmem_max");
	if (sys_wmem_max > 0) {
		m_logger << Logger::Level::LowLevel << "System wmem_max: " << Logger::humanreadable_bytes
				<< sys_wmem_max << Logger::nohumanreadable << std::endl;
	}

	if (sys_rmem_max > 0) {
		m_logger << Logger::Level::LowLevel << "System rmem_max: " << Logger::humanreadable_bytes
				<< sys_rmem_max << Logger::nohumanreadable << std::endl;
	}

	int send_buf = desired_buf;
	int recv_buf = desired_buf;
	if (sys_wmem_max > send_buf) send_buf = sys_wmem_max;
	if (sys_rmem_max > recv_buf) recv_buf = sys_rmem_max;
	rc = setsockopt(m_handle, SOL_SOCKET, SO_SNDBUF, &send_buf, sizeof(send_buf));
	if (rc != 0) {
		m_logger << Logger::Level::Warning << "setsockopt(SO_SNDBUF) failed: "
				<< std::string_view{Connection::Handler::Instance().LastError()} << std::endl;
	}

	rc = setsockopt(m_handle, SOL_SOCKET, SO_RCVBUF, &recv_buf, sizeof(recv_buf));
	if (rc != 0) {
		m_logger << Logger::Level::Warning << "setsockopt(SO_RCVBUF) failed: "
				<< std::string_view{Connection::Handler::Instance().LastError()} << std::endl;
	}
#elifdef UNIX
	// macOS / other POSIX (no /proc)
	int send_buf = desired_buf;
	int recv_buf = desired_buf;
	rc = setsockopt(m_handle, SOL_SOCKET, SO_SNDBUF, &send_buf, sizeof(send_buf));
	if (rc != 0) {
		m_logger << Logger::Level::Warning << "setsockopt(SO_SNDBUF) failed: "
				<< std::string_view{Connection::Handler::Instance().LastError()} << std::endl;
	}

	rc = setsockopt(m_handle, SOL_SOCKET, SO_RCVBUF, &recv_buf, sizeof(recv_buf));
	if (rc != 0) {
		m_logger << Logger::Level::Warning << "setsockopt(SO_RCVBUF) failed: "
				<< std::string_view{Connection::Handler::Instance().LastError()} << std::endl;
	}

	// Prevent SIGPIPE on send (macOS/BSD equivalent of MSG_NOSIGNAL)
	int nosigpipe = 1;
	rc = setsockopt(m_handle, SOL_SOCKET, SO_NOSIGPIPE, &nosigpipe, sizeof(nosigpipe));
	if (rc != 0) {
		m_logger << Logger::Level::Warning << "setsockopt(SO_NOSIGPIPE) failed: "
				<< std::string_view{Connection::Handler::Instance().LastError()} << std::endl;
	}
#else
	// Windows
	constexpr int WINDOWS_DESIRED_MAX = 8 * 1024 * 1024; // 8 MiB request (OS will clamp)
	int try_send = WINDOWS_DESIRED_MAX;
	int try_recv = WINDOWS_DESIRED_MAX;
	rc = setsockopt(m_handle, SOL_SOCKET, SO_SNDBUF,
		reinterpret_cast<const char*>(&try_send), sizeof(try_send));
	if (rc != 0) {
		m_logger << Logger::Level::Warning << "setsockopt(SO_SNDBUF) attempt failed: "
				<< std::string_view{Connection::Handler::Instance().LastError()} << std::endl;
		rc = setsockopt(m_handle, SOL_SOCKET, SO_SNDBUF,
			reinterpret_cast<const char*>(&desired_buf), sizeof(desired_buf));
		if (rc != 0) {
			m_logger << Logger::Level::Warning << "setsockopt(SO_SNDBUF) fallback failed: "
					<< std::string_view{Connection::Handler::Instance().LastError()} << std::endl;
		}
	}

	rc = setsockopt(m_handle, SOL_SOCKET, SO_RCVBUF,
		reinterpret_cast<const char*>(&try_recv), sizeof(try_recv));
	if (rc != 0) {
		m_logger << Logger::Level::Warning << "setsockopt(SO_RCVBUF) attempt failed: "
				<< std::string_view{Connection::Handler::Instance().LastError()} << std::endl;
		rc = setsockopt(m_handle, SOL_SOCKET, SO_RCVBUF,
			reinterpret_cast<const char*>(&desired_buf), sizeof(desired_buf));
		if (rc != 0) {
			m_logger << Logger::Level::Warning << "setsockopt(SO_RCVBUF) fallback failed: "
					<< std::string_view{Connection::Handler::Instance().LastError()} << std::endl;
		}
	}
#endif
	int effective = 0;
#ifdef WINDOWS
	int optlen = sizeof(effective);
	if (getsockopt(m_handle, SOL_SOCKET, SO_SNDBUF, reinterpret_cast<char*>(&effective), &optlen) == 0) {
		m_logger << Logger::Level::LowLevel << "Effective SO_SNDBUF: " << Logger::humanreadable_bytes
				<< effective << Logger::nohumanreadable << std::endl;
		m_effective_send_buf = StormByte::ByteSize{effective};
	}

	optlen = sizeof(effective);
	if (getsockopt(m_handle, SOL_SOCKET, SO_RCVBUF, reinterpret_cast<char*>(&effective), &optlen) == 0) {
		m_logger << Logger::Level::LowLevel << "Effective SO_RCVBUF: " << Logger::humanreadable_bytes
				<< effective << Logger::nohumanreadable << std::endl;
		m_effective_recv_buf = StormByte::ByteSize{effective};
	}
#else
	socklen_t optlen = sizeof(effective);
	if (getsockopt(m_handle, SOL_SOCKET, SO_SNDBUF, &effective, &optlen) == 0) {
		m_logger << Logger::Level::LowLevel << "Effective SO_SNDBUF: " << Logger::humanreadable_bytes
				<< effective << Logger::nohumanreadable << std::endl;
		m_effective_send_buf = StormByte::ByteSize{effective};
	}

	optlen = sizeof(effective);
	if (getsockopt(m_handle, SOL_SOCKET, SO_RCVBUF, &effective, &optlen) == 0) {
		m_logger << Logger::Level::LowLevel << "Effective SO_RCVBUF: " << Logger::humanreadable_bytes
				<< effective << Logger::nohumanreadable << std::endl;
		m_effective_recv_buf = StormByte::ByteSize{effective};
	}
#endif
	{
		std::size_t send_cap = static_cast<std::size_t>(m_effective_send_buf);
		std::size_t recv_cap = static_cast<std::size_t>(m_effective_recv_buf);
		if (send_cap == 0) send_cap = 65536;
		if (recv_cap == 0) recv_cap = 65536;
		send_cap = std::min(send_cap, MAX_SINGLE_IO);
		recv_cap = std::min(recv_cap, MAX_SINGLE_IO);
		m_logger << Logger::Level::LowLevel << "Per-call send capacity: " << Logger::humanreadable_bytes
				<< send_cap << ", recv capacity: " << recv_cap
				<< " (max single IO: " << MAX_SINGLE_IO << ")" << Logger::nohumanreadable << std::endl;
	}

	int flag = 1;
#ifdef WINDOWS
	rc = setsockopt(m_handle, IPPROTO_TCP, TCP_NODELAY,
		reinterpret_cast<const char*>(&flag), sizeof(flag));
#else
	rc = setsockopt(m_handle, IPPROTO_TCP, TCP_NODELAY, &flag, sizeof(flag));
#endif
	if (rc != 0) {
		m_logger << Logger::Level::Warning << "setsockopt(TCP_NODELAY) failed: "
				<< std::string_view{Connection::Handler::Instance().LastError()} << std::endl;
	}

	m_status.store(Connection::Status::Connected, StormByte::Safe::MemoryOrder::Release);
}

#ifdef UNIX
StormByte::ByteSize Socket::GetMTU() const noexcept {
	if (!m_conn_info || !HasHandle())
		return DEFAULT_MTU;
#ifdef LINUX
	int mtu = 0;
	socklen_t optlen = sizeof(mtu);
	if (getsockopt(m_handle, IPPROTO_IP, IP_MTU, &mtu, &optlen) >= 0 && mtu > 0)
		return StormByte::ByteSize{mtu};
#endif
	// macOS / otros UNIX: devolvemos el valor por defecto
	// (el stack de macOS gestiona el PMTU de forma transparente)
	return DEFAULT_MTU;
}
#else
StormByte::ByteSize Socket::GetMTU() const noexcept {
	if (!m_conn_info || !HasHandle())
		return DEFAULT_MTU;
	ULONG out_buf_len = 0;
	GetAdaptersAddresses(AF_UNSPEC, 0, NULL, NULL, &out_buf_len);
	auto adapter_addresses = std::make_unique<BYTE[]>(out_buf_len);
	if (GetAdaptersAddresses(AF_UNSPEC, 0, NULL,
			reinterpret_cast<PIP_ADAPTER_ADDRESSES>(adapter_addresses.get()),
			&out_buf_len) != ERROR_SUCCESS) {
		return DEFAULT_MTU;
	}

	PIP_ADAPTER_ADDRESSES adapter = reinterpret_cast<PIP_ADAPTER_ADDRESSES>(adapter_addresses.get());
	while (adapter) {
		for (PIP_ADAPTER_UNICAST_ADDRESS unicast = adapter->FirstUnicastAddress;
			unicast != nullptr; unicast = unicast->Next) {
			if (unicast->Address.lpSockaddr &&
				static_cast<std::size_t>(unicast->Address.iSockaddrLength) >= static_cast<std::size_t>(m_conn_info->SockAddrSize()) &&
				unicast->Address.lpSockaddr->sa_family == m_conn_info->SockAddr()->sa_family &&
				std::memcmp(unicast->Address.lpSockaddr, m_conn_info->SockAddr().get(),
					static_cast<std::size_t>(m_conn_info->SockAddrSize())) == 0) {
				return StormByte::ByteSize{adapter->Mtu};
			}
		}

		adapter = adapter->Next;
	}

	return DEFAULT_MTU;
}
#endif

void Socket::SetNonBlocking() noexcept {
#ifdef WINDOWS
	u_long mode = 1;
	ioctlsocket(m_handle, FIONBIO, &mode);
#else
	int flags = fcntl(m_handle, F_GETFL, 0);
	fcntl(m_handle, F_SETFL, flags | O_NONBLOCK);
#endif
}
