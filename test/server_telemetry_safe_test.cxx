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

#include <StormByte/network/server_telemetry.hxx>
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

// -------------------
// Construct
// -------------------

int test_initial_getters() {
	const ServerTelemetry telemetry;
	ASSERT_EQUAL(Size{0}, telemetry.CurrentConnections());
	ASSERT_EQUAL(Size{0}, telemetry.AcceptedConnections());
	ASSERT_EQUAL(Size{0}, telemetry.ClosedConnections());
	ASSERT_EQUAL(Size{0}, telemetry.PeakConnections());
	ASSERT_EQUAL(Size{0}, telemetry.PacketsDispatched());
	ASSERT_EQUAL(Size{0}, telemetry.HandlersCompleted());
	ASSERT_EQUAL(Size{0}, telemetry.HandlersWithoutResponse());
	ASSERT_EQUAL(Size{0}, telemetry.HandlerErrors());
	ASSERT_EQUAL(Size{0}, telemetry.HandlerLatencySamples());
	ASSERT_EQUAL(std::chrono::microseconds{0}, telemetry.MeanHandlerLatency());
	ASSERT_EQUAL(Size{0}, telemetry.WorkerQueueBackpressureEvents());
	RETURN_TEST(0);
}

int test_maybe_safe_traits() {
	static_assert(Type::MaybeSafe<ServerTelemetry>);
	static_assert(Type::MaybeSafe<const ServerTelemetry&>);
	static_assert(Type::MaybeSafe<Safe::Shared<ServerTelemetry>>);
	static_assert(Type::MaybeSafe<Safe::Weak<ServerTelemetry>>);
	static_assert(!Type::IsSafe<ServerTelemetry>::value);
	static_assert(Type::DerivedFrom<ServerTelemetry, Network::Telemetry>);
	static_assert(std::has_virtual_destructor_v<ServerTelemetry>);
	static_assert(std::is_nothrow_default_constructible_v<ServerTelemetry>);
	static_assert(!std::is_copy_constructible_v<ServerTelemetry>);
	static_assert(!std::is_copy_assignable_v<ServerTelemetry>);
	RETURN_TEST(0);
}

int test_public_getter_types() {
	const ServerTelemetry telemetry;
	ASSERT_TRUE((Type::SameAs<decltype(telemetry.CurrentConnections()), Size>));
	ASSERT_TRUE((Type::SameAs<decltype(telemetry.AcceptedConnections()), Size>));
	ASSERT_TRUE((Type::SameAs<decltype(telemetry.ClosedConnections()), Size>));
	ASSERT_TRUE((Type::SameAs<decltype(telemetry.PeakConnections()), Size>));
	ASSERT_TRUE((Type::SameAs<decltype(telemetry.PacketsDispatched()), Size>));
	ASSERT_TRUE((Type::SameAs<decltype(telemetry.HandlersCompleted()), Size>));
	ASSERT_TRUE((Type::SameAs<decltype(telemetry.HandlersWithoutResponse()), Size>));
	ASSERT_TRUE((Type::SameAs<decltype(telemetry.HandlerErrors()), Size>));
	ASSERT_TRUE((Type::SameAs<decltype(telemetry.HandlerLatencySamples()), Size>));
	ASSERT_TRUE((Type::SameAs<decltype(telemetry.WorkerQueueBackpressureEvents()), Size>));
	ASSERT_TRUE((Type::SameAs<decltype(telemetry.MeanHandlerLatency()), std::chrono::microseconds>));
	ASSERT_TRUE(noexcept(telemetry.CurrentConnections()));
	ASSERT_TRUE(noexcept(telemetry.AcceptedConnections()));
	ASSERT_TRUE(noexcept(telemetry.ClosedConnections()));
	ASSERT_TRUE(noexcept(telemetry.PeakConnections()));
	ASSERT_TRUE(noexcept(telemetry.PacketsDispatched()));
	ASSERT_TRUE(noexcept(telemetry.HandlersCompleted()));
	ASSERT_TRUE(noexcept(telemetry.HandlersWithoutResponse()));
	ASSERT_TRUE(noexcept(telemetry.HandlerErrors()));
	ASSERT_TRUE(noexcept(telemetry.HandlerLatencySamples()));
	ASSERT_TRUE(noexcept(telemetry.MeanHandlerLatency()));
	ASSERT_TRUE(noexcept(telemetry.WorkerQueueBackpressureEvents()));
	RETURN_TEST(0);
}

// -------------------
// Conversion
// -------------------

int test_polymorphic_strings() {
	const ServerTelemetry telemetry;
	const Network::Telemetry& network = telemetry;
	const StormByte::Telemetry& base = telemetry;
	const Safe::String direct = static_cast<Safe::String>(telemetry);
	const Safe::String through_network = static_cast<Safe::String>(network);
	const Safe::String through_base = static_cast<Safe::String>(base);
	const std::string_view expected =
		"CurrentConnections=0 AcceptedConnections=0 ClosedConnections=0 PeakConnections=0 "
		"PacketsDispatched=0 HandlersCompleted=0 HandlersWithoutResponse=0 HandlerErrors=0 HandlerLatencySamples=0 "
		"MeanHandlerLatencyUs=0 WorkerQueueBackpressureEvents=0";
	ASSERT_EQUAL(expected, std::string_view{direct});
	ASSERT_EQUAL(expected, std::string_view{through_network});
	ASSERT_EQUAL(expected, std::string_view{through_base});
	ASSERT_EQUAL(std::string{expected}, static_cast<std::string>(telemetry));
	ASSERT_EQUAL(std::string{expected}, static_cast<std::string>(network));
	ASSERT_EQUAL(std::string{expected}, static_cast<std::string>(base));
	ASSERT_EQUAL(Size{0}, telemetry.CurrentConnections());
	ASSERT_EQUAL(Size{0}, telemetry.HandlerLatencySamples());
	RETURN_TEST(0);
}

// -------------------
// Ownership
// -------------------

int test_shared_factory() {
	auto owner = Safe::Shared<ServerTelemetry>::MakePointer<ServerTelemetry>();
	Safe::Weak<ServerTelemetry> observer = owner;
	Safe::Shared<Network::Telemetry> network = owner;
	Safe::Shared<StormByte::Telemetry> base = network;
	ASSERT_NOT_NULL(owner.get());
	ASSERT_EQUAL(3l, owner.use_count());
	ASSERT_EQUAL(Size{0}, owner->CurrentConnections());
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
	auto owner = Safe::MakeUnique<ServerTelemetry>();
	ASSERT_NOT_NULL(owner.get());
	ASSERT_EQUAL(Size{0}, owner->CurrentConnections());
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
	result += test_shared_factory();
	result += test_unique_factory();
	RETURN_TEST(result);
}
