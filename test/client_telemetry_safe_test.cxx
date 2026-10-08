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
#include <StormByte/network/client_telemetry.hxx>
#include <StormByte/network/telemetry.hxx>
#include <StormByte/safe/pointers.hxx>
#include <StormByte/safe/string.hxx>
#include <StormByte/size.hxx>
#include <StormByte/telemetry.hxx>
#include <StormByte/test_handlers.h>
#include <StormByte/type_traits.hxx>

#include <chrono>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

using namespace StormByte;
using namespace StormByte::Network;

namespace {
	/**
	 * @brief State-free client leaf owned by this test provider.
	 * @details Base disables copying; this leaf disables moving through its
	 * declared destructor. All owners are released while providers remain loaded.
	 */
	class ClientFixture final: public Client {
		public:
			/**
			 * @brief Construct a disconnected client without a decoder or logger.
			 */
			ClientFixture(): Client({}, {}) {}

			/**
			 * @brief Release the disconnected client in its provider.
			 */
			~ClientFixture() noexcept override = default;

			/**
			 * @brief Submit a packet through the application-facing send hook.
			 * @param packet Request packet borrowed for this call.
			 * @return Response packet, or an empty handle while disconnected.
			 */
			PacketPointer Request(const Transport::Packet& packet) noexcept {
				return Send(packet);
			}
	};

	/**
	 * @brief State-free stack packet used for disconnected requests.
	 */
	class EmptyPacket final: public Transport::Packet {
		public:
			/**
			 * @brief Construct an application packet with no payload.
			 */
			EmptyPacket() noexcept: Packet(PROCESS_THRESHOLD) {}

		private:
			/**
			 * @brief Produce the empty public binary payload.
			 * @return Empty Base-owned binary value.
			 */
			Safe::Binary DoSerialize() const noexcept override {
				return {};
			}
	};
}

STORMBYTE_DECLARE_MAYBE_SAFE(ClientFixture);

// -------------------
// Construct
// -------------------

int test_initial_getters() {
	const ClientTelemetry telemetry;
	ASSERT_EQUAL(Size{0}, telemetry.ConnectionAttempts());
	ASSERT_EQUAL(Size{0}, telemetry.ConnectionsEstablished());
	ASSERT_EQUAL(Size{0}, telemetry.ConnectionFailures());
	ASSERT_FALSE(telemetry.Connected());
	ASSERT_EQUAL(Size{0}, telemetry.Requests());
	ASSERT_EQUAL(Size{0}, telemetry.Responses());
	ASSERT_EQUAL(Size{0}, telemetry.RequestsWithoutResponse());
	ASSERT_EQUAL(Size{0}, telemetry.RequestLatencySamples());
	ASSERT_EQUAL(std::chrono::microseconds{0}, telemetry.MeanRequestLatency());
	RETURN_TEST(0);
}

int test_maybe_safe_traits() {
	static_assert(Type::MaybeSafe<ClientTelemetry>);
	static_assert(Type::MaybeSafe<const ClientTelemetry&>);
	static_assert(Type::MaybeSafe<Safe::Shared<ClientTelemetry>>);
	static_assert(Type::MaybeSafe<Safe::Weak<ClientTelemetry>>);
	static_assert(!Type::IsSafe<ClientTelemetry>::value);
	static_assert(Type::DerivedFrom<ClientTelemetry, Network::Telemetry>);
	static_assert(std::has_virtual_destructor_v<ClientTelemetry>);
	static_assert(std::is_nothrow_default_constructible_v<ClientTelemetry>);
	static_assert(!std::is_copy_constructible_v<ClientTelemetry>);
	static_assert(!std::is_copy_assignable_v<ClientTelemetry>);
	RETURN_TEST(0);
}

int test_public_getter_types() {
	const ClientTelemetry telemetry;
	ASSERT_TRUE((Type::SameAs<decltype(telemetry.ConnectionAttempts()), Size>));
	ASSERT_TRUE((Type::SameAs<decltype(telemetry.ConnectionsEstablished()), Size>));
	ASSERT_TRUE((Type::SameAs<decltype(telemetry.ConnectionFailures()), Size>));
	ASSERT_TRUE((Type::SameAs<decltype(telemetry.Requests()), Size>));
	ASSERT_TRUE((Type::SameAs<decltype(telemetry.Responses()), Size>));
	ASSERT_TRUE((Type::SameAs<decltype(telemetry.RequestsWithoutResponse()), Size>));
	ASSERT_TRUE((Type::SameAs<decltype(telemetry.RequestLatencySamples()), Size>));
	ASSERT_TRUE((Type::SameAs<decltype(telemetry.Connected()), bool>));
	ASSERT_TRUE((Type::SameAs<decltype(telemetry.MeanRequestLatency()), std::chrono::microseconds>));
	ASSERT_TRUE(noexcept(telemetry.ConnectionAttempts()));
	ASSERT_TRUE(noexcept(telemetry.ConnectionsEstablished()));
	ASSERT_TRUE(noexcept(telemetry.ConnectionFailures()));
	ASSERT_TRUE(noexcept(telemetry.Connected()));
	ASSERT_TRUE(noexcept(telemetry.Requests()));
	ASSERT_TRUE(noexcept(telemetry.Responses()));
	ASSERT_TRUE(noexcept(telemetry.RequestsWithoutResponse()));
	ASSERT_TRUE(noexcept(telemetry.RequestLatencySamples()));
	ASSERT_TRUE(noexcept(telemetry.MeanRequestLatency()));
	RETURN_TEST(0);
}

// -------------------
// Conversion
// -------------------

int test_polymorphic_strings() {
	const ClientTelemetry telemetry;
	const Network::Telemetry& network = telemetry;
	const StormByte::Telemetry& base = telemetry;
	const Safe::String direct = static_cast<Safe::String>(telemetry);
	const Safe::String through_network = static_cast<Safe::String>(network);
	const Safe::String through_base = static_cast<Safe::String>(base);
	const std::string_view expected =
		"ConnectAttempts=0 ConnectionsEstablished=0 ConnectionFailures=0 Connected=false "
		"Requests=0 Responses=0 RequestsWithoutResponse=0 RequestLatencySamples=0 MeanRequestLatencyUs=0";
	ASSERT_EQUAL(expected, std::string_view{direct});
	ASSERT_EQUAL(expected, std::string_view{through_network});
	ASSERT_EQUAL(expected, std::string_view{through_base});
	ASSERT_EQUAL(std::string{expected}, static_cast<std::string>(telemetry));
	ASSERT_EQUAL(std::string{expected}, static_cast<std::string>(network));
	ASSERT_EQUAL(std::string{expected}, static_cast<std::string>(base));
	ASSERT_EQUAL(Size{0}, telemetry.ConnectionAttempts());
	ASSERT_FALSE(telemetry.Connected());
	RETURN_TEST(0);
}

// -------------------
// Ownership
// -------------------

int test_client_owns_live_telemetry() {
	Safe::Shared<ClientTelemetry> retained;
	Safe::String text;
	{
		auto client = Safe::MakeShared<ClientFixture>();
		retained = client->Telemetry();
		ASSERT_NOT_NULL(retained.get());
		ASSERT_EQUAL(retained.get(), client->Telemetry().get());
		text = static_cast<Safe::String>(*retained);
		client->Disconnect();
		client->Disconnect();
		ASSERT_FALSE(retained->Connected());
		client.reset();
	}
	ASSERT_EQUAL(Size{0}, retained->ConnectionAttempts());
	ASSERT_EQUAL(Size{0}, retained->Requests());
	const Safe::String current = static_cast<Safe::String>(*retained);
	ASSERT_EQUAL(std::string_view{text}, std::string_view{current});
	retained.reset();
	ASSERT_NOT_EMPTY(text);
	RETURN_TEST(0);
}

int test_shared_factory() {
	auto owner = Safe::Shared<ClientTelemetry>::MakePointer<ClientTelemetry>();
	Safe::Weak<ClientTelemetry> observer = owner;
	Safe::Shared<Network::Telemetry> network = owner;
	Safe::Shared<StormByte::Telemetry> base = network;
	ASSERT_NOT_NULL(owner.get());
	ASSERT_EQUAL(3l, owner.use_count());
	ASSERT_EQUAL(Size{0}, owner->ConnectionAttempts());
	const Safe::String retained = static_cast<Safe::String>(*owner);
	const std::string expected = static_cast<std::string>(retained);
	owner.reset();
	network.reset();
	ASSERT_FALSE(observer.expired());
	ASSERT_EQUAL(1l, base.use_count());
	ASSERT_EQUAL(expected, static_cast<std::string>(*base));
	auto moved = std::move(base);
	ASSERT_NULL(base.get());
	ASSERT_EQUAL(expected, static_cast<std::string>(*moved));
	moved.reset();
	ASSERT_TRUE(observer.expired());
	ASSERT_NULL(observer.lock().get());
	ASSERT_EQUAL(expected, static_cast<std::string>(retained));
	RETURN_TEST(0);
}

int test_unique_factory() {
	auto owner = Safe::MakeUnique<ClientTelemetry>();
	ASSERT_NOT_NULL(owner.get());
	ASSERT_EQUAL(Size{0}, owner->Requests());
	const Safe::String retained = static_cast<Safe::String>(*owner);
	Safe::Unique<Network::Telemetry> network = std::move(owner);
	ASSERT_NULL(owner.get());
	Safe::Unique<StormByte::Telemetry> base = std::move(network);
	ASSERT_NULL(network.get());
	ASSERT_NOT_NULL(base.get());
	const Safe::String current = static_cast<Safe::String>(*base);
	ASSERT_EQUAL(std::string_view{retained}, std::string_view{current});
	base.reset();
	ASSERT_NULL(base.get());
	ASSERT_NOT_EMPTY(retained);
	RETURN_TEST(0);
}

// -------------------
// Requests
// -------------------

int test_disconnected_request_preserves_counters() {
	ClientFixture client;
	EmptyPacket packet;
	const auto telemetry = client.Telemetry();
	ASSERT_NOT_NULL(telemetry.get());
	ASSERT_NULL(client.Request(packet).get());
	ASSERT_NULL(client.Request(packet).get());
	ASSERT_EQUAL(Size{0}, telemetry->Requests());
	ASSERT_EQUAL(Size{0}, telemetry->Responses());
	ASSERT_EQUAL(Size{0}, telemetry->RequestsWithoutResponse());
	ASSERT_EQUAL(Size{0}, telemetry->RequestLatencySamples());
	ASSERT_EQUAL(std::chrono::microseconds{0}, telemetry->MeanRequestLatency());
	ASSERT_FALSE(telemetry->Connected());
	RETURN_TEST(0);
}

int main() {
	int result = 0;

	// -------------------
	// Construct
	// -------------------
	result += test_initial_getters();
	result += test_maybe_safe_traits();
	result += test_public_getter_types();

	// -------------------
	// Conversion
	// -------------------
	result += test_polymorphic_strings();

	// -------------------
	// Ownership
	// -------------------
	result += test_client_owns_live_telemetry();
	result += test_shared_factory();
	result += test_unique_factory();

	// -------------------
	// Requests
	// -------------------
	result += test_disconnected_request_preserves_counters();
	RETURN_TEST(result);
}
