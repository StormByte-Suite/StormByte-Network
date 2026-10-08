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
	Network::Exception destination{std::string_view{"old"}};
	{
		Network::Exception source{ExceptionPaths::Path{"Peer"}, std::string_view{"copied"}};
		ASSERT_EQUAL(&destination, &(destination = source));
		source = Network::Exception{std::string_view{"changed"}};
	}
	ASSERT_EQUAL(std::string_view{"StormByte.Network.Peer: copied"}, std::string_view{destination.what()});
	RETURN_TEST(0);
}

int test_copy_self_assignment() {
	Network::Exception error{std::string_view{"self {literal}"}};
	const auto* self = &error;
	ASSERT_EQUAL(&error, &(error = *self));
	ASSERT_EQUAL(std::string_view{"StormByte.Network: self {literal}"}, std::string_view{error.what()});
	RETURN_TEST(0);
}

int test_move_assignment() {
	Network::Exception destination{std::string_view{"old"}};
	{
		Network::Exception source{ExceptionPaths::Path{"Peer"}, "moved {}", 42};
		ASSERT_EQUAL(&destination, &(destination = std::move(source)));
		source = Network::Exception{std::string_view{"reused"}};
		ASSERT_EQUAL(std::string_view{"StormByte.Network: reused"}, std::string_view{source.what()});
	}
	ASSERT_EQUAL(std::string_view{"StormByte.Network.Peer: moved 42"}, std::string_view{destination.what()});
	RETURN_TEST(0);
}

int test_move_self_assignment() {
	Network::Exception error{std::string_view{"self"}};
	auto* self = &error;
	ASSERT_EQUAL(&error, &(error = std::move(*self)));
	error = Network::Exception{std::string_view{"recovered"}};
	ASSERT_EQUAL(std::string_view{"StormByte.Network: recovered"}, std::string_view{error.what()});
	RETURN_TEST(0);
}

// -------------------
// Construct
// -------------------

int test_child_paths() {
	const Network::Exception empty{ExceptionPaths::Path{}, std::string_view{"empty"}};
	const Network::Exception nested{ExceptionPaths::Path{"Transport.Packet"}, std::string_view{"{literal}"}};
	const Network::Exception formatted{ExceptionPaths::Path{"Peer"}, "status {:04x}", 42};
	ASSERT_EQUAL(std::string_view{"StormByte.Network.: empty"}, std::string_view{empty.what()});
	ASSERT_EQUAL(std::string_view{"StormByte.Network.Transport.Packet: {literal}"}, std::string_view{nested.what()});
	ASSERT_EQUAL(std::string_view{"StormByte.Network.Peer: status 002a"}, std::string_view{formatted.what()});
	Safe::String path{"Owned.Child"};
	const Network::Exception owned{ExceptionPaths::Path{std::string_view{path}}, std::string_view{"retained"}};
	path.clear();
	ASSERT_EQUAL(std::string_view{"StormByte.Network.Owned.Child: retained"}, std::string_view{owned.what()});
	RETURN_TEST(0);
}

int test_copy_constructor() {
	Network::Exception copy = [] {
		Network::Exception source{ExceptionPaths::Path{"Peer"}, "copy {}", 7};
		Network::Exception result{source};
		source = Network::Exception{std::string_view{"changed"}};
		return result;
	}();
	ASSERT_EQUAL(std::string_view{"StormByte.Network.Peer: copy 7"}, std::string_view{copy.what()});
	RETURN_TEST(0);
}

int test_embedded_null() {
	const Network::Exception error{std::string_view{"left\0right", 10}};
	constexpr std::string_view expected{"StormByte.Network: left\0right", 29};
	ASSERT_EQUAL(expected, (std::string_view{error.what(), expected.size()}));
	ASSERT_EQUAL('\0', error.what()[expected.size()]);
	RETURN_TEST(0);
}

int test_empty_message() {
	const Network::Exception error{std::string_view{}};
	ASSERT_NOT_NULL(error.what());
	ASSERT_EQUAL(std::string_view{"StormByte.Network: "}, std::string_view{error.what()});
	ASSERT_SIZE(std::string_view{error.what()}, 19u);
	RETURN_TEST(0);
}

int test_format_arguments() {
	const Network::Exception error{"peer {}: code {:04x}, retry {}", std::string_view{"alpha"}, 42, true};
	ASSERT_EQUAL(std::string_view{"StormByte.Network: peer alpha: code 002a, retry true"}, std::string_view{error.what()});
	RETURN_TEST(0);
}

int test_move_constructor() {
	Network::Exception moved = [] {
		Network::Exception source{ExceptionPaths::Path{"Peer"}, std::string_view{"moved"}};
		return Network::Exception{std::move(source)};
	}();
	ASSERT_EQUAL(std::string_view{"StormByte.Network.Peer: moved"}, std::string_view{moved.what()});
	RETURN_TEST(0);
}

int test_plain_string_view() {
	const Network::Exception error{std::string_view{"unmatched { and } plus {}"}};
	ASSERT_EQUAL(std::string_view{"StormByte.Network: unmatched { and } plus {}"}, std::string_view{error.what()});
	RETURN_TEST(0);
}

int test_safe_string() {
	Safe::String message{"owned {literal}"};
	const Network::Exception error{std::string_view{message}};
	message.clear();
	ASSERT_EQUAL(std::string_view{"StormByte.Network: owned {literal}"}, std::string_view{error.what()});
	Safe::String empty;
	const Network::Exception empty_error{std::string_view{empty}};
	ASSERT_EQUAL(std::string_view{"StormByte.Network: "}, std::string_view{empty_error.what()});
	RETURN_TEST(0);
}

// -------------------
// Ownership
// -------------------

int test_polymorphic_destruction() {
	auto owner = Safe::MakeUnique<Network::Exception>(std::string_view{"owned"});
	Safe::Unique<StormByte::Exception> base = std::move(owner);
	ASSERT_NULL(owner.get());
	ASSERT_NOT_NULL(base.get());
	ASSERT_NOT_NULL(dynamic_cast<Network::Exception*>(base.get()));
	ASSERT_EQUAL(std::string_view{"StormByte.Network: owned"}, std::string_view{base->what()});
	base.reset();
	ASSERT_NULL(base.get());
	RETURN_TEST(0);
}

// -------------------
// Throw
// -------------------

int test_base_catch() {
	try {
		throw Network::Exception{"caught {}", 7};
	}
	catch (const StormByte::Exception& error) {
		ASSERT_NOT_NULL(dynamic_cast<const Network::Exception*>(&error));
		ASSERT_EQUAL(std::string_view{"StormByte.Network: caught 7"}, std::string_view{error.what()});
		RETURN_TEST(0);
	}
	ASSERT_FAIL("Network exception was not caught through Base");
}

int test_exact_catch() {
	try {
		throw Network::Exception{std::string_view{"caught {literal}"}};
	}
	catch (const Network::Exception& error) {
		ASSERT_EQUAL(std::string_view{"StormByte.Network: caught {literal}"}, std::string_view{error.what()});
		RETURN_TEST(0);
	}
	ASSERT_FAIL("Network exception was not caught by its exact type");
}

// -------------------
// Traits
// -------------------

int test_maybe_safe_traits() {
	static_assert(Type::DerivedFrom<Network::Exception, StormByte::Exception>);
	static_assert(Type::MaybeSafe<Network::Exception>);
	static_assert(Type::MaybeSafe<const Network::Exception&>);
	static_assert(Type::SafeComponent<Network::Exception>);
	static_assert(!Type::IsSafe<Network::Exception>::value);
	static_assert(!Type::SafeValue<Network::Exception>);
	static_assert(Type::MaybeSafe<Safe::Shared<Network::Exception>>);
	static_assert(Type::MaybeSafe<Safe::Unique<Network::Exception>>);
	static_assert(Type::MaybeSafe<Safe::Weak<Network::Exception>>);
	static_assert(!std::is_default_constructible_v<Network::Exception>);
	static_assert(std::is_copy_constructible_v<Network::Exception>);
	static_assert(std::is_copy_assignable_v<Network::Exception>);
	static_assert(std::is_nothrow_move_constructible_v<Network::Exception>);
	static_assert(std::is_nothrow_move_assignable_v<Network::Exception>);
	static_assert(std::has_virtual_destructor_v<Network::Exception>);
	static_assert(std::is_nothrow_destructible_v<Network::Exception>);
	static_assert(noexcept(std::declval<const Network::Exception&>().what()));
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
	result += test_child_paths();
	result += test_copy_constructor();
	result += test_embedded_null();
	result += test_empty_message();
	result += test_format_arguments();
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

	// -------------------
	// Traits
	// -------------------
	result += test_maybe_safe_traits();
	RETURN_TEST(result);
}