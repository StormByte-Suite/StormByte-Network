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

#include <StormByte/network/exception.hxx>
#include <StormByte/safe/pointers.hxx>
#include <StormByte/test_handlers.h>
#include <StormByte/type_traits.hxx>

#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

using namespace StormByte;

namespace {
	/**
	 * @brief Expose the protected path tag without constructing a test exception.
	 */
	struct ExceptionPaths: Network::Exception {
		/**
		 * @brief Borrowed component path accepted by the public constructors.
		 */
		using Path = StormByte::Exception::Path;
	};
}

// -------------------
// Assign
// -------------------

int test_copy_assignment() {
	Network::ConnectionError destination{std::string_view{"old"}};
	{
		Network::ConnectionError source{std::string_view{"copied"}};
		ASSERT_EQUAL(&destination, &(destination = source));
		source = Network::ConnectionError{std::string_view{"changed"}};
	}
	ASSERT_EQUAL(std::string_view{"StormByte.Network.Connection: copied"}, std::string_view{destination.what()});
	RETURN_TEST(0);
}

int test_copy_self_assignment() {
	Network::ConnectionError error{std::string_view{"self {literal}"}};
	const auto* self = &error;
	ASSERT_EQUAL(&error, &(error = *self));
	ASSERT_EQUAL(std::string_view{"StormByte.Network.Connection: self {literal}"}, std::string_view{error.what()});
	RETURN_TEST(0);
}

int test_move_assignment() {
	Network::ConnectionError destination{std::string_view{"old"}};
	{
		Network::ConnectionError source{"moved {}", 42};
		ASSERT_EQUAL(&destination, &(destination = std::move(source)));
		source = Network::ConnectionError{std::string_view{"reused"}};
		ASSERT_EQUAL(std::string_view{"StormByte.Network.Connection: reused"}, std::string_view{source.what()});
	}
	ASSERT_EQUAL(std::string_view{"StormByte.Network.Connection: moved 42"}, std::string_view{destination.what()});
	RETURN_TEST(0);
}

int test_move_self_assignment() {
	Network::ConnectionError error{std::string_view{"self"}};
	auto* self = &error;
	ASSERT_EQUAL(&error, &(error = std::move(*self)));
	error = Network::ConnectionError{std::string_view{"recovered"}};
	ASSERT_EQUAL(std::string_view{"StormByte.Network.Connection: recovered"}, std::string_view{error.what()});
	RETURN_TEST(0);
}

// -------------------
// Construct
// -------------------

int test_copy_constructor() {
	Network::ConnectionError copy = [] {
		Network::ConnectionError source{"copy {}", 7};
		Network::ConnectionError result{source};
		source = Network::ConnectionError{std::string_view{"changed"}};
		return result;
	}();
	ASSERT_EQUAL(std::string_view{"StormByte.Network.Connection: copy 7"}, std::string_view{copy.what()});
	RETURN_TEST(0);
}

int test_embedded_null() {
	const Network::ConnectionError error{std::string_view{"left\0right", 10}};
	constexpr std::string_view expected{"StormByte.Network.Connection: left\0right", 40};
	ASSERT_EQUAL(expected, (std::string_view{error.what(), expected.size()}));
	ASSERT_EQUAL('\0', error.what()[expected.size()]);
	RETURN_TEST(0);
}

int test_empty_message() {
	const Network::ConnectionError error{std::string_view{}};
	ASSERT_NOT_NULL(error.what());
	ASSERT_EQUAL(std::string_view{"StormByte.Network.Connection: "}, std::string_view{error.what()});
	ASSERT_SIZE(std::string_view{error.what()}, 30u);
	RETURN_TEST(0);
}

int test_format_arguments() {
	const Network::ConnectionError error{"peer {}: code {:04x}, retry {}", std::string_view{"alpha"}, 42, true};
	ASSERT_EQUAL(std::string_view{"StormByte.Network.Connection: peer alpha: code 002a, retry true"}, std::string_view{error.what()});
	RETURN_TEST(0);
}

