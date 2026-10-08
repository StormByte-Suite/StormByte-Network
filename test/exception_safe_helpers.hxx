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

#pragma once

#include <StormByte/network/exception.hxx>
#include <StormByte/safe/pointers.hxx>
#include <StormByte/test_handlers.h>
#include <StormByte/type_traits.hxx>

#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

/**
 * @namespace NetworkExceptionTest
 * @brief Shared public exception regression helpers for Network tests.
 */
namespace NetworkExceptionTest {
	/**
	 * @brief Expose the borrowed path tag; no instances can be constructed or copied.
	 */
	struct PathAccess final: StormByte::Network::Exception {
		/**
		 * @brief Borrowed child path accepted by inherited exception constructors.
		 */
		using Path = StormByte::Exception::Path;

		/**
		 * @brief Prevent construction of the tag-access helper.
		 */
		PathAccess() = delete;

		/**
		 * @brief Prevent copying the tag-access helper.
		 * @param other Unused source.
		 */
		PathAccess(const PathAccess& other) = delete;

		/**
		 * @brief Prevent moving the tag-access helper.
		 * @param other Unused source.
		 */
		PathAccess(PathAccess&& other) = delete;

		/**
		 * @brief Preserve virtual destruction without introducing owned state.
		 */
		~PathAccess() noexcept override = default;

		/**
		 * @brief Prevent copy assignment.
		 * @param other Unused source.
		 * @return No value; this operation is deleted.
		 */
		PathAccess& operator=(const PathAccess& other) = delete;

		/**
		 * @brief Prevent move assignment.
		 * @param other Unused source.
		 * @return No value; this operation is deleted.
		 */
		PathAccess& operator=(PathAccess&& other) = delete;
	};

	/**
	 * @brief Common lifecycle and interface checks instantiated by each leaf suite.
	 * @tparam Error Exact public exception type.
	 * @tparam Prefix Static null-terminated prefix for leaf message constructors.
	 */
	template<typename Error, auto& Prefix>
	class Checks {
		public:
			/**
			 * @brief Check copy assignment, its return reference and independent storage.
			 * @return Zero on success, otherwise one.
			 */
			static int CopyAssignment() {
				Error destination{std::string_view{"old"}};
				{
					Error source{std::string_view{"copied"}};
					ASSERT_EQUAL(&destination, &(destination = source));
					source = Error{std::string_view{"changed"}};
				}
				RETURN_TEST(Message(destination, "copied"));
			}

			/**
			 * @brief Check that self-copy retains the message.
			 * @return Zero on success, otherwise one.
			 */
			static int CopySelfAssignment() {
				Error error{std::string_view{"self {literal}"}};
				const auto* self = &error;
				ASSERT_EQUAL(&error, &(error = *self));
				RETURN_TEST(Message(error, "self {literal}"));
			}

			/**
			 * @brief Check move assignment, source reuse and destination lifetime.
			 * @return Zero on success, otherwise one.
			 */
			static int MoveAssignment() {
				Error destination{std::string_view{"old"}};
				{
					Error source{"moved {}", 42};
					ASSERT_EQUAL(&destination, &(destination = std::move(source)));
					ASSERT_NOT_NULL(source.what());
					ASSERT_EQUAL(0, Message(destination, "moved 42"));
					source = Error{std::string_view{"reused"}};
					ASSERT_EQUAL(0, Message(source, "reused"));
				}
				RETURN_TEST(Message(destination, "moved 42"));
			}

			/**
			 * @brief Check self-move validity without assuming a retained message.
			 * @return Zero on success, otherwise one.
			 */
			static int MoveSelfAssignment() {
				Error error{std::string_view{"self"}};
				auto* self = &error;
				ASSERT_EQUAL(&error, &(error = std::move(*self)));
				ASSERT_NOT_NULL(error.what());
				error = Error{std::string_view{"recovered"}};
				RETURN_TEST(Message(error, "recovered"));
			}

			/**
			 * @brief Check copied text after mutation and destruction of its source.
			 * @return Zero on success, otherwise one.
			 */
			static int CopyConstructor() {
				Error copy = [] {
					Error source{"copy {}", 7};
					Error result{source};
					source = Error{std::string_view{"changed"}};
					return result;
				}();
				RETURN_TEST(Message(copy, "copy 7"));
			}

			/**
			 * @brief Check embedded null bytes in plain and formatted text.
			 * @return Zero on success, otherwise one.
			 */
			static int EmbeddedNull() {
				constexpr std::string_view message{"left\0right", 10};
				const Error plain{message};
				const Error formatted{"{}", message};
				ASSERT_EQUAL(0, Message(plain, message));
				RETURN_TEST(Message(formatted, message));
			}

			/**
			 * @brief Check empty plain and formatted messages retain the leaf prefix.
			 * @return Zero on success, otherwise one.
			 */
			static int EmptyMessage() {
				const Error plain{std::string_view{}};
				const Error formatted{""};
				ASSERT_EQUAL(0, Message(plain, ""));
				RETURN_TEST(Message(formatted, ""));
			}

			/**
			 * @brief Check forwarding, format specifications and escaped braces.
			 * @return Zero on success, otherwise one.
			 */
			static int FormatArguments() {
				std::string_view peer{"alpha"};
				const Error error{"peer {}: code {:04x}, retry {}, {{literal}}", peer, 42, true};
				RETURN_TEST(Message(error, "peer alpha: code 002a, retry true, {literal}"));
			}

			/**
			 * @brief Check inherited plain/formatted paths and ownership of path bytes.
			 * @return Zero on success, otherwise one.
			 */
			static int InheritedChildPaths() {
				StormByte::Safe::String path{"Custom.Child"};
				const Error plain{PathAccess::Path{std::string_view{path}}, std::string_view{"{literal}"}};
				const Error formatted{PathAccess::Path{std::string_view{path}}, "code {}", 7};
				const Error empty{PathAccess::Path{}, std::string_view{"empty"}};
				path.clear();
				ASSERT_EQUAL(std::string_view{"StormByte.Network.Custom.Child: {literal}"}, std::string_view{plain.what()});
				ASSERT_EQUAL(std::string_view{"StormByte.Network.Custom.Child: code 7"}, std::string_view{formatted.what()});
				ASSERT_EQUAL(std::string_view{"StormByte.Network.: empty"}, std::string_view{empty.what()});
				RETURN_TEST(0);
			}

			/**
			 * @brief Check large messages survive caller buffer destruction and copying.
			 * @return Zero on success, otherwise one.
			 */
			static int LongMessage() {
				const std::string expected(8192, 'x');
				Error error = [] {
					const std::string source(8192, 'x');
					return Error{std::string_view{source}};
				}();
				const Error copy{error};
				ASSERT_EQUAL(0, Message(error, expected));
				RETURN_TEST(Message(copy, expected));
			}

			/**
			 * @brief Check borrowed message bytes are copied rather than retained.
			 * @return Zero on success, otherwise one.
			 */
			static int MessageIsOwned() {
				char message[] = "borrowed {literal}";
				const Error error{std::string_view{message}};
				message[0] = 'X';
				RETURN_TEST(Message(error, "borrowed {literal}"));
			}

			/**
			 * @brief Check moved text and reassignment of the still-live source.
			 * @return Zero on success, otherwise one.
			 */
			static int MoveConstructor() {
				Error moved = [] {
					Error source{std::string_view{"moved"}};
					return Error{std::move(source)};
				}();
				Error source{std::string_view{"second"}};
				const Error second{std::move(source)};
				ASSERT_NOT_NULL(source.what());
				source = Error{std::string_view{"reused"}};
				ASSERT_EQUAL(0, Message(source, "reused"));
				ASSERT_EQUAL(0, Message(second, "second"));
				RETURN_TEST(Message(moved, "moved"));
			}

			/**
			 * @brief Check plain text is never interpreted as a format string.
			 * @return Zero on success, otherwise one.
			 */
			static int PlainStringView() {
				const Error error{std::string_view{"unmatched { and } plus {}"}};
				RETURN_TEST(Message(error, "unmatched { and } plus {}"));
			}

			/**
			 * @brief Check explicit views of Safe strings preserve the leaf path and owned text.
			 * @return Zero on success, otherwise one.
			 */
			static int SafeString() {
				StormByte::Safe::String message{"owned {literal}"};
				const Error owned{std::string_view{message}};
				message.clear();
				const Error temporary{std::string_view{StormByte::Safe::String{"temporary"}}};
				const Error empty{std::string_view{message}};
				const Error literal{"plain {literal}"};
				std::string dynamic{"dynamic {literal}"};
				const Error text{dynamic.c_str()};
				const Error standard{dynamic};
				dynamic.clear();
				ASSERT_EQUAL(0, Message(owned, "owned {literal}"));
				ASSERT_EQUAL(0, Message(temporary, "temporary"));
				ASSERT_EQUAL(0, Message(empty, ""));
				ASSERT_EQUAL(0, Message(literal, "plain {literal}"));
				ASSERT_EQUAL(0, Message(text, "dynamic {literal}"));
				ASSERT_EQUAL(0, Message(standard, "dynamic {literal}"));
				RETURN_TEST(0);
			}

			/**
			 * @brief Check repeated const what calls through each public base interface.
			 * @return Zero on success, otherwise one.
			 */
			static int WhatInterface() {
				const Error error{std::string_view{"stable"}};
				const StormByte::Network::Exception& network = error;
				const StormByte::Exception& base = error;
				ASSERT_NOT_NULL(error.what());
				ASSERT_EQUAL(error.what(), error.what());
				ASSERT_EQUAL(error.what(), network.what());
				ASSERT_EQUAL(error.what(), base.what());
				RETURN_TEST(Message(error, "stable"));
			}

			/**
			 * @brief Check exact Safe allocation and deletion through Network/Base owners.
			 * @return Zero on success, otherwise one.
			 */
			static int PolymorphicDestruction() {
				auto owner = StormByte::Safe::MakeUnique<Error>(std::string_view{"owned"});
				StormByte::Safe::Unique<StormByte::Network::Exception> network = std::move(owner);
				StormByte::Safe::Unique<StormByte::Exception> base = std::move(network);
				ASSERT_NULL(owner.get());
				ASSERT_NULL(network.get());
				ASSERT_NOT_NULL(base.get());
				const auto* exact = dynamic_cast<const Error*>(base.get());
				ASSERT_NOT_NULL(exact);
				ASSERT_EQUAL(0, Message(*exact, "owned"));
				base.reset();
				ASSERT_NULL(base.get());
				RETURN_TEST(0);
			}

			/**
			 * @brief Check formatted exceptions caught through the StormByte base.
			 * @return Zero on success, otherwise one.
			 */
			static int BaseCatch() {
				try {
					throw Error{"caught {}", 7};
				}
				catch (const StormByte::Exception& error) {
					const auto* exact = dynamic_cast<const Error*>(&error);
					ASSERT_NOT_NULL(exact);
					RETURN_TEST(Message(*exact, "caught 7"));
				}
				ASSERT_FAIL("Exception was not caught through Base");
			}

			/**
			 * @brief Check throwing a named copy leaves the original usable.
			 * @return Zero on success, otherwise one.
			 */
			static int ExactCatch() {
				const Error original{std::string_view{"caught {literal}"}};
				try {
					throw original;
				}
				catch (const Error& error) {
					ASSERT_EQUAL(0, Message(original, "caught {literal}"));
					RETURN_TEST(Message(error, "caught {literal}"));
				}
				ASSERT_FAIL("Exception was not caught by its exact type");
			}

			/**
			 * @brief Check catching an exact leaf through the Network exception base.
			 * @return Zero on success, otherwise one.
			 */
			static int NetworkCatch() {
				try {
					throw Error{std::string_view{"network"}};
				}
				catch (const StormByte::Network::Exception& error) {
					const auto* exact = dynamic_cast<const Error*>(&error);
					ASSERT_NOT_NULL(exact);
					RETURN_TEST(Message(*exact, "network"));
				}
				ASSERT_FAIL("Exception was not caught through Network");
			}

			/**
			 * @brief Check rethrow preserves dynamic type and message across base catches.
			 * @return Zero on success, otherwise one.
			 */
			static int Rethrow() {
				try {
					try {
						throw Error{std::string_view{"rethrown"}};
					}
					catch (const StormByte::Network::Exception&) {
						throw;
					}
				}
				catch (const Error& error) {
					RETURN_TEST(Message(error, "rethrown"));
				}
				ASSERT_FAIL("Rethrow lost the exact exception type");
			}

			/**
			 * @brief Check declared Safe category and public lifecycle/type contracts.
			 * @return Zero on success; invalid contracts fail compilation.
			 */
			static int MaybeSafeTraits() {
				static_assert(StormByte::Type::DerivedFrom<Error, StormByte::Network::Exception>);
				static_assert(StormByte::Type::DerivedFrom<Error, StormByte::Exception>);
				static_assert(StormByte::Type::MaybeSafe<Error>);
				static_assert(StormByte::Type::MaybeSafe<const Error&>);
				static_assert(StormByte::Type::SafeComponent<Error>);
				static_assert(!StormByte::Type::IsSafe<Error>::value);
				static_assert(!StormByte::Type::SafeValue<Error>);
				static_assert(StormByte::Type::MaybeSafe<StormByte::Safe::Shared<Error>>);
				static_assert(StormByte::Type::MaybeSafe<StormByte::Safe::Unique<Error>>);
				static_assert(StormByte::Type::MaybeSafe<StormByte::Safe::Weak<Error>>);
				static_assert(!std::is_default_constructible_v<Error>);
				static_assert(std::is_copy_constructible_v<Error>);
				static_assert(std::is_copy_assignable_v<Error>);
				static_assert(std::is_nothrow_move_constructible_v<Error>);
				static_assert(std::is_nothrow_move_assignable_v<Error>);
				static_assert(std::has_virtual_destructor_v<Error>);
				static_assert(std::is_nothrow_destructible_v<Error>);
				static_assert(std::is_final_v<Error>);
				static_assert(StormByte::Type::SameAs<decltype(std::declval<const Error&>().what()), const char*>);
				static_assert(noexcept(std::declval<const Error&>().what()));
				RETURN_TEST(0);
			}

		private:
			/**
			 * @brief Compare all message bytes, including nulls, and the terminator.
			 * @param error Exact exception under inspection.
			 * @param message Expected message without the leaf prefix.
			 * @return Zero on success, otherwise one.
			 */
			static int Message(const Error& error, std::string_view message) {
				StormByte::Safe::String expected{std::string_view{Prefix}};
				expected += message;
				ASSERT_NOT_NULL(error.what());
				ASSERT_EQUAL(std::string_view{expected}, (std::string_view{error.what(), std::string_view{expected}.size()}));
				ASSERT_EQUAL('\0', error.what()[std::string_view{expected}.size()]);
				RETURN_TEST(0);
			}
	};
}

STORMBYTE_DECLARE_MAYBE_SAFE(NetworkExceptionTest::PathAccess);
