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

#include <StormByte/logger/threaded_log.hxx>
#include <StormByte/network/server.hxx>
#include <StormByte/test_handlers.h>
#include <StormByte/type_traits.hxx>

#include <iostream>
#include <type_traits>
#include <utility>

/**
 * @brief State-free application server owned by this test provider.
 * @details Exact derived allocation uses this provider's Safe declaration.
 * Disconnect precedes derived destruction; Base, Network and Logger remain
 * loaded until every owner and retained telemetry handle has been released.
 * Loopback tests bind ephemeral ports and never retain borrowed handler data.
 */
class ServerFixture final: public StormByte::Network::Server {
	public:
		/**
		 * @brief Construct with an empty decoder and an error logger.
		 */
		ServerFixture(): ServerFixture({},
			StormByte::Safe::Shared<StormByte::Logger::Log>::MakePointer<StormByte::Logger::ThreadedLog>(
				std::cerr, StormByte::Logger::Level::Error)) {}

		/**
		 * @brief Construct with explicit application configuration.
		 * @param decoder Caller-owned application packet decoder.
		 * @param logger Shared diagnostic logger retained by the server.
		 */
		ServerFixture(StormByte::Network::DeserializePacketFunction decoder,
			StormByte::Safe::Shared<StormByte::Logger::Log> logger):
			Server(std::move(decoder), std::move(logger)) {}

		/**
		 * @brief Disable copying of Network owners.
		 * @param other Source fixture.
		 */
		ServerFixture(const ServerFixture& other) = delete;

		/**
		 * @brief Stop the source listener and transfer inactive Network state.
		 * @param other Source fixture, moved only from the test thread.
		 */
		ServerFixture(ServerFixture&& other) noexcept = default;

		/**
		 * @brief Join Network callbacks before derived destruction.
		 */
		~ServerFixture() noexcept override {
			Disconnect();
		}

		/**
		 * @brief Disable copy assignment.
		 * @param other Source fixture.
		 * @return No value; this operation is deleted.
		 */
		ServerFixture& operator=(const ServerFixture& other) = delete;

		/**
		 * @brief Stop both listeners and transfer inactive Network state.
		 * @param other Source fixture, moved only from the test thread.
		 * @return This fixture.
		 */
		ServerFixture& operator=(ServerFixture&& other) noexcept = default;

		/**
		 * @brief Expose default incoming application opcode admission.
		 */
		using Server::AllowIncomingOpcode;

		/**
		 * @brief Expose default outgoing application opcode admission.
		 */
		using Server::AllowOutgoingOpcode;

		/**
		 * @brief Expose application session pipeline configuration.
		 */
		using Server::ConfigureClientPipelines;

		/**
		 * @brief Expose application session disconnection.
		 */
		using Server::DisconnectClient;

		/**
		 * @brief Expose application rejection after a reply.
		 */
		using Server::DisconnectClientAfterReply;

		/**
		 * @brief Expose application read capability mounting.
		 */
		using Server::MountRemoteFileReader;

		/**
		 * @brief Expose application write capability mounting.
		 */
		using Server::MountRemoteFileWriter;

		/**
		 * @brief Expose default application session admission.
		 */
		using Server::OnClientConnected;

		/**
		 * @brief Expose default application session cleanup.
		 */
		using Server::OnClientDisconnected;

	private:
		/**
		 * @brief Provide a stateless handler without a response.
		 * @param client_uuid Borrowed sender identity, not retained.
		 * @param packet Received packet, released in this call.
		 * @return Empty response.
		 */
		StormByte::Network::PacketPointer ProcessClientPacket(std::string_view client_uuid,
			StormByte::Network::PacketPointer packet) noexcept override {
			(void)client_uuid;
			(void)packet;
			return {};
		}
};

STORMBYTE_DECLARE_MAYBE_SAFE(ServerFixture);

using namespace StormByte;

// -------------------
// Classification
// -------------------

int test_maybe_safe_traits() {
	static_assert(Type::MaybeSafe<Network::Server>);
	static_assert(Type::MaybeSafe<const Network::Server&>);
	static_assert(Type::MaybeSafe<ServerFixture>);
	static_assert(Type::MaybeSafe<Safe::Shared<Network::Server>>);
	static_assert(!Type::SafeValue<Network::Server>);
	static_assert(!Type::SafeValue<ServerFixture>);
	static_assert(std::is_abstract_v<Network::Server>);
	static_assert(!std::is_default_constructible_v<Network::Server>);
	static_assert(!std::is_copy_constructible_v<ServerFixture>);
	static_assert(!std::is_copy_assignable_v<ServerFixture>);
	static_assert(std::is_nothrow_move_constructible_v<ServerFixture>);
	static_assert(std::is_nothrow_move_assignable_v<ServerFixture>);
	static_assert(std::is_nothrow_destructible_v<ServerFixture>);
	static_assert(std::has_virtual_destructor_v<Network::Server>);
	RETURN_TEST(0);
}

// -------------------
// Construct
// -------------------

int test_default_configuration() {
	ServerFixture server;
	ASSERT_EQUAL(Network::Connection::Status::Disconnected, server.Status());
	ASSERT_NOT_NULL(server.Telemetry().get());
	ASSERT_TRUE(server.AllowIncomingOpcode("offline", Network::Transport::Packet::PROCESS_THRESHOLD));
	ASSERT_TRUE(server.AllowOutgoingOpcode("offline", Network::Transport::Packet::PROCESS_THRESHOLD));
	ASSERT_TRUE(server.OnClientConnected("offline"));
	ASSERT_NO_THROW(server.OnClientDisconnected("offline"));
	RETURN_TEST(0);
}

int test_explicit_configuration() {
	auto logger = Safe::Shared<Logger::Log>::MakePointer<Logger::ThreadedLog>(std::cerr, Logger::Level::Error);
	Network::DeserializePacketFunction decoder{[](Network::Transport::Packet::OpcodeType,
		Buffer::Consumer, Safe::Shared<Logger::Log>) -> Network::PacketPointer { return {}; }};
	ServerFixture server{std::move(decoder), logger};
	ASSERT_EQUAL(Network::Connection::Status::Disconnected, server.Status());
	ASSERT_NOT_NULL(server.Telemetry().get());
	ASSERT_TRUE(server.Connect(Network::Connection::Protocol::IPv4, "127.0.0.1", 0));
	ASSERT_EQUAL(Network::Connection::Status::Connected, server.Status());
	server.Disconnect();
	ASSERT_EQUAL(Network::Connection::Status::Disconnected, server.Status());
	RETURN_TEST(0);
}

int test_invalid_bind_configuration() {
	ServerFixture server;
	auto retained = server.Telemetry();
	ASSERT_FALSE(server.Connect(Network::Connection::Protocol::IPv4, "::1", 0));
	ASSERT_EQUAL(Network::Connection::Status::Disconnected, server.Status());
	ASSERT_EQUAL(retained.get(), server.Telemetry().get());
	server.Disconnect();
	ASSERT_TRUE(server.Connect(Network::Connection::Protocol::IPv4, "127.0.0.1", 0));
	ASSERT_EQUAL(Network::Connection::Status::Connected, server.Status());
	server.Disconnect();
	RETURN_TEST(0);
}

int test_null_configuration() {
	ServerFixture server{{}, {}};
	ASSERT_EQUAL(Network::Connection::Status::Disconnected, server.Status());
	ASSERT_NOT_NULL(server.Telemetry().get());
	ASSERT_NO_THROW(server.Disconnect());
	RETURN_TEST(0);
}

// -------------------
// Interface
// -------------------

int test_offline_operations() {
	ServerFixture server;
	auto retained = server.Telemetry();
	ASSERT_FALSE(server.ConfigureClientPipelines("", {}, {}));
	ASSERT_FALSE(server.ConfigureClientPipelines("missing-session", {}, {}));
	server.DisconnectClient("");
	server.DisconnectClient("missing-session");
	server.DisconnectClientAfterReply("");
	server.DisconnectClientAfterReply("missing-session");
	ASSERT_EQUAL(Network::Connection::Status::Disconnected, server.Status());
	ASSERT_EQUAL(retained.get(), server.Telemetry().get());
	RETURN_TEST(0);
}

int test_public_signatures() {
	using Server = Network::Server;
	static_assert(Type::SameAs<decltype(&Server::Connect), bool (Server::*)(const Network::Connection::Protocol&,
		std::string_view, const unsigned short&)>);
	static_assert(Type::SameAs<decltype(&Server::Disconnect), void (Server::*)() noexcept>);
	static_assert(Type::SameAs<decltype(&Server::Status), Network::Connection::Status (Server::*)() const noexcept>);
	static_assert(Type::SameAs<decltype(&Server::Telemetry), Safe::Shared<Network::ServerTelemetry> (Server::*)() const noexcept>);
	static_assert(Type::SameAs<decltype(std::declval<Server&>() = std::declval<Server&&>()), Server&>);
	RETURN_TEST(0);
}

int test_rejected_remote_mounts() {
	ServerFixture server;
	for (const std::uint16_t timeout: {std::uint16_t{0}, std::uint16_t{2}, std::uint16_t{3},
		std::uint16_t{30}, std::uint16_t{3600}, std::uint16_t{3601}}) {
		const auto reader = server.MountRemoteFileReader("missing-session", "unused-server-safe-file", timeout);
		const auto writer = server.MountRemoteFileWriter("missing-session", "unused-server-safe-file", timeout);
		ASSERT_EQUAL(Network::RemoteFileMount::Status::Failed, reader.Result());
		ASSERT_EQUAL(Network::RemoteFileMount::Status::Failed, writer.Result());
		ASSERT_EQUAL(Network::RemoteFileMount::Access::None, reader.Mode());
		ASSERT_EQUAL(Network::RemoteFileMount::Access::None, writer.Mode());
		ASSERT_EQUAL(std::uint16_t{0}, reader.Port());
		ASSERT_EQUAL(std::uint16_t{0}, writer.Port());
	}
	ASSERT_EQUAL(Network::RemoteFileMount::Status::Failed, server.MountRemoteFileReader("", "").Result());
	ASSERT_EQUAL(Network::RemoteFileMount::Status::Failed, server.MountRemoteFileWriter("", "").Result());
	ASSERT_EQUAL(Network::Connection::Status::Disconnected, server.Status());
	RETURN_TEST(0);
}

// -------------------
// Lifecycle
// -------------------

int test_listener_restart() {
	ServerFixture server;
	auto retained = server.Telemetry();
	for (int iteration = 0; iteration < 2; ++iteration) {
		ASSERT_TRUE(server.Connect(Network::Connection::Protocol::IPv4, "127.0.0.1", 0));
		ASSERT_EQUAL(Network::Connection::Status::Connected, server.Status());
		ASSERT_FALSE(server.Connect(Network::Connection::Protocol::IPv4, "127.0.0.1", 0));
		ASSERT_EQUAL(Network::Connection::Status::Connected, server.Status());
		server.Disconnect();
		server.Disconnect();
		ASSERT_EQUAL(Network::Connection::Status::Disconnected, server.Status());
		ASSERT_EQUAL(retained.get(), server.Telemetry().get());
	}
	RETURN_TEST(0);
}

int test_move_assignment() {
	ServerFixture source;
	ServerFixture destination;
	auto retained = source.Telemetry();
	Safe::Weak<Network::ServerTelemetry> replaced = destination.Telemetry();
	ASSERT_TRUE(source.Connect(Network::Connection::Protocol::IPv4, "127.0.0.1", 0));
	ASSERT_TRUE(destination.Connect(Network::Connection::Protocol::IPv4, "127.0.0.1", 0));
	ASSERT_EQUAL(&destination, &(destination = std::move(source)));
	ASSERT_EQUAL(retained.get(), destination.Telemetry().get());
	ASSERT_TRUE(replaced.expired());
	ASSERT_NULL(source.Telemetry().get());
	ASSERT_EQUAL(Network::Connection::Status::Disconnected, source.Status());
	ASSERT_EQUAL(Network::Connection::Status::Disconnected, destination.Status());
	source.Disconnect();
	ASSERT_TRUE(source.Connect(Network::Connection::Protocol::IPv4, "127.0.0.1", 0));
	ASSERT_NOT_NULL(source.Telemetry().get());
	ASSERT_NOT_EQUAL(retained.get(), source.Telemetry().get());
	ASSERT_EQUAL(Network::Connection::Status::Connected, source.Status());
	ASSERT_TRUE(destination.Connect(Network::Connection::Protocol::IPv4, "127.0.0.1", 0));
	ASSERT_EQUAL(Network::Connection::Status::Connected, destination.Status());
	source.Disconnect();
	destination.Disconnect();
	RETURN_TEST(0);
}

int test_move_construction() {
	ServerFixture source;
	auto retained = source.Telemetry();
	ASSERT_TRUE(source.Connect(Network::Connection::Protocol::IPv4, "127.0.0.1", 0));
	ServerFixture destination{std::move(source)};
	ASSERT_EQUAL(retained.get(), destination.Telemetry().get());
	ASSERT_NULL(source.Telemetry().get());
	ASSERT_EQUAL(Network::Connection::Status::Disconnected, source.Status());
	ASSERT_EQUAL(Network::Connection::Status::Disconnected, destination.Status());
	source.Disconnect();
	ASSERT_TRUE(source.Connect(Network::Connection::Protocol::IPv4, "127.0.0.1", 0));
	ASSERT_NOT_NULL(source.Telemetry().get());
	ASSERT_NOT_EQUAL(retained.get(), source.Telemetry().get());
	ASSERT_EQUAL(Network::Connection::Status::Connected, source.Status());
	ASSERT_TRUE(destination.Connect(Network::Connection::Protocol::IPv4, "127.0.0.1", 0));
	ASSERT_EQUAL(Network::Connection::Status::Connected, destination.Status());
	source.Disconnect();
	destination.Disconnect();
	RETURN_TEST(0);
}

int test_self_move_assignment() {
	ServerFixture server;
	auto retained = server.Telemetry();
	ASSERT_TRUE(server.Connect(Network::Connection::Protocol::IPv4, "127.0.0.1", 0));
	Network::Server& base = server;
	Network::Server* alias = &server;
	ASSERT_EQUAL(alias, &(base = std::move(*alias)));
	ASSERT_EQUAL(retained.get(), server.Telemetry().get());
	ASSERT_EQUAL(Network::Connection::Status::Connected, server.Status());
	server.Disconnect();
	ASSERT_EQUAL(Network::Connection::Status::Disconnected, server.Status());
	RETURN_TEST(0);
}

int test_shutdown_idempotence() {
	ServerFixture server;
	auto retained = server.Telemetry();
	server.Disconnect();
	server.Disconnect();
	ASSERT_EQUAL(Network::Connection::Status::Disconnected, server.Status());
	ASSERT_EQUAL(retained.get(), server.Telemetry().get());
	RETURN_TEST(0);
}

// -------------------
// Ownership
// -------------------

int test_configuration_ownership() {
	Safe::Weak<Logger::Log> observer;
	{
		auto logger = Safe::Shared<Logger::Log>::MakePointer<Logger::ThreadedLog>(std::cerr, Logger::Level::Error);
		observer = logger;
		Network::DeserializePacketFunction decoder{[retained = logger](Network::Transport::Packet::OpcodeType,
			Buffer::Consumer, Safe::Shared<Logger::Log>) -> Network::PacketPointer {
			(void)retained;
			return {};
		}};
		ServerFixture server{std::move(decoder), logger};
		logger.reset();
		ASSERT_FALSE(observer.expired());
		server.Disconnect();
		ASSERT_FALSE(observer.expired());
	}
	ASSERT_TRUE(observer.expired());
	RETURN_TEST(0);
}

int test_shared_factory_destruction() {
	auto owner = Safe::Shared<Network::Server>::MakePointer<ServerFixture>();
	Safe::Weak<Network::Server> observer = owner;
	auto retained = owner->Telemetry();
	Safe::Weak<Network::ServerTelemetry> telemetry_observer = retained;
	ASSERT_NOT_NULL(owner.get());
	ASSERT_NOT_NULL(Safe::DynamicPointerCast<ServerFixture>(owner).get());
	ASSERT_TRUE(owner->Connect(Network::Connection::Protocol::IPv4, "127.0.0.1", 0));
	ASSERT_EQUAL(Network::Connection::Status::Connected, owner->Status());
	owner.reset();
	ASSERT_TRUE(observer.expired());
	ASSERT_NOT_NULL(retained.get());
	ASSERT_FALSE(telemetry_observer.expired());
	retained.reset();
	ASSERT_TRUE(telemetry_observer.expired());
	RETURN_TEST(0);
}

int main() {
	int result = 0;

	// -------------------
	// Classification
	// -------------------
	result += test_maybe_safe_traits();

	// -------------------
	// Construct
	// -------------------
	result += test_default_configuration();
	result += test_explicit_configuration();
	result += test_invalid_bind_configuration();
	result += test_null_configuration();

	// -------------------
	// Interface
	// -------------------
	result += test_offline_operations();
	result += test_public_signatures();
	result += test_rejected_remote_mounts();

	// -------------------
	// Lifecycle
	// -------------------
	result += test_listener_restart();
	result += test_move_assignment();
	result += test_move_construction();
	result += test_self_move_assignment();
	result += test_shutdown_idempotence();

	// -------------------
	// Ownership
	// -------------------
	result += test_configuration_ownership();
	result += test_shared_factory_destruction();

	RETURN_TEST(result);
}
