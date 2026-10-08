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

#include <StormByte/network/endpoint.hxx>
#include <StormByte/test_handlers.h>
#include <StormByte/type_traits.hxx>

#include <type_traits>
#include <utility>

using namespace StormByte;

// -------------------
// Classification
// -------------------

int test_maybe_safe_traits() {
	static_assert(Type::MaybeSafe<Network::Endpoint>);
	static_assert(Type::MaybeSafe<const Network::Endpoint&>);
	static_assert(Type::MaybeSafe<Safe::Shared<Network::Endpoint>>);
	static_assert(!Type::SafeValue<Network::Endpoint>);
	static_assert(std::is_abstract_v<Network::Endpoint>);
	static_assert(std::has_virtual_destructor_v<Network::Endpoint>);
	RETURN_TEST(0);
}

// -------------------
// Interface
// -------------------

int test_public_signatures() {
	using Endpoint = Network::Endpoint;
	using Protocol = Network::Connection::Protocol;
	static_assert(Type::SameAs<decltype(&Endpoint::Connect),
		bool (Endpoint::*)(const Protocol&, std::string_view, const unsigned short&)>);
	static_assert(Type::SameAs<decltype(&Endpoint::Disconnect), void (Endpoint::*)() noexcept>);
	static_assert(Type::SameAs<decltype(&Endpoint::Status), Network::Connection::Status (Endpoint::*)() const noexcept>);
	static_assert(noexcept(std::declval<Endpoint&>().Disconnect()));
	static_assert(noexcept(std::declval<const Endpoint&>().Status()));
	RETURN_TEST(0);
}

// -------------------
// Lifecycle
// -------------------

int test_deleted_copy_and_move_assignment() {
	using Endpoint = Network::Endpoint;
	static_assert(!std::is_default_constructible_v<Endpoint>);
	static_assert(!std::is_copy_constructible_v<Endpoint>);
	static_assert(!std::is_copy_assignable_v<Endpoint>);
	static_assert(std::is_nothrow_move_assignable_v<Endpoint>);
	static_assert(std::is_nothrow_destructible_v<Endpoint>);
	static_assert(Type::SameAs<decltype(std::declval<Endpoint&>() = std::declval<Endpoint&&>()), Endpoint&>);
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
	result += test_public_signatures();

	// -------------------
	// Lifecycle
	// -------------------
	result += test_deleted_copy_and_move_assignment();

	RETURN_TEST(result);
}