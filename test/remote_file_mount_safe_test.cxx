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

#include <StormByte/network/remote_file_mount.hxx>
#include <StormByte/safe/binary.hxx>
#include <StormByte/serializable.hxx>
#include <StormByte/test_handlers.h>
#include <StormByte/type_traits.hxx>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <span>
#include <utility>

using namespace StormByte;
using namespace StormByte::Network;

namespace {
	/**
	 * @brief Construct a mount payload without invoking the descriptor codec.
	 * @param status Raw mount result.
	 * @param access Raw channel direction.
	 * @param port Data-plane port.
	 * @param timeout Maximum idle interval in seconds.
	 * @param token Opaque channel capability.
	 * @return Version-one wire payload owned by Base.
	 */
	Safe::Binary Payload(std::uint8_t status, std::uint8_t access, std::uint16_t port,
		std::uint16_t timeout, const RemoteFileMount::ChannelToken& token) {
		Safe::Binary payload;
		payload.append(Serializable<std::uint32_t>(0x5342464Du).Serialize());
		payload.append(Serializable<std::uint16_t>(1).Serialize());
		payload.append(Serializable<std::uint16_t>(port).Serialize());
		payload.append(Serializable<std::uint16_t>(timeout).Serialize());
		payload.append(Serializable<std::uint8_t>(status).Serialize());
		payload.append(Serializable<std::uint8_t>(access).Serialize());
		payload.append(std::span<const std::byte>{token});
		return payload;
	}

	/**
	 * @brief Make a capability with distinct bytes at every position.
	 * @return Nonempty fixed-width capability.
	 */
	RemoteFileMount::ChannelToken Token() {
		RemoteFileMount::ChannelToken token{};
		for (std::size_t index = 0; index < token.size(); ++index)
			token[index] = static_cast<std::byte>(index + 1);
		return token;
	}
}

// -------------------
// Codec
// -------------------

/**
 * @brief Verify both authorized directions and accepted scalar boundaries.
 * @return Zero on success.
 */
int test_codec_authorized_round_trip() {
	const auto token = Token();
	for (const std::uint8_t access: {std::uint8_t{1}, std::uint8_t{2}}) {
		for (const std::uint16_t port: {std::uint16_t{1}, std::uint16_t{65535}}) {
			for (const std::uint16_t timeout: {std::uint16_t{3}, std::uint16_t{3600}}) {
				const auto payload = Payload(1, access, port, timeout, token);
				ASSERT_SIZE(payload, ByteSize{44});
				const auto mount = Serializable<RemoteFileMount>::Deserialize(payload);
				ASSERT_TRUE(mount);
				ASSERT_EQUAL(RemoteFileMount::Status::Authorized, mount->Result());
				ASSERT_EQUAL(static_cast<RemoteFileMount::Access>(access), mount->Mode());
				ASSERT_EQUAL(port, mount->Port());
				ASSERT_EQUAL(timeout, mount->MaximumTimeoutSeconds());
				ASSERT_EQUAL(token, mount->Token());
				ASSERT_EQUAL(ByteSize{44}, Serializable<RemoteFileMount>::Size(*mount));
				ASSERT_EQUAL(payload, Serializable<RemoteFileMount>(*mount).Serialize());
				const auto direct = Serializable<RemoteFileMount>::Deserialize(payload.span());
				ASSERT_TRUE(direct);
				ASSERT_EQUAL(token, direct->Token());
			}
		}
	}
	RETURN_TEST(0);
}

/**
 * @brief Reject every invalid authorized scalar and an empty capability.
 * @return Zero on success.
 */
int test_codec_invalid_authorized() {
	const auto token = Token();
	ASSERT_FALSE(Serializable<RemoteFileMount>::Deserialize(Payload(1, 1, 0, 3, token)));
	for (const std::uint16_t timeout: {std::uint16_t{0}, std::uint16_t{1}, std::uint16_t{2},
		std::uint16_t{3601}, std::uint16_t{65535}})
		ASSERT_FALSE(Serializable<RemoteFileMount>::Deserialize(Payload(1, 1, 1, timeout, token)));
	for (unsigned int access = 0; access <= 255; ++access) {
		if (access == 1 || access == 2)
			continue;
		ASSERT_FALSE(Serializable<RemoteFileMount>::Deserialize(
			Payload(1, static_cast<std::uint8_t>(access), 1, 3, token)));
	}
	ASSERT_FALSE(Serializable<RemoteFileMount>::Deserialize(Payload(1, 1, 1, 3, {})));
	RETURN_TEST(0);
}

/**
 * @brief Reject wrong magic, unsupported versions, truncation and trailing bytes.
 * @return Zero on success.
 */
int test_codec_invalid_envelope() {
	const auto payload = Payload(1, 1, 1, 3, Token());
	for (std::size_t length = 0; length < 44; ++length)
		ASSERT_FALSE(Serializable<RemoteFileMount>::Deserialize(payload.span().first(length)));
	auto trailing = payload;
	trailing.push_back(std::byte{0});
	ASSERT_FALSE(Serializable<RemoteFileMount>::Deserialize(trailing));
	for (std::size_t offset = 0; offset < 6; ++offset) {
		auto corrupt = payload;
		corrupt[ByteSize{offset}] ^= std::byte{0xFF};
		ASSERT_FALSE(Serializable<RemoteFileMount>::Deserialize(corrupt));
	}
	RETURN_TEST(0);
}

/**
 * @brief Reject unknown results and credentials attached to rejected mounts.
 * @return Zero on success.
 */
int test_codec_invalid_rejected() {
	for (unsigned int status = 0; status <= 255; ++status) {
		if (status >= 1 && status <= 6)
			continue;
		ASSERT_FALSE(Serializable<RemoteFileMount>::Deserialize(
			Payload(static_cast<std::uint8_t>(status), 0, 0, 0, {})));
	}
	for (std::uint8_t status = 2; status <= 6; ++status) {
		ASSERT_FALSE(Serializable<RemoteFileMount>::Deserialize(Payload(status, 0, 1, 0, {})));
		ASSERT_FALSE(Serializable<RemoteFileMount>::Deserialize(Payload(status, 0, 0, 1, {})));
		for (unsigned int access = 1; access <= 255; ++access)
			ASSERT_FALSE(Serializable<RemoteFileMount>::Deserialize(
				Payload(status, static_cast<std::uint8_t>(access), 0, 0, {})));
		for (std::size_t index = 0; index < 32; ++index) {
			RemoteFileMount::ChannelToken token{};
			token[index] = std::byte{1};
			ASSERT_FALSE(Serializable<RemoteFileMount>::Deserialize(Payload(status, 0, 0, 0, token)));
		}
	}
	RETURN_TEST(0);
}

/**
 * @brief Verify every rejection factory and the exact little-endian wire layout.
 * @return Zero on success.
 */
int test_codec_rejected_round_trip() {
	for (const auto& mount: {RemoteFileMount::NotAuthorized(), RemoteFileMount::Unavailable(),
		RemoteFileMount::FileBeingRead(), RemoteFileMount::FileBeingWritten(), RemoteFileMount::Failed()}) {
		const auto status = static_cast<std::uint8_t>(mount.Result());
		const auto payload = Serializable<RemoteFileMount>(mount).Serialize();
		ASSERT_SIZE(payload, ByteSize{44});
		ASSERT_EQUAL(ByteSize{44}, Serializable<RemoteFileMount>::Size(mount));
		ASSERT_EQUAL(Payload(status, 0, 0, 0, {}), payload);
		ASSERT_EQUAL(RemoteFileMount::Access::None, mount.Mode());
		ASSERT_EQUAL(0, mount.Port());
		ASSERT_EQUAL(0, mount.MaximumTimeoutSeconds());
		ASSERT_TRUE(std::ranges::all_of(mount.Token(), [](std::byte value) { return value == std::byte{0}; }));
		ASSERT_EQUAL(std::byte{0x4D}, payload[ByteSize{0}]);
		ASSERT_EQUAL(std::byte{0x46}, payload[ByteSize{1}]);
		ASSERT_EQUAL(std::byte{0x42}, payload[ByteSize{2}]);
		ASSERT_EQUAL(std::byte{0x53}, payload[ByteSize{3}]);
		ASSERT_EQUAL(std::byte{1}, payload[ByteSize{4}]);
		ASSERT_EQUAL(std::byte{0}, payload[ByteSize{5}]);
		const auto decoded = Serializable<RemoteFileMount>::Deserialize(payload);
		ASSERT_TRUE(decoded);
		ASSERT_EQUAL(mount.Result(), decoded->Result());
		ASSERT_EQUAL(mount.Mode(), decoded->Mode());
		ASSERT_EQUAL(mount.Token(), decoded->Token());
		ASSERT_EQUAL(mount.Port(), decoded->Port());
		ASSERT_EQUAL(mount.MaximumTimeoutSeconds(), decoded->MaximumTimeoutSeconds());
	}
	ASSERT_EQUAL(RemoteFileMount::Status::NotAuthorized, RemoteFileMount::NotAuthorized().Result());
	ASSERT_EQUAL(RemoteFileMount::Status::Unavailable, RemoteFileMount::Unavailable().Result());
	ASSERT_EQUAL(RemoteFileMount::Status::FileBeingRead, RemoteFileMount::FileBeingRead().Result());
	ASSERT_EQUAL(RemoteFileMount::Status::FileBeingWritten, RemoteFileMount::FileBeingWritten().Result());
	ASSERT_EQUAL(RemoteFileMount::Status::Failed, RemoteFileMount::Failed().Result());
	RETURN_TEST(0);
}

/**
 * @brief Verify a capability with any single byte set is not treated as empty.
 * @return Zero on success.
 */
int test_codec_sparse_token() {
	for (std::size_t index = 0; index < 32; ++index) {
		RemoteFileMount::ChannelToken token{};
		token[index] = std::byte{0xFF};
		const auto payload = Payload(1, 2, 1, 3, token);
		const auto mount = Serializable<RemoteFileMount>::Deserialize(payload);
		ASSERT_TRUE(mount);
		ASSERT_EQUAL(token, mount->Token());
		ASSERT_EQUAL(payload, Serializable<RemoteFileMount>(*mount).Serialize());
	}
	RETURN_TEST(0);
}

// -------------------
// Construct
// -------------------

/**
 * @brief Verify every copy/move operation preserves authorized and rejected values.
 * @return Zero on success.
 */
int test_construct_copy_move() {
	const auto authorized = Serializable<RemoteFileMount>::Deserialize(Payload(1, 2, 65535, 3600, Token()));
	ASSERT_TRUE(authorized);
	for (const auto& original: {*authorized, RemoteFileMount::NotAuthorized(), RemoteFileMount::Unavailable(),
		RemoteFileMount::FileBeingRead(), RemoteFileMount::FileBeingWritten(), RemoteFileMount::Failed()}) {
		const auto payload = Serializable<RemoteFileMount>(original).Serialize();
		RemoteFileMount copied(original);
		ASSERT_EQUAL(payload, Serializable<RemoteFileMount>(copied).Serialize());
		RemoteFileMount moved(std::move(copied));
		ASSERT_EQUAL(payload, Serializable<RemoteFileMount>(moved).Serialize());
		ASSERT_EQUAL(payload, Serializable<RemoteFileMount>(copied).Serialize());
		auto assigned = RemoteFileMount::Failed();
		ASSERT_EQUAL(&assigned, &(assigned = original));
		ASSERT_EQUAL(payload, Serializable<RemoteFileMount>(assigned).Serialize());
		auto move_assigned = RemoteFileMount::Failed();
		ASSERT_EQUAL(&move_assigned, &(move_assigned = std::move(assigned)));
		ASSERT_EQUAL(payload, Serializable<RemoteFileMount>(move_assigned).Serialize());
		ASSERT_EQUAL(payload, Serializable<RemoteFileMount>(assigned).Serialize());
		auto* self = &move_assigned;
		ASSERT_EQUAL(self, &(move_assigned = *self));
		ASSERT_EQUAL(self, &(move_assigned = std::move(*self)));
		ASSERT_EQUAL(payload, Serializable<RemoteFileMount>(move_assigned).Serialize());
	}
	RETURN_TEST(0);
}

/**
 * @brief Verify mount accessors expose the documented scalar and borrowed types.
 * @return Zero on success.
 */
int test_construct_getter_types() {
	const auto mount = RemoteFileMount::Failed();
	static_assert(Type::SameAs<decltype(mount.Port()), std::uint16_t>);
	static_assert(Type::SameAs<decltype(mount.MaximumTimeoutSeconds()), std::uint16_t>);
	static_assert(Type::SameAs<decltype(mount.Result()), RemoteFileMount::Status>);
	static_assert(Type::SameAs<decltype(mount.Mode()), RemoteFileMount::Access>);
	static_assert(Type::SameAs<decltype(mount.Token()), const RemoteFileMount::ChannelToken&>);
	static_assert(noexcept(mount.Port()));
	static_assert(noexcept(mount.MaximumTimeoutSeconds()));
	static_assert(noexcept(mount.Result()));
	static_assert(noexcept(mount.Mode()));
	static_assert(noexcept(mount.Token()));
	ASSERT_SIZE(mount.Token(), Size{32});
	RETURN_TEST(0);
}

// -------------------
// Safe
// -------------------

/**
 * @brief Verify provider classification and the actual binary codec interface.
 * @return Zero on success.
 */
int test_safe_interface() {
	static_assert(Type::MaybeSafe<RemoteFileMount>);
	static_assert(Type::SafeComponent<const RemoteFileMount&>);
	static_assert(Type::SafeComponent<Expected<RemoteFileMount, DeserializeError>>);
	static_assert(Type::SameAs<decltype(Serializable<RemoteFileMount>(RemoteFileMount::Failed()).Serialize()), Safe::Binary>);
	static_assert(noexcept(RemoteFileMount::Failed()));
	static_assert(noexcept(RemoteFileMount(std::declval<RemoteFileMount&&>())));
	static_assert(noexcept(std::declval<RemoteFileMount&>() = std::declval<RemoteFileMount&&>()));
	static_assert(noexcept(std::declval<RemoteFileMount&>().~RemoteFileMount()));
	RETURN_TEST(0);
}

/**
 * @brief Run the independent remote-file mount tests.
 * @return Number of failed tests.
 */
int main() {
	int result = 0;

	// -------------------
	// Codec
	// -------------------
	result += test_codec_authorized_round_trip();
	result += test_codec_invalid_authorized();
	result += test_codec_invalid_envelope();
	result += test_codec_invalid_rejected();
	result += test_codec_rejected_round_trip();
	result += test_codec_sparse_token();

	// -------------------
	// Construct
	// -------------------
	result += test_construct_copy_move();
	result += test_construct_getter_types();

	// -------------------
	// Safe
	// -------------------
	result += test_safe_interface();

	RETURN_TEST(result);
}
