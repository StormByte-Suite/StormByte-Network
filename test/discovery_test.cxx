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
#include <StormByte/network/discovery.hxx>
#include <StormByte/network/server.hxx>
#include <StormByte/safe/binary.hxx>
#include <StormByte/safe/function.hxx>
#include <StormByte/safe/string.hxx>

#include <chrono>
#include <iostream>
#include <string>
#include <string_view>
#include <thread>

namespace DiscoveryExample {
	using namespace StormByte;
	using namespace StormByte::Network;

	constexpr unsigned short tcp_port = 7195;
	constexpr unsigned short discovery_port = 42424;
	constexpr std::string_view service_id = "FIND:my-service";

	class Server final: public Network::Server {
		public:
			Server(): Network::Server({}, {}) {}
			~Server() noexcept override {
				StopAnnounce();
				Disconnect();
			}

			bool Start() {
				if (!Connect(Connection::Protocol::IPv4, "0.0.0.0", tcp_port))
					return false;
				return Announce(Connection::Protocol::IPv4, discovery_port);
			}

		protected:
			PacketPointer ProcessClientPacket(std::string_view, PacketPointer) noexcept override {
				return {};
			}

			Safe::Binary OnDiscoveryQuery(const Safe::Binary& query) const noexcept override {
				const std::string_view text(reinterpret_cast<const char*>(query.data()), query.size());
				if (text != service_id)
					return {};
				const std::string reply = "HERE:my-service:" + std::to_string(tcp_port);
				return Safe::Binary(reinterpret_cast<const std::byte*>(reply.data()), reply.size());
			}
	};

	class Client final: public Network::Client {
		public:
			Client(): Network::Client({}, {}) {}

			bool Find() {
				bool found = false;
				auto invoke = [](void* context, bool* output, const DiscoveryReply& reply) noexcept {
					auto* found = static_cast<bool*>(context);
					const std::string_view payload(
						reinterpret_cast<const char*>(reply.payload.data()), reply.payload.size());
					std::cout << "discovered from " << std::string_view{reply.from_address}
						<< ":" << reply.from_port << " payload=\"" << payload << "\"\n";
					Safe::String address;
					unsigned short port = 0;
					if (!InterpretReply(reply.payload, address, port)) {
						*output = false;
						return Safe::Status::Success;
					}
					std::cout << "accepted server at " << std::string_view{address}
						<< ":" << port << "\n";
					*found = true;
					*output = true;
					return Safe::Status::Success;
				};
				auto clone = [](const void* context) noexcept -> void* {
					return const_cast<void*>(context);
				};
				auto release = [](void*) noexcept {};
				Safe::Function<bool(const DiscoveryReply&)> on_reply(
					&found, +invoke, +clone, +release);
				const bool sent = Discover(Connection::Protocol::IPv4, discovery_port,
					std::chrono::milliseconds(2000), std::move(on_reply));
				std::cerr << "[test] Discover returned " << sent << " found=" << found << "\n";
				return sent && found;
			}

		private:
			static bool InterpretReply(const Safe::Binary& reply,
				Safe::String& address, unsigned short& port) {
				const std::string_view text(reinterpret_cast<const char*>(reply.data()), reply.size());
				constexpr std::string_view prefix = "HERE:my-service:";
				if (!text.starts_with(prefix))
					return false;
				try {
					port = static_cast<unsigned short>(std::stoi(std::string(text.substr(prefix.size()))));
				} catch (...) {
					return false;
				}
				address = Safe::String{"127.0.0.1"};
				return port != 0;
			}

		protected:
			Safe::Binary MakeDiscoveryQuery() const noexcept override {
				return Safe::Binary(reinterpret_cast<const std::byte*>(service_id.data()), service_id.size());
			}

			bool InterpretDiscoveryReply(const Safe::Binary& reply,
				Safe::String& address, unsigned short& port) const noexcept override {
				const std::string_view text(reinterpret_cast<const char*>(reply.data()), reply.size());
				constexpr std::string_view prefix = "HERE:my-service:";
				if (!text.starts_with(prefix))
					return false;
				try {
					port = static_cast<unsigned short>(std::stoi(std::string(text.substr(prefix.size()))));
				} catch (...) {
					return false;
				}
				address = Safe::String{"127.0.0.1"};
				return port != 0;
			}
	};
}

int main() {
	DiscoveryExample::Server server;
	if (!server.Start()) {
		std::cerr << "failed to start announcing server\n";
		return 1;
	}
	std::this_thread::sleep_for(std::chrono::milliseconds(100));

	DiscoveryExample::Client client;
	if (!client.Find()) {
		std::cerr << "discovery did not find the announced server\n";
		return 1;
	}
	std::cout << "discovery ok\n";
	return 0;
}
