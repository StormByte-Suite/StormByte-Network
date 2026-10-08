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

#include <StormByte/network/typedefs.hxx>
#include <StormByte/serializable.hxx>
#include <StormByte/test_handlers.h>

#include <array>
#include <limits>
#include <string_view>
#include <utility>

using namespace StormByte;
using namespace StormByte::Network;
using Transport::Packet;

/**
 * @brief Public packet leaf with Base-owned payload and an optional borrowed destruction counter.
 */
class PacketFixture final: public Packet {
	public:
		/**
		 * @brief Construct a payload-bearing packet.
		 * @param opcode Wire opcode.
		 * @param payload Owned payload bytes.
		 * @param destructions Counter that outlives the packet, or null.
		 */
		PacketFixture(OpcodeType opcode, Safe::Binary payload = {}, unsigned int* destructions = nullptr):
			Packet(opcode), m_payload(std::move(payload)), m_destructions(destructions) {}

		PacketFixture(const PacketFixture& other) = default;

		PacketFixture(PacketFixture&& other) noexcept = default;

		PacketFixture& operator=(const PacketFixture& other) = default;

		PacketFixture& operator=(PacketFixture&& other) noexcept = default;

		~PacketFixture() noexcept override {
			if (m_destructions)
				++*m_destructions;
		}

	protected:
		Safe::Binary DoSerialize() const noexcept override {
			return m_payload;
		}

	private:
		Safe::Binary m_payload;
		unsigned int* m_destructions;
};

STORMBYTE_DECLARE_MAYBE_SAFE(PacketFixture);

static_assert(Type::MaybeSafe<Packet>);
static_assert(Type::MaybeSafe<PacketFixture>);
static_assert(Type::MaybeSafe<PacketPointer>);
static_assert(Type::DerivedFrom<PacketFixture, Packet>);

int CheckWire(const Packet& packet, Packet::OpcodeType opcode, const Safe::Binary& payload) {
	Buffer::FIFO wire = packet.Serialize();
	const Safe::Binary encoded_opcode = Serializable<Packet::OpcodeType>(opcode).Serialize();
	const ByteSize expected_size = encoded_opcode.size() + payload.size();
	ASSERT_EQUAL(expected_size, wire.Size());
	ASSERT_EQUAL(expected_size, wire.Available());
	ASSERT_SIZE(wire.Data(), expected_size);
	Buffer::FIFO expected;
	ASSERT_TRUE(expected.Write(encoded_opcode));
	if (!payload.empty())
		ASSERT_TRUE(expected.Write(payload));
	ASSERT_EQUAL(expected.Data(), wire.Data());
	Safe::Binary read_opcode;
	ASSERT_TRUE(wire.Read(encoded_opcode.size(), read_opcode));
	ASSERT_EQUAL(encoded_opcode, read_opcode);
	ASSERT_EQUAL(payload.size(), wire.Available());
	if (!payload.empty()) {
		Safe::Binary read_payload;
		ASSERT_TRUE(wire.Read(payload.size(), read_payload));
		ASSERT_EQUAL(payload, read_payload);
	}
	ASSERT_EQUAL(ByteSize{0}, wire.Available());
	ASSERT_EQUAL(expected_size, wire.Size());
	RETURN_TEST(0);
}

// -------------------
// Assign
// -------------------

int test_base_copy_assignment() {
	PacketFixture source(41, Safe::Binary{"source"});
	const Safe::Binary target_payload{"target"};
	PacketFixture target(7, target_payload);
	Packet& target_base = target;
	const Packet& source_base = source;
	ASSERT_EQUAL(&target_base, &(target_base = source_base));
	ASSERT_EQUAL(source.Opcode(), target.Opcode());
	ASSERT_EQUAL(0, CheckWire(target, 41, target_payload));
	ASSERT_EQUAL(0, CheckWire(source, 41, Safe::Binary{"source"}));
	RETURN_TEST(0);
}

int test_base_move_assignment() {
	PacketFixture source(42, Safe::Binary{"source"});
	const Safe::Binary target_payload{"target"};
	PacketFixture target(8, target_payload);
	Packet& target_base = target;
	Packet& source_base = source;
	ASSERT_EQUAL(&target_base, &(target_base = std::move(source_base)));
	ASSERT_EQUAL(Packet::OpcodeType{42}, target.Opcode());
	ASSERT_EQUAL(Packet::OpcodeType{42}, source.Opcode());
	ASSERT_EQUAL(0, CheckWire(target, 42, target_payload));
	ASSERT_EQUAL(0, CheckWire(source, 42, Safe::Binary{"source"}));
	RETURN_TEST(0);
}

int test_base_self_assignment() {
	const Safe::Binary payload{"unchanged"};
	PacketFixture packet(43, payload);
	Packet& base = packet;
	Packet* alias = &base;
	ASSERT_EQUAL(&base, &(base = *alias));
	ASSERT_EQUAL(0, CheckWire(packet, 43, payload));
	ASSERT_EQUAL(&base, &(base = std::move(*alias)));
	ASSERT_EQUAL(0, CheckWire(packet, 43, payload));
	RETURN_TEST(0);
}

// -------------------
// Construct
// -------------------

int test_copy_construction() {
	const Safe::Binary payload{std::string_view{"a\0b", 3}};
	PacketFixture source(44, payload);
	PacketFixture copy(source);
	source = PacketFixture(1, Safe::Binary{"replacement"});
	ASSERT_EQUAL(Packet::OpcodeType{44}, copy.Opcode());
	ASSERT_EQUAL(0, CheckWire(copy, 44, payload));
	ASSERT_EQUAL(0, CheckWire(source, 1, Safe::Binary{"replacement"}));
	RETURN_TEST(0);
}

int test_move_construction() {
	const Safe::Binary payload{"moved"};
	PacketFixture source(45, payload);
	PacketFixture moved(std::move(source));
	ASSERT_EQUAL(Packet::OpcodeType{45}, moved.Opcode());
	ASSERT_EQUAL(Packet::OpcodeType{45}, source.Opcode());
	ASSERT_EQUAL(0, CheckWire(moved, 45, payload));
	ASSERT_EQUAL(0, CheckWire(source, 45, Safe::Binary{}));
	source = PacketFixture(46, payload);
	ASSERT_EQUAL(0, CheckWire(source, 46, payload));
	RETURN_TEST(0);
}

int test_opcode_boundaries() {
	const std::array<Packet::OpcodeType, 5> opcodes{
		0, Packet::PROCESS_THRESHOLD - 1, Packet::PROCESS_THRESHOLD,
		Packet::PROCESS_THRESHOLD + 1, std::numeric_limits<Packet::OpcodeType>::max()
	};
	ASSERT_EQUAL(Packet::OpcodeType{10}, Packet::PROCESS_THRESHOLD);
	for (const auto opcode: opcodes) {
		const PacketFixture packet(opcode);
		ASSERT_EQUAL(opcode, packet.Opcode());
		ASSERT_EQUAL(0, CheckWire(packet, opcode, Safe::Binary{}));
	}
	RETURN_TEST(0);
}

// -------------------
// Ownership
// -------------------

int test_polymorphic_factory_lifetime() {
	unsigned int destructions = 0;
	const Safe::Binary payload{"owned"};
	{
		auto owner = PacketPointer::MakePointer<PacketFixture>(47, payload, &destructions);
		ASSERT_NOT_NULL(owner.get());
		ASSERT_NOT_NULL(dynamic_cast<PacketFixture*>(owner.get()));
		ASSERT_EQUAL(0, CheckWire(*owner, 47, payload));
		auto copy = owner;
		ASSERT_EQUAL(owner.get(), copy.get());
		owner.reset();
		ASSERT_NULL(owner.get());
		ASSERT_EQUAL(0U, destructions);
		auto moved = std::move(copy);
		ASSERT_NULL(copy.get());
		ASSERT_EQUAL(0, CheckWire(*moved, 47, payload));
		moved.reset();
		ASSERT_EQUAL(1U, destructions);
	}
	ASSERT_EQUAL(1U, destructions);
	RETURN_TEST(0);
}

// -------------------
// Serialize
// -------------------

int test_embedded_null_payload() {
	const Safe::Binary payload{std::string_view{"\0a\0b\0", 5}};
	ASSERT_SIZE(payload, ByteSize{5});
	const PacketFixture packet(48, payload);
	ASSERT_EQUAL(0, CheckWire(packet, 48, payload));
	RETURN_TEST(0);
}

int test_empty_payload() {
	const PacketFixture packet(49);
	ASSERT_EQUAL(0, CheckWire(packet, 49, Safe::Binary{}));
	RETURN_TEST(0);
}

int test_nonempty_payload() {
	const Safe::Binary payload{std::byte{0}, std::byte{1}, std::byte{127}, std::byte{255}};
	const PacketFixture packet(50, payload);
	ASSERT_EQUAL(0, CheckWire(packet, 50, payload));
	RETURN_TEST(0);
}

int test_repeated_serialization_independence() {
	const Safe::Binary payload{"stable"};
	const PacketFixture packet(51, payload);
	auto first = packet.Serialize();
	Safe::Binary consumed;
	ASSERT_TRUE(first.Extract(first.Size(), consumed));
	ASSERT_EQUAL(ByteSize{0}, first.Size());
	ASSERT_EQUAL(0, CheckWire(packet, 51, payload));
	ASSERT_EQUAL(0, CheckWire(packet, 51, payload));
	RETURN_TEST(0);
}

int main() {
	int result = 0;

	// -------------------
	// Assign
	// -------------------
	result += test_base_copy_assignment();
	result += test_base_move_assignment();
	result += test_base_self_assignment();

	// -------------------
	// Construct
	// -------------------
	result += test_copy_construction();
	result += test_move_construction();
	result += test_opcode_boundaries();

	// -------------------
	// Ownership
	// -------------------
	result += test_polymorphic_factory_lifetime();

	// -------------------
	// Serialize
	// -------------------
	result += test_embedded_null_payload();
	result += test_empty_payload();
	result += test_nonempty_payload();
	result += test_repeated_serialization_independence();

	RETURN_TEST(result);
}