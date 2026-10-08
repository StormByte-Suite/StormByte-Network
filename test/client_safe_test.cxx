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
#include <StormByte/network/client.hxx>
#include <StormByte/test_handlers.h>
#include <StormByte/type_traits.hxx>

#include <iostream>
#include <type_traits>
#include <utility>

/**
 * @brief State-free application client owned by this test provider.
 * @details All base lifecycle operations remain in Network. No callbacks retain
 * borrowed arguments; Base, Network and Logger outlive every test owner.
 */
class ClientFixture final: public StormByte::Network::Client {
	public:
		/**
		 * @brief Construct an offline client with an empty packet decoder.
		 */
		ClientFixture(): Client({}, StormByte::Safe::Shared<StormByte::Logger::Log>::MakePointer<StormByte::Logger::ThreadedLog>(
			std::cerr, StormByte::Logger::Level::Error)) {}

		/**
		 * @brief Disable copying of Network owners.
		 * @param other Source fixture.
		 */
		ClientFixture(const ClientFixture& other) = delete;

		/**
		 * @brief Transfer inactive Network state.
		 * @param other Source fixture.
		 */
		ClientFixture(ClientFixture&& other) noexcept = default;

		/**
		 * @brief Release the client in this provider.
		 */
		~ClientFixture() noexcept override = default;

		/**
		 * @brief Disable copy assignment.
		 * @param other Source fixture.
		 * @return No value; this operation is deleted.
		 */
		ClientFixture& operator=(const ClientFixture& other) = delete;

		/**
		 * @brief Disconnect the destination and transfer Network state.
		 * @param other Source fixture.
		 * @return This fixture.
		 */
		ClientFixture& operator=(ClientFixture&& other) noexcept = default;

		/**
		 * @brief Expose the default incoming opcode admission policy.
		 */
		using Client::AllowIncomingOpcode;

		/**
		 * @brief Expose the default outgoing opcode admission policy.
		 */
		using Client::AllowOutgoingOpcode;

		/**
		 * @brief Expose one-time public pipeline configuration for offline checks.
		 */
		using Client::ConfigurePipelines;

		/**
		 * @brief Expose the public reader factory without private construction.
		 */
		using Client::CreateRemoteFileReader;

		/**
		 * @brief Expose the public writer factory without private construction.
		 */
		using Client::CreateRemoteFileWriter;

		/**
		 * @brief Expose application send for offline failure checks.
		 */
		using Client::Send;
};

STORMBYTE_DECLARE_MAYBE_SAFE(ClientFixture);

using namespace StormByte;

// -------------------
// Classification
// -------------------

int test_maybe_safe_traits() {
	static_assert(Type::MaybeSafe<Network::Client>);
	static_assert(Type::MaybeSafe<const Network::Client&>);
	static_assert(Type::MaybeSafe<ClientFixture>);
	static_assert(Type::MaybeSafe<Safe::Shared<Network::Client>>);
	static_assert(!Type::SafeValue<Network::Client>);
	static_assert(!std::is_copy_constructible_v<ClientFixture>);
	static_assert(!std::is_copy_assignable_v<ClientFixture>);
	static_assert(std::is_nothrow_move_constructible_v<ClientFixture>);
	static_assert(std::is_nothrow_move_assignable_v<ClientFixture>);
	static_assert(std::has_virtual_destructor_v<Network::Client>);
	RETURN_TEST(0);
}

// -------------------
// Interface
// -------------------

int test_offline_operations() {
	/**
	 * @brief Concrete empty request for disconnected send checks.
	 */
	class EmptyPacket final: public Network::Transport::Packet {
		public:
			/**
			 * @brief Construct an application request with no payload.
			 */
			EmptyPacket(): Packet(PROCESS_THRESHOLD) {}

		protected:
			/**
			 * @brief Serialize the empty request payload.
			 * @return Empty Base-owned binary value.
			 */
			Safe::Binary DoSerialize() const noexcept override {
				return {};
			}
	};

	ClientFixture client;
	ASSERT_EQUAL(Network::Connection::Status::Disconnected, client.Status());
	ASSERT_NOT_NULL(client.Telemetry().get());
	ASSERT_TRUE(client.AllowIncomingOpcode("offline", Network::Transport::Packet::PROCESS_THRESHOLD));
	ASSERT_TRUE(client.AllowOutgoingOpcode("offline", Network::Transport::Packet::PROCESS_THRESHOLD));
	ASSERT_FALSE(client.ConfigurePipelines({}, {}));
	EmptyPacket packet;
	ASSERT_NULL(client.Send(packet).get());
	ASSERT_EQUAL(Network::Connection::Status::Disconnected, client.Status());
	RETURN_TEST(0);
}

int test_public_signatures() {
	using Client = Network::Client;
	static_assert(Type::SameAs<decltype(&Client::Connect), bool (Client::*)(const Network::Connection::Protocol&,
		std::string_view, const unsigned short&)>);
	static_assert(Type::SameAs<decltype(&Client::Disconnect), void (Client::*)() noexcept>);
	static_assert(Type::SameAs<decltype(&Client::Status), Network::Connection::Status (Client::*)() const noexcept>);
	static_assert(Type::SameAs<decltype(&Client::Telemetry), Safe::Shared<Network::ClientTelemetry> (Client::*)() const noexcept>);
	RETURN_TEST(0);
}

int test_rejected_remote_factories() {
	ClientFixture client;
	const Network::RemoteFileMount mounts[]{Network::RemoteFileMount::Failed(), Network::RemoteFileMount::FileBeingRead(),
		Network::RemoteFileMount::FileBeingWritten(), Network::RemoteFileMount::NotAuthorized(), Network::RemoteFileMount::Unavailable()};
	for (const auto& mount: mounts) {
		ASSERT_NULL(client.CreateRemoteFileReader(mount).get());
		ASSERT_NULL(client.CreateRemoteFileWriter(mount).get());
	}
	ASSERT_EQUAL(Network::Connection::Status::Disconnected, client.Status());
	RETURN_TEST(0);
}

// -------------------
// Lifecycle
// -------------------

int test_move_assignment() {
	ClientFixture source;
	ClientFixture destination;
	auto retained = source.Telemetry();
	Safe::Weak<Network::ClientTelemetry> replaced = destination.Telemetry();
	ASSERT_EQUAL(&destination, &(destination = std::move(source)));
	ASSERT_EQUAL(retained.get(), destination.Telemetry().get());
	ASSERT_TRUE(replaced.expired());
	ASSERT_NULL(source.Telemetry().get());
	ASSERT_EQUAL(Network::Connection::Status::Disconnected, source.Status());
	ASSERT_EQUAL(Network::Connection::Status::Disconnected, destination.Status());
	source.Disconnect();
	destination.Disconnect();
	RETURN_TEST(0);
}

int test_move_construction() {
	ClientFixture source;
	auto retained = source.Telemetry();
	ClientFixture destination{std::move(source)};
	ASSERT_EQUAL(retained.get(), destination.Telemetry().get());
	ASSERT_NULL(source.Telemetry().get());
	ASSERT_EQUAL(Network::Connection::Status::Disconnected, source.Status());
	ASSERT_EQUAL(Network::Connection::Status::Disconnected, destination.Status());
	source.Disconnect();
	destination.Disconnect();
	RETURN_TEST(0);
}

int test_self_move_assignment() {
	ClientFixture client;
	auto retained = client.Telemetry();
	Network::Client& alias = client;
	auto* source = &alias;
	ASSERT_EQUAL(&alias, &(alias = std::move(*source)));
	ASSERT_EQUAL(retained.get(), client.Telemetry().get());
	ASSERT_EQUAL(Network::Connection::Status::Disconnected, client.Status());
	RETURN_TEST(0);
}

int test_shutdown_idempotence() {
	ClientFixture client;
	auto retained = client.Telemetry();
	client.Disconnect();
	client.Disconnect();
	ASSERT_EQUAL(Network::Connection::Status::Disconnected, client.Status());
	ASSERT_EQUAL(retained.get(), client.Telemetry().get());
	RETURN_TEST(0);
}

// -------------------
// Ownership
// -------------------

int test_shared_factory() {
	auto owner = Safe::Shared<Network::Client>::MakePointer<ClientFixture>();
	Safe::Weak<Network::Client> observer = owner;
	auto retained = owner->Telemetry();
	ASSERT_NOT_NULL(owner.get());
	ASSERT_NOT_NULL(Safe::DynamicPointerCast<ClientFixture>(owner).get());
	owner->Disconnect();
	owner.reset();
	ASSERT_TRUE(observer.expired());
	ASSERT_NOT_NULL(retained.get());
	RETURN_TEST(0);
}

int main() {
	int result = 0;

	// -------------------
	// Classification
	// -------------------
	result += test_maybe_safe_traits();

	// -------------------
	// Interface
	// -------------------
	result += test_offline_operations();
	result += test_public_signatures();
	result += test_rejected_remote_factories();

	// -------------------
	// Lifecycle
	// -------------------
	result += test_move_assignment();
	result += test_move_construction();
	result += test_self_move_assignment();
	result += test_shutdown_idempotence();

	// -------------------
	// Ownership
	// -------------------
	result += test_shared_factory();

	RETURN_TEST(result);
}