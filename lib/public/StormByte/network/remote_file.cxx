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

#include <StormByte/network/remote_file.hxx>

#include <StormByte/network/remote_file_protocol.hxx>
#include <StormByte/safe/binary.hxx>

#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <utility>

#ifdef WINDOWS
#include <iphlpapi.h>
#include <ws2tcpip.h>
#elifdef UNIX
#include <arpa/inet.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <netinet/in.h>
#if defined(MACOS)
#include <net/if_dl.h>
#endif
#endif

namespace StormByte::Network {
	using StormByte::Buffer::IO::State;
	using StormByte::Buffer::IO::Status;
	using StormByte::Buffer::Position;
	using StormByte::Network::Detail::RemoteFile::DataPlane;
	using StormByte::Network::Detail::RemoteFile::Message;
	using StormByte::Network::Detail::RemoteFile::Opcode;
	using RemoteStatus = StormByte::Network::Detail::RemoteFile::Status;
	using Device = StormByte::System::Device;
	using NetworkThroughput = struct Device::Throughput;
	using NetworkWindow = struct Device::Window;

	namespace {
		constexpr StormByte::ByteSize fallback_network_bps{30ull * 1024ull * 1024ull};
		constexpr StormByte::ByteSize minimum_window{16ull * 1024ull};
		constexpr StormByte::ByteSize maximum_window{1024ull * 1024ull};

		StormByte::ByteSize UsefulThroughput(const std::uint64_t link_bps) noexcept {
			return link_bps == 0 ? fallback_network_bps
				: StormByte::ByteSize{link_bps / 10ull};
		}

		NetworkWindow WindowFrom(const NetworkThroughput& throughput) noexcept {
			const auto window = [](const StormByte::ByteSize bps) {
				const StormByte::ByteSize raw = bps / 500ull;
				return std::clamp(raw, minimum_window, maximum_window);
			};
			return {window(throughput.read_bps), window(throughput.write_bps)};
		}

#ifdef WINDOWS
		NetworkThroughput GetNetworkThroughput(const StormByte::Safe::String& local_address) {
			NetworkThroughput result{fallback_network_bps, fallback_network_bps};
			SOCKET_ADDRESS address{};
			sockaddr_in address_v4{};
			sockaddr_in6 address_v6{};
			if (InetPtonA(AF_INET, local_address.c_str(), &address_v4.sin_addr) == 1) {
				address_v4.sin_family = AF_INET;
				address.lpSockaddr = reinterpret_cast<sockaddr*>(&address_v4);
				address.iSockaddrLength = sizeof(address_v4);
			} else if (InetPtonA(AF_INET6, local_address.c_str(), &address_v6.sin6_addr) == 1) {
				address_v6.sin6_family = AF_INET6;
				address.lpSockaddr = reinterpret_cast<sockaddr*>(&address_v6);
				address.iSockaddrLength = sizeof(address_v6);
			} else {
				return result;
			}

			ULONG size = 0;
			(void)GetAdaptersAddresses(AF_UNSPEC, GAA_FLAG_INCLUDE_PREFIX, nullptr, nullptr, &size);
			if (size == 0) return result;
			StormByte::Safe::Binary storage(StormByte::ByteSize{size});
			auto* adapter = reinterpret_cast<IP_ADAPTER_ADDRESSES*>(storage.data());
			if (GetAdaptersAddresses(AF_UNSPEC, GAA_FLAG_INCLUDE_PREFIX, nullptr, adapter, &size) != NO_ERROR)
				return result;

			for (; adapter; adapter = adapter->Next) {
				for (auto* unicast = adapter->FirstUnicastAddress; unicast; unicast = unicast->Next) {
					const sockaddr* candidate = unicast->Address.lpSockaddr;
					if (!candidate || candidate->sa_family != address.lpSockaddr->sa_family) continue;
					const bool matches = candidate->sa_family == AF_INET
						? reinterpret_cast<const sockaddr_in*>(candidate)->sin_addr.s_addr == address_v4.sin_addr.s_addr
						: std::memcmp(&reinterpret_cast<const sockaddr_in6*>(candidate)->sin6_addr,
							&address_v6.sin6_addr, sizeof(IN6_ADDR)) == 0;
					if (matches) {
						result = {UsefulThroughput(adapter->ReceiveLinkSpeed),
							UsefulThroughput(adapter->TransmitLinkSpeed)};
						return result;
					}
				}
			}
			return result;
		}
#elifdef UNIX
		bool SameAddress(const sockaddr* candidate, const sockaddr* target) noexcept {
			if (!candidate || candidate->sa_family != target->sa_family) return false;
			if (target->sa_family == AF_INET) {
				return reinterpret_cast<const sockaddr_in*>(candidate)->sin_addr.s_addr
					== reinterpret_cast<const sockaddr_in*>(target)->sin_addr.s_addr;
			}
			if (target->sa_family == AF_INET6) {
				return std::memcmp(&reinterpret_cast<const sockaddr_in6*>(candidate)->sin6_addr,
					&reinterpret_cast<const sockaddr_in6*>(target)->sin6_addr, sizeof(in6_addr)) == 0;
			}
			return false;
		}

		NetworkThroughput GetNetworkThroughput(const StormByte::Safe::String& local_address) {
			NetworkThroughput result{fallback_network_bps, fallback_network_bps};
			sockaddr_in address_v4{};
			sockaddr_in6 address_v6{};
			const sockaddr* target = nullptr;
			if (inet_pton(AF_INET, local_address.c_str(), &address_v4.sin_addr) == 1) {
				address_v4.sin_family = AF_INET;
				target = reinterpret_cast<const sockaddr*>(&address_v4);
			} else if (inet_pton(AF_INET6, local_address.c_str(), &address_v6.sin6_addr) == 1) {
				address_v6.sin6_family = AF_INET6;
				target = reinterpret_cast<const sockaddr*>(&address_v6);
			} else {
				return result;
			}

			ifaddrs* interfaces = nullptr;
			if (getifaddrs(&interfaces) != 0) return result;
			StormByte::Safe::String interface_name;
			for (auto* interface = interfaces; interface; interface = interface->ifa_next) {
				if (SameAddress(interface->ifa_addr, target)) {
					interface_name = std::string_view{interface->ifa_name};
					break;
				}
			}
#if defined(LINUX)
			if (!interface_name.empty()) {
				std::ifstream speed_file(std::filesystem::path{"/sys/class/net"} / std::string_view{interface_name} / "speed");
				std::uint64_t megabits = 0;
				if (speed_file >> megabits && megabits > 0) {
					const StormByte::ByteSize bps{megabits * 1000000ull};
					result = {UsefulThroughput(static_cast<std::uint64_t>(bps)), UsefulThroughput(static_cast<std::uint64_t>(bps))};
				}
			}
#elif defined(MACOS)
			for (auto* interface = interfaces; interface; interface = interface->ifa_next) {
				if (interface_name == interface->ifa_name && interface->ifa_data
					&& interface->ifa_addr && interface->ifa_addr->sa_family == AF_LINK) {
					const auto* data = static_cast<const struct if_data*>(interface->ifa_data);
					if (data->ifi_baudrate != 0) {
						const StormByte::ByteSize bps{data->ifi_baudrate};
						result = {UsefulThroughput(static_cast<std::uint64_t>(bps)), UsefulThroughput(static_cast<std::uint64_t>(bps))};
					}
					break;
				}
			}
#endif
			freeifaddrs(interfaces);
			return result;
		}
#else
		NetworkThroughput GetNetworkThroughput(const StormByte::Safe::String&) {
			return {fallback_network_bps, fallback_network_bps};
		}
#endif

		class RemoteNetworkDevice final: public Device {
			public:
				RemoteNetworkDevice(std::string_view local_address, NetworkThroughput throughput) noexcept:
					Device(local_address), m_throughput(throughput) {}

				NetworkThroughput Throughput() const noexcept override {
					return m_throughput;
				}

				NetworkWindow Window() const noexcept override {
					return WindowFrom(m_throughput);
				}

			private:
				NetworkThroughput m_throughput;
		};
	}
}

STORMBYTE_DECLARE_MAYBE_SAFE(StormByte::Network::RemoteNetworkDevice);

namespace StormByte::Network {

	namespace Detail::RemoteFile {
		StormByte::Safe::Shared<StormByte::System::Device> CreateNetworkDevice(std::string_view local_address) {
			return StormByte::Safe::Shared<StormByte::System::Device>::MakePointer<RemoteNetworkDevice>(
				local_address, GetNetworkThroughput(StormByte::Safe::String{local_address}));
		}
	}

	BufferedRemoteFileReader::BufferedRemoteFileReader(StormByte::Safe::String locator,
		StormByte::Safe::Weak<DataPlane> plane, RemoteFileMount::ChannelToken token):
		BufferedLocationReader(std::move(locator), Buffer::IO::Location::Remote,
			StormByte::ByteSize{0}, StormByte::ByteSize{0}, std::chrono::milliseconds{0}, true),
		m_plane(std::move(plane)), m_token(std::move(token)) {}

	BufferedRemoteFileReader::~BufferedRemoteFileReader() noexcept {
		if (IsOpen()) {
			(void)Close();
		}
		ReleaseToken();
	}

	StormByte::Safe::Shared<StormByte::System::Device> BufferedRemoteFileReader::OriginDevice() const {
		if (auto plane = m_plane.lock()) return plane->Device();
		return {};
	}

	bool BufferedRemoteFileReader::OriginDeviceUsable(
		const StormByte::Safe::Shared<StormByte::System::Device>& device) const noexcept {
		return static_cast<bool>(device);
	}

	Buffer::IO::Result BufferedRemoteFileReader::OriginOpen() {
		auto plane = m_plane.lock();
		if (!plane || plane->Failed()) {
			SetState(State::Fault);
			return { Status::Failed, StormByte::ByteSize{0} };
		}
		Message request;
		request.opcode = Opcode::Open;
		request.token = m_token;
		auto response = plane->Exchange(std::move(request));
		if (!response || response->status != RemoteStatus::Ok) {
			SetState(response ? State::Permission : State::Fault);
			return { Status::Failed, StormByte::ByteSize{0} };
		}
		m_offset = StormByte::ByteSize{0};
		SetState(State::Idle);
		return { Status::Ok, StormByte::ByteSize{0} };
	}

	Buffer::IO::Result BufferedRemoteFileReader::OriginClose() {
		if (!ReleaseToken()) {
			SetState(State::Fault);
			return { Status::Error, StormByte::ByteSize{0} };
		}
		SetState(State::Unavailable);
		return { Status::Ok, StormByte::ByteSize{0} };
	}

	Buffer::IO::Result BufferedRemoteFileReader::OriginPull(const StormByte::ByteSize count, Buffer::FIFO& destination) {
		if (count == StormByte::ByteSize{0}) return { Status::Ok, StormByte::ByteSize{0} };
		if (count > StormByte::ByteSize{Detail::RemoteFile::max_data_size}) {
			SetState(State::Fault);
			return { Status::Failed, StormByte::ByteSize{0} };
		}
		auto plane = m_plane.lock();
		if (!plane || plane->Failed()) {
			SetState(State::Fault);
			return { Status::Failed, StormByte::ByteSize{0} };
		}
		Message request;
		request.opcode = Opcode::Read;
		request.token = m_token;
		request.offset = static_cast<std::uint64_t>(m_offset);
		request.value = static_cast<std::uint64_t>(count);
		auto response = plane->Exchange(std::move(request));
		if (!response || response->value != response->data.size() || response->data.size() > count
			|| response->offset != static_cast<std::uint64_t>(m_offset)
			|| (response->status != RemoteStatus::Ok && response->status != RemoteStatus::End)) {
			SetState(State::Fault);
			return { Status::Error, StormByte::ByteSize{0} };
		}
		const StormByte::ByteSize received = response->data.size();
		m_offset += received;
		if (received > StormByte::ByteSize{0}
			&& !destination.Write(received, std::move(response->data))) {
			SetState(State::Fault);
			return { Status::Error, StormByte::ByteSize{0} };
		}
		return { response->status == RemoteStatus::End ? Status::End : Status::Ok, received };
	}

	Buffer::IO::Result BufferedRemoteFileReader::OriginSeek(const std::ptrdiff_t offset, const Position mode) {
		const std::int64_t signed_offset = static_cast<std::int64_t>(offset);
		const std::uint64_t current = static_cast<std::uint64_t>(m_offset);
		std::uint64_t target = 0;
		if (mode == Position::Absolute) {
			if (signed_offset < 0) return { Status::Failed, StormByte::ByteSize{0} };
			target = static_cast<std::uint64_t>(signed_offset);
		} else if (signed_offset < 0) {
			const std::uint64_t magnitude = static_cast<std::uint64_t>(-(signed_offset + 1)) + 1;
			if (magnitude > current) return { Status::Failed, StormByte::ByteSize{0} };
			target = current - magnitude;
		} else {
			if (static_cast<std::uint64_t>(signed_offset) > std::numeric_limits<std::uint64_t>::max() - current) {
				return { Status::Failed, StormByte::ByteSize{0} };
			}
			target = current + static_cast<std::uint64_t>(signed_offset);
		}
		m_offset = StormByte::ByteSize{target};
		return { Status::Ok, StormByte::ByteSize{0} };
	}

	StormByte::Safe::Optional<StormByte::ByteSize> BufferedRemoteFileReader::OriginSize() const noexcept {
		auto plane = m_plane.lock();
		if (!plane || plane->Failed()) {
			const_cast<BufferedRemoteFileReader*>(this)->SetState(State::Fault);
			return {};
		}
		Message request;
		request.opcode = Opcode::Size;
		request.token = m_token;
		auto response = plane->Exchange(std::move(request));
		if (!response || response->status != RemoteStatus::Ok) {
			const_cast<BufferedRemoteFileReader*>(this)->SetState(State::Fault);
			return {};
		}
		return StormByte::ByteSize{response->value};
	}

	BufferedRemoteFileWriter::BufferedRemoteFileWriter(StormByte::Safe::String locator,
		StormByte::Safe::Weak<DataPlane> plane, RemoteFileMount::ChannelToken token):
		BufferedLocationWriter(std::move(locator), Buffer::IO::Location::Remote,
			StormByte::ByteSize{0}, 0, std::chrono::milliseconds{0}, StormByte::ByteSize{0}, true),
		m_plane(std::move(plane)), m_token(std::move(token)) {}

	BufferedRemoteFileWriter::~BufferedRemoteFileWriter() noexcept {
		if (IsOpen()) {
			(void)Close();
		}
		ReleaseToken();
	}

	StormByte::Safe::Shared<StormByte::System::Device> BufferedRemoteFileWriter::OriginDevice() const {
		if (auto plane = m_plane.lock()) return plane->Device();
		return {};
	}

	bool BufferedRemoteFileWriter::OriginDeviceUsable(
		const StormByte::Safe::Shared<StormByte::System::Device>& device) const noexcept {
		return static_cast<bool>(device);
	}

	Buffer::IO::Result BufferedRemoteFileWriter::OriginOpen() {
		auto plane = m_plane.lock();
		if (!plane || plane->Failed()) {
			SetState(State::Fault);
			return { Status::Failed, StormByte::ByteSize{0} };
		}
		Message request;
		request.opcode = Opcode::Open;
		request.token = m_token;
		auto response = plane->Exchange(std::move(request));
		if (!response || response->status != RemoteStatus::Ok) {
			SetState(response ? State::NotWritable : State::Fault);
			return { Status::Failed, StormByte::ByteSize{0} };
		}
		m_offset = StormByte::ByteSize{0};
		SetState(State::Idle);
		return { Status::Ok, StormByte::ByteSize{0} };
	}

	Buffer::IO::Result BufferedRemoteFileWriter::OriginClose() {
		if (!ReleaseToken()) {
			SetState(State::Fault);
			return { Status::Error, StormByte::ByteSize{0} };
		}
		SetState(State::Unavailable);
		return { Status::Ok, StormByte::ByteSize{0} };
	}

	Buffer::IO::Result BufferedRemoteFileWriter::OriginPush(const std::span<const std::byte> data) {
		if (data.empty()) {
			return { Status::Ok, StormByte::ByteSize{0} };
		}
		constexpr std::size_t max_chunk = static_cast<std::size_t>(Detail::RemoteFile::max_data_size);
		auto plane = m_plane.lock();
		if (!plane || plane->Failed()
			|| data.size() > std::numeric_limits<std::uint64_t>::max() - static_cast<std::uint64_t>(m_offset)) {
			SetState(State::Fault);
			return { Status::Error, StormByte::ByteSize{0} };
		}
		std::size_t written = 0;
		while (written < data.size()) {
			const std::size_t count = std::min(max_chunk, data.size() - written);
			Message request;
			request.opcode = Opcode::Write;
			request.token = m_token;
			request.offset = static_cast<std::uint64_t>(m_offset) + written;
			request.value = count;
			request.data.assign(data.subspan(written, count));
			auto response = plane->Exchange(std::move(request));
			if (!response || response->status != RemoteStatus::Ok || response->value != count
				|| response->offset != static_cast<std::uint64_t>(m_offset) + written
				|| !response->data.empty()) {
				SetState(State::Fault);
				return { Status::Error, StormByte::ByteSize{0} };
			}
			written += count;
		}
		m_offset += StormByte::ByteSize{written};
		return { Status::Ok, StormByte::ByteSize{written} };
	}

	Buffer::IO::Result BufferedRemoteFileWriter::OriginFlush() {
		auto plane = m_plane.lock();
		if (!plane || plane->Failed()) {
			SetState(State::Fault);
			return { Status::Error, StormByte::ByteSize{0} };
		}
		Message request;
		request.opcode = Opcode::Flush;
		request.token = m_token;
		auto response = plane->Exchange(std::move(request));
		if (!response || response->status != RemoteStatus::Ok || !response->data.empty()) {
			SetState(State::Fault);
			return { Status::Error, StormByte::ByteSize{0} };
		}
		return { Status::Ok, StormByte::ByteSize{0} };
	}

	Buffer::IO::Result BufferedRemoteFileWriter::OriginTruncate() {
		auto plane = m_plane.lock();
		if (!plane || plane->Failed()) {
			SetState(State::Fault);
			return { Status::Error, StormByte::ByteSize{0} };
		}
		Message request;
		request.opcode = Opcode::Truncate;
		request.token = m_token;
		auto response = plane->Exchange(std::move(request));
		if (!response || response->status != RemoteStatus::Ok || !response->data.empty()) {
			SetState(State::Fault);
			return { Status::Error, StormByte::ByteSize{0} };
		}
		m_offset = StormByte::ByteSize{0};
		return { Status::Ok, StormByte::ByteSize{0} };
	}

	Buffer::IO::Result BufferedRemoteFileWriter::OriginSeek(const StormByte::ByteSize absolute) {
		m_offset = absolute;
		return { Status::Ok, StormByte::ByteSize{0} };
	}

	StormByte::ByteSize BufferedRemoteFileWriter::OriginSize() const noexcept {
		auto plane = m_plane.lock();
		if (!plane || plane->Failed()) {
			const_cast<BufferedRemoteFileWriter*>(this)->SetState(State::Fault);
			return StormByte::ByteSize{0};
		}
		Message request;
		request.opcode = Opcode::Size;
		request.token = m_token;
		auto response = plane->Exchange(std::move(request));
		if (!response || response->status != RemoteStatus::Ok || !response->data.empty()) {
			const_cast<BufferedRemoteFileWriter*>(this)->SetState(State::Fault);
			return StormByte::ByteSize{0};
		}
		const StormByte::ByteSize origin_size{response->value};
		const StormByte::ByteSize logical_size = Tell();
		return origin_size > logical_size ? origin_size : logical_size;
	}

	void BufferedRemoteFileWriter::MarkFailed() noexcept {
		SetState(State::Fault);
	}

	bool BufferedRemoteFileWriter::ReleaseToken() noexcept {
		if (m_token_released) return true;
		m_token_released = true;
		if (auto plane = m_plane.lock(); plane && plane->ReleaseToken(m_token)) return true;
		SetState(State::Fault);
		return false;
	}

	void BufferedRemoteFileReader::MarkFailed() noexcept {
		SetState(State::Fault);
	}

	bool BufferedRemoteFileReader::ReleaseToken() noexcept {
		if (m_token_released) return true;
		m_token_released = true;
		if (auto plane = m_plane.lock(); plane && plane->ReleaseToken(m_token)) return true;
		SetState(State::Fault);
		return false;
	}

}