int test_inherited_child_paths() {
	const Network::ConnectionError plain{ExceptionPaths::Path{"Custom.Child"}, std::string_view{"{literal}"}};
	const Network::ConnectionError formatted{ExceptionPaths::Path{"Custom.Child"}, "code {}", 7};
	const Network::ConnectionError empty{ExceptionPaths::Path{}, std::string_view{"empty"}};
	ASSERT_EQUAL(std::string_view{"StormByte.Network.Custom.Child: {literal}"}, std::string_view{plain.what()});
	ASSERT_EQUAL(std::string_view{"StormByte.Network.Custom.Child: code 7"}, std::string_view{formatted.what()});
	ASSERT_EQUAL(std::string_view{"StormByte.Network.: empty"}, std::string_view{empty.what()});
	RETURN_TEST(0);
}

int test_message_is_owned() {
	char message[] = "borrowed {literal}";
	const Network::ConnectionError error{std::string_view{message}};
	message[0] = 'X';
	ASSERT_EQUAL(std::string_view{"StormByte.Network.Connection: borrowed {literal}"}, std::string_view{error.what()});
	RETURN_TEST(0);
}

int test_move_constructor() {
	Network::ConnectionError moved = [] {
		Network::ConnectionError source{std::string_view{"moved"}};
		return Network::ConnectionError{std::move(source)};
	}();
	ASSERT_EQUAL(std::string_view{"StormByte.Network.Connection: moved"}, std::string_view{moved.what()});
	RETURN_TEST(0);
}

int test_plain_string_view() {
	const Network::ConnectionError error{std::string_view{"unmatched { and } plus {}"}};
	ASSERT_EQUAL(std::string_view{"StormByte.Network.Connection: unmatched { and } plus {}"}, std::string_view{error.what()});
	RETURN_TEST(0);
}

int test_safe_string() {
	Safe::String message{"owned {literal}"};
	const Network::ConnectionError error{std::string_view{message}};
	message.clear();
	ASSERT_EQUAL(std::string_view{"StormByte.Network.Connection: owned {literal}"}, std::string_view{error.what()});
	const Network::ConnectionError temporary{std::string_view{Safe::String{"temporary"}}};
	ASSERT_EQUAL(std::string_view{"StormByte.Network.Connection: temporary"}, std::string_view{temporary.what()});
	const Network::ConnectionError literal{"plain {literal}"};
	std::string dynamic{"dynamic {literal}"};
	const Network::ConnectionError text{dynamic.c_str()};
	const Network::ConnectionError standard{dynamic};
	dynamic.clear();
	ASSERT_EQUAL(std::string_view{"StormByte.Network.Connection: plain {literal}"}, std::string_view{literal.what()});
	ASSERT_EQUAL(std::string_view{"StormByte.Network.Connection: dynamic {literal}"}, std::string_view{text.what()});
	ASSERT_EQUAL(std::string_view{"StormByte.Network.Connection: dynamic {literal}"}, std::string_view{standard.what()});
	RETURN_TEST(0);
}

// -------------------
// Ownership
// -------------------

int test_polymorphic_destruction() {
	auto owner = Safe::MakeUnique<Network::ConnectionError>(std::string_view{"owned"});
	Safe::Unique<Network::Exception> network = std::move(owner);
	Safe::Unique<StormByte::Exception> base = std::move(network);
	ASSERT_NULL(owner.get());
	ASSERT_NULL(network.get());
	ASSERT_NOT_NULL(base.get());
	ASSERT_NOT_NULL(dynamic_cast<Network::ConnectionError*>(base.get()));
	ASSERT_EQUAL(std::string_view{"StormByte.Network.Connection: owned"}, std::string_view{base->what()});
	base.reset();
	ASSERT_NULL(base.get());
	RETURN_TEST(0);
}

// -------------------
// Throw
// -------------------

int test_base_catch() {
	try {
		throw Network::ConnectionError{"caught {}", 7};
	}
	catch (const StormByte::Exception& error) {
		ASSERT_NOT_NULL(dynamic_cast<const Network::ConnectionError*>(&error));
		ASSERT_EQUAL(std::string_view{"StormByte.Network.Connection: caught 7"}, std::string_view{error.what()});
		RETURN_TEST(0);
	}
	ASSERT_FAIL("ConnectionError was not caught through Base");
}

int test_exact_catch() {
	try {
		throw Network::ConnectionError{std::string_view{"caught {literal}"}};
	}
	catch (const Network::ConnectionError& error) {
		ASSERT_EQUAL(std::string_view{"StormByte.Network.Connection: caught {literal}"}, std::string_view{error.what()});
		RETURN_TEST(0);
	}
	ASSERT_FAIL("ConnectionError was not caught by its exact type");
}

int test_network_catch() {
	try {
		throw Network::ConnectionError{std::string_view{"network"}};
	}
	catch (const Network::Exception& error) {
		ASSERT_NOT_NULL(dynamic_cast<const Network::ConnectionError*>(&error));
		ASSERT_EQUAL(std::string_view{"StormByte.Network.Connection: network"}, std::string_view{error.what()});
		RETURN_TEST(0);
	}
	ASSERT_FAIL("ConnectionError was not caught through Network");
}

// -------------------
// Traits
// -------------------

int test_maybe_safe_traits() {
	static_assert(Type::DerivedFrom<Network::ConnectionError, Network::Exception>);
	static_assert(Type::DerivedFrom<Network::ConnectionError, StormByte::Exception>);
	static_assert(Type::MaybeSafe<Network::ConnectionError>);
	static_assert(Type::MaybeSafe<const Network::ConnectionError&>);
	static_assert(Type::SafeComponent<Network::ConnectionError>);
	static_assert(!Type::IsSafe<Network::ConnectionError>::value);
	static_assert(!Type::SafeValue<Network::ConnectionError>);
	static_assert(Type::MaybeSafe<Safe::Shared<Network::ConnectionError>>);
	static_assert(Type::MaybeSafe<Safe::Unique<Network::ConnectionError>>);
	static_assert(Type::MaybeSafe<Safe::Weak<Network::ConnectionError>>);
	static_assert(!std::is_default_constructible_v<Network::ConnectionError>);
	static_assert(std::is_copy_constructible_v<Network::ConnectionError>);
	static_assert(std::is_copy_assignable_v<Network::ConnectionError>);
	static_assert(std::is_nothrow_move_constructible_v<Network::ConnectionError>);
	static_assert(std::is_nothrow_move_assignable_v<Network::ConnectionError>);
	static_assert(std::has_virtual_destructor_v<Network::ConnectionError>);
	static_assert(std::is_nothrow_destructible_v<Network::ConnectionError>);
	static_assert(!std::is_final_v<Network::ConnectionError>);
	static_assert(noexcept(std::declval<const Network::ConnectionError&>().what()));
	RETURN_TEST(0);
}

int main() {
	int result = 0;

	// -------------------
	// Assign
	// -------------------
	result += test_copy_assignment();
	result += test_copy_self_assignment();
	result += test_move_assignment();
	result += test_move_self_assignment();

	// -------------------
	// Construct
	// -------------------
	result += test_copy_constructor();
	result += test_embedded_null();
	result += test_empty_message();
	result += test_format_arguments();
	result += test_inherited_child_paths();
	result += test_message_is_owned();
	result += test_move_constructor();
	result += test_plain_string_view();
	result += test_safe_string();

	// -------------------
	// Ownership
	// -------------------
	result += test_polymorphic_destruction();

	// -------------------
	// Throw
	// -------------------
	result += test_base_catch();
	result += test_exact_catch();
	result += test_network_catch();

	// -------------------
	// Traits
	// -------------------
	result += test_maybe_safe_traits();
	RETURN_TEST(result);
}