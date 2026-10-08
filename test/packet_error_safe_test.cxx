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

#include "exception_safe_helpers.hxx"

namespace {
	constexpr char Prefix[] = "StormByte.Network.Transport.Packet: ";
	using Checks = NetworkExceptionTest::Checks<StormByte::Network::PacketError, Prefix>;
}

// -------------------
// Assign
// -------------------

int test_copy_assignment() {
	RETURN_TEST(Checks::CopyAssignment());
}

int test_copy_self_assignment() {
	RETURN_TEST(Checks::CopySelfAssignment());
}

int test_move_assignment() {
	RETURN_TEST(Checks::MoveAssignment());
}

int test_move_self_assignment() {
	RETURN_TEST(Checks::MoveSelfAssignment());
}

// -------------------
// Construct
// -------------------

int test_copy_constructor() {
	RETURN_TEST(Checks::CopyConstructor());
}

int test_embedded_null() {
	RETURN_TEST(Checks::EmbeddedNull());
}

int test_empty_message() {
	RETURN_TEST(Checks::EmptyMessage());
}

int test_format_arguments() {
	RETURN_TEST(Checks::FormatArguments());
}

int test_inherited_child_paths() {
	RETURN_TEST(Checks::InheritedChildPaths());
}

int test_long_message() {
	RETURN_TEST(Checks::LongMessage());
}

int test_message_is_owned() {
	RETURN_TEST(Checks::MessageIsOwned());
}

int test_move_constructor() {
	RETURN_TEST(Checks::MoveConstructor());
}

int test_plain_string_view() {
	RETURN_TEST(Checks::PlainStringView());
}

int test_safe_string() {
	RETURN_TEST(Checks::SafeString());
}

// -------------------
// Interface
// -------------------

int test_what_interface() {
	RETURN_TEST(Checks::WhatInterface());
}

// -------------------
// Ownership
// -------------------

int test_polymorphic_destruction() {
	RETURN_TEST(Checks::PolymorphicDestruction());
}

// -------------------
// Throw
// -------------------

int test_base_catch() {
	RETURN_TEST(Checks::BaseCatch());
}

int test_exact_catch() {
	RETURN_TEST(Checks::ExactCatch());
}

int test_network_catch() {
	RETURN_TEST(Checks::NetworkCatch());
}

int test_rethrow() {
	RETURN_TEST(Checks::Rethrow());
}

// -------------------
// Traits
// -------------------

int test_maybe_safe_traits() {
	RETURN_TEST(Checks::MaybeSafeTraits());
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
	result += test_long_message();
	result += test_message_is_owned();
	result += test_move_constructor();
	result += test_plain_string_view();
	result += test_safe_string();

	// -------------------
	// Interface
	// -------------------
	result += test_what_interface();

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
	result += test_rethrow();

	// -------------------
	// Traits
	// -------------------
	result += test_maybe_safe_traits();
	RETURN_TEST(result);
}
