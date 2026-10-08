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

#include <StormByte/buffer/producer.hxx>
#include <StormByte/logger/threaded_log.hxx>
#include <StormByte/network/typedefs.hxx>
#include <StormByte/test_handlers.h>

#include <iostream>
#include <limits>
#include <string_view>
#include <type_traits>
#include <utility>

using namespace StormByte;
using namespace StormByte::Network;

/**
 * @brief State-free public packet leaf allocated and released by this test provider.
 */
class DeserializePacketFixture final: public Transport::Packet {
	public:
		/**
		 * @brief Construct a packet with an empty serialized payload.
		 * @param opcode Wire opcode.
		 */
		explicit DeserializePacketFixture(OpcodeType opcode): Packet(opcode) {}

		/**
		 * @brief Copy the public packet state.
		 * @param other Source packet.
		 */
		DeserializePacketFixture(const DeserializePacketFixture& other) = default;

		/**
		 * @brief Transfer the public packet state.
		 * @param other Source packet.
		 */
		DeserializePacketFixture(DeserializePacketFixture&& other) noexcept = default;

		/**
		 * @brief Assign the public packet state.
		 * @param other Source packet.
		 * @return This packet.
		 */
		DeserializePacketFixture& operator=(const DeserializePacketFixture& other) = default;

		/**
		 * @brief Move-assign the public packet state.
		 * @param other Source packet.
		 * @return This packet.
		 */
		DeserializePacketFixture& operator=(DeserializePacketFixture&& other) noexcept = default;

		/**
		 * @brief Release the leaf in its allocating provider.
		 */
		~DeserializePacketFixture() noexcept override = default;

	protected:
		/**
		 * @brief Serialize the empty application payload.
		 * @return Empty bytes.
		 */
		Safe::Binary DoSerialize() const noexcept override {
			return {};
		}
};

STORMBYTE_DECLARE_MAYBE_SAFE(DeserializePacketFixture);

/**
 * @brief Borrowed lifecycle counters that outlive every counted callable.
 */
struct CallableCounts {
	unsigned int live = 0;
	unsigned int copies = 0;
	unsigned int moves = 0;
	unsigned int destructions = 0;
};

/**
 * @brief Mutable application callable with owned bytes and observable lifecycle.
 */
class CountedCallable final {
	public:
		/**
		 * @brief Own bytes and borrow counters for this callable's lifetime.
		 * @param counts Counters that outlive this callable and every clone.
		 */
		explicit CountedCallable(CallableCounts& counts): m_counts(&counts), m_bytes("owned") {
			++m_counts->live;
		}

		/**
		 * @brief Clone owned bytes and mutable invocation state.
		 * @param other Source callable.
		 */
		CountedCallable(const CountedCallable& other):
			m_counts(other.m_counts), m_bytes(other.m_bytes), m_calls(other.m_calls) {
			++m_counts->live;
			++m_counts->copies;
		}

		/**
		 * @brief Transfer owned bytes while preserving both objects' counter borrows.
		 * @param other Source callable.
		 */
		CountedCallable(CountedCallable&& other) noexcept:
			m_counts(other.m_counts), m_bytes(std::move(other.m_bytes)), m_calls(other.m_calls) {
			++m_counts->live;
			++m_counts->moves;
		}

		/**
		 * @brief Disable target assignment; callback assignment replaces storage.
		 * @param other Source callable.
		 * @return No value; this operation is deleted.
		 */
		CountedCallable& operator=(const CountedCallable& other) = delete;

		/**
		 * @brief Disable target move assignment.
		 * @param other Source callable.
		 * @return No value; this operation is deleted.
		 */
		CountedCallable& operator=(CountedCallable&& other) = delete;

		/**
		 * @brief Release owned bytes and record exactly one target destruction.
		 */
		~CountedCallable() noexcept {
			--m_counts->live;
			++m_counts->destructions;
		}

		/**
		 * @brief Return the invocation count as a packet opcode.
		 * @param opcode Unused wire opcode.
		 * @param payload Unused synchronous payload.
		 * @param logger Unused synchronous logger.
		 * @return Packet identifying independent target state.
		 */
		PacketPointer operator()(DeserializePacketFunction::OpcodeType, Buffer::Consumer,
			Safe::Shared<Logger::Log>) {
			return PacketPointer::MakePointer<DeserializePacketFixture>(++m_calls);
		}

	private:
		CallableCounts* m_counts;
		Safe::Binary m_bytes;
		DeserializePacketFunction::OpcodeType m_calls = 0;
};

/**
 * @brief Decode an opcode through an ordinary public callback function.
 * @param opcode Wire opcode.
 * @param payload Unused synchronous payload.
 * @param logger Unused synchronous logger.
 * @return Packet with the supplied opcode.
 */
PacketPointer DecodeOpcode(DeserializePacketFunction::OpcodeType opcode, Buffer::Consumer,
	Safe::Shared<Logger::Log>) {
	return PacketPointer::MakePointer<DeserializePacketFixture>(opcode);
}

/**
 * @brief Invoke through the public const interface and compare the returned opcode.
 * @param function Callback to invoke.
 * @param expected Expected packet opcode.
 * @return Test status.
 */
int CheckInvocation(const DeserializePacketFunction& function, DeserializePacketFunction::OpcodeType expected) {
	auto packet = function(0, Buffer::Consumer{}, {});
	ASSERT_NOT_NULL(packet.get());
	ASSERT_EQUAL(expected, packet->Opcode());
	RETURN_TEST(0);
}

// -------------------
// Classification
// -------------------

int test_maybe_safe_traits() {
	static_assert(Type::MaybeSafe<DeserializePacketFunction>);
	static_assert(Type::MaybeSafe<const DeserializePacketFunction&>);
	static_assert(Type::MaybeSafe<DeserializePacketFixture>);
	static_assert(Type::SafeValue<DeserializePacketFunction>);
	static_assert(Type::CopyConstructible<DeserializePacketFunction>);
	static_assert(std::is_copy_assignable_v<DeserializePacketFunction>);
	static_assert(std::is_nothrow_default_constructible_v<DeserializePacketFunction>);
	static_assert(std::is_nothrow_move_constructible_v<DeserializePacketFunction>);
	static_assert(std::is_nothrow_move_assignable_v<DeserializePacketFunction>);
	static_assert(std::is_nothrow_destructible_v<DeserializePacketFunction>);
	RETURN_TEST(0);
}

// -------------------
// Construct
// -------------------

int test_copy_construction_clones_state() {
	CallableCounts counts;
	{
		DeserializePacketFunction source{CountedCallable{counts}};
		ASSERT_EQUAL(0, CheckInvocation(source, 1));
		DeserializePacketFunction copy{source};
		ASSERT_TRUE(source);
		ASSERT_TRUE(copy);
		ASSERT_EQUAL(2U, counts.live);
		ASSERT_EQUAL(1U, counts.copies);
		ASSERT_EQUAL(0, CheckInvocation(copy, 2));
		ASSERT_EQUAL(0, CheckInvocation(copy, 3));
		ASSERT_EQUAL(0, CheckInvocation(source, 2));
		source = DeserializePacketFunction{};
		ASSERT_EQUAL(1U, counts.live);
		ASSERT_EQUAL(0, CheckInvocation(copy, 4));
	}
	ASSERT_EQUAL(0U, counts.live);
	ASSERT_EQUAL(3U, counts.destructions);
	RETURN_TEST(0);
}

int test_empty_construction_copy_and_move() {
	DeserializePacketFunction empty;
	DeserializePacketFunction copy{empty};
	DeserializePacketFunction moved{std::move(empty)};
	ASSERT_FALSE(empty);
	ASSERT_FALSE(copy);
	ASSERT_FALSE(moved);
	ASSERT_NULL(empty(0, Buffer::Consumer{}, {}).get());
	ASSERT_NULL(copy(0, Buffer::Consumer{}, {}).get());
	ASSERT_NULL(moved(0, Buffer::Consumer{}, {}).get());
	RETURN_TEST(0);
}

int test_function_pointer_construction() {
	const DeserializePacketFunction function{&DecodeOpcode};
	ASSERT_TRUE(function);
	auto packet = function(91, Buffer::Consumer{}, {});
	ASSERT_NOT_NULL(packet.get());
	ASSERT_EQUAL(DeserializePacketFunction::OpcodeType{91}, packet->Opcode());
	RETURN_TEST(0);
}

int test_lvalue_callable_construction() {
	CallableCounts counts;
	{
		CountedCallable callable{counts};
		DeserializePacketFunction function{callable};
		ASSERT_TRUE(function);
		ASSERT_EQUAL(2U, counts.live);
		ASSERT_EQUAL(1U, counts.copies);
		ASSERT_EQUAL(0U, counts.moves);
		ASSERT_EQUAL(0, CheckInvocation(function, 1));
		auto packet = callable(0, Buffer::Consumer{}, {});
		ASSERT_NOT_NULL(packet.get());
		ASSERT_EQUAL(DeserializePacketFunction::OpcodeType{1}, packet->Opcode());
	}
	ASSERT_EQUAL(0U, counts.live);
	ASSERT_EQUAL(2U, counts.destructions);
	RETURN_TEST(0);
}

int test_move_construction_transfers_target() {
	CallableCounts counts;
	{
		DeserializePacketFunction source{CountedCallable{counts}};
		ASSERT_EQUAL(0, CheckInvocation(source, 1));
		DeserializePacketFunction moved{std::move(source)};
		ASSERT_FALSE(source);
		ASSERT_TRUE(moved);
		ASSERT_EQUAL(1U, counts.live);
		ASSERT_EQUAL(0U, counts.copies);
		ASSERT_EQUAL(1U, counts.moves);
		ASSERT_NULL(source(0, Buffer::Consumer{}, {}).get());
		ASSERT_EQUAL(0, CheckInvocation(moved, 2));
		source = moved;
		ASSERT_EQUAL(0, CheckInvocation(source, 3));
		ASSERT_EQUAL(0, CheckInvocation(moved, 3));
	}
	ASSERT_EQUAL(0U, counts.live);
	ASSERT_EQUAL(3U, counts.destructions);
	RETURN_TEST(0);
}

int test_rvalue_callable_construction() {
	CallableCounts counts;
	{
		DeserializePacketFunction function{CountedCallable{counts}};
		ASSERT_TRUE(function);
		ASSERT_EQUAL(1U, counts.live);
		ASSERT_EQUAL(0U, counts.copies);
		ASSERT_EQUAL(1U, counts.moves);
		ASSERT_EQUAL(1U, counts.destructions);
		ASSERT_EQUAL(0, CheckInvocation(function, 1));
	}
	ASSERT_EQUAL(0U, counts.live);
	ASSERT_EQUAL(2U, counts.destructions);
	RETURN_TEST(0);
}

// -------------------
// Interface
// -------------------

int test_forwarding_payload_and_logger() {
	const Safe::Binary bytes{std::string_view{"\0a\0b\0", 5}};
	Buffer::Consumer payload;
	auto producer = payload.Producer();
	ASSERT_TRUE(producer.Write(bytes));
	producer.Close();
	auto logger = Safe::Shared<Logger::Log>::MakePointer<Logger::ThreadedLog>(std::cerr, Logger::Level::Error);
	unsigned int calls = 0;
	DeserializePacketFunction::OpcodeType observed_opcode = 0;
	Safe::Binary observed_bytes;
	Logger::Log* observed_logger = nullptr;
	bool same_payload = false;
	bool extracted = false;
	bool closed = false;
	const DeserializePacketFunction function{[&](DeserializePacketFunction::OpcodeType opcode,
		Buffer::Consumer received_payload, Safe::Shared<Logger::Log> received_logger) -> PacketPointer {
		++calls;
		observed_opcode = opcode;
		same_payload = payload == received_payload;
		observed_logger = received_logger.get();
		closed = !received_payload.IsWritable();
		extracted = received_payload.Extract(bytes.size(), observed_bytes);
		return PacketPointer::MakePointer<DeserializePacketFixture>(opcode);
	}};
	const auto opcode = std::numeric_limits<DeserializePacketFunction::OpcodeType>::max();
	auto packet = function(opcode, payload, logger);
	ASSERT_NOT_NULL(packet.get());
	ASSERT_EQUAL(opcode, packet->Opcode());
	ASSERT_EQUAL(opcode, observed_opcode);
	ASSERT_EQUAL(1U, calls);
	ASSERT_EQUAL(true, same_payload);
	ASSERT_EQUAL(true, extracted);
	ASSERT_EQUAL(true, closed);
	ASSERT_EQUAL(bytes, observed_bytes);
	ASSERT_EQUAL(logger.get(), observed_logger);
	ASSERT_EQUAL(ByteSize{0}, payload.Available());
	ASSERT_TRUE(payload.EoF());
	ASSERT_NOT_NULL(logger.get());
	RETURN_TEST(0);
}

int test_null_result_and_empty_arguments() {
	unsigned int calls = 0;
	ByteSize observed_size{1};
	Logger::Log* observed_logger = nullptr;
	DeserializePacketFunction::OpcodeType observed_opcode = 1;
	const DeserializePacketFunction function{[&](DeserializePacketFunction::OpcodeType opcode,
		Buffer::Consumer payload, Safe::Shared<Logger::Log> logger) -> PacketPointer {
		++calls;
		observed_opcode = opcode;
		observed_size = payload.Available();
		observed_logger = logger.get();
		return {};
	}};
	ASSERT_NULL(function(0, Buffer::Consumer{}, {}).get());
	ASSERT_NULL(function(0, Buffer::Consumer{}, {}).get());
	ASSERT_TRUE(function);
	ASSERT_EQUAL(2U, calls);
	ASSERT_EQUAL(DeserializePacketFunction::OpcodeType{0}, observed_opcode);
	ASSERT_EQUAL(ByteSize{0}, observed_size);
	ASSERT_NULL(observed_logger);
	RETURN_TEST(0);
}

int test_public_signatures() {
	using Function = DeserializePacketFunction;
	static_assert(Type::SameAs<Function::OpcodeType, Transport::Packet::OpcodeType>);
	static_assert(Type::SameAs<Function::InvokeFunction,
		PacketPointer (*)(void*, Function::OpcodeType, Buffer::Consumer, Safe::Shared<Logger::Log>)>);
	static_assert(Type::SameAs<Function::CloneFunction, void* (*)(const void*)>);
	static_assert(Type::SameAs<Function::DestroyFunction, void (*)(void*) noexcept>);
	static_assert(Type::SameAs<decltype(&Function::operator()),
		PacketPointer (Function::*)(Function::OpcodeType, Buffer::Consumer, Safe::Shared<Logger::Log>) const>);
	static_assert(Type::SameAs<decltype(&Function::operator bool), bool (Function::*)() const noexcept>);
	static_assert(!std::is_convertible_v<Function, bool>);
	RETURN_TEST(0);
}

// -------------------
// Lifecycle
// -------------------

int test_copy_assignment_replaces_and_clones() {
	CallableCounts source_counts;
	CallableCounts replaced_counts;
	{
		DeserializePacketFunction source{CountedCallable{source_counts}};
		DeserializePacketFunction destination{CountedCallable{replaced_counts}};
		ASSERT_EQUAL(0, CheckInvocation(source, 1));
		ASSERT_EQUAL(&destination, &(destination = source));
		ASSERT_EQUAL(0U, replaced_counts.live);
		ASSERT_EQUAL(2U, replaced_counts.destructions);
		ASSERT_EQUAL(2U, source_counts.live);
		ASSERT_EQUAL(1U, source_counts.copies);
		ASSERT_EQUAL(0, CheckInvocation(destination, 2));
		ASSERT_EQUAL(0, CheckInvocation(destination, 3));
		ASSERT_EQUAL(0, CheckInvocation(source, 2));
		source = DeserializePacketFunction{};
		ASSERT_EQUAL(0, CheckInvocation(destination, 4));
	}
	ASSERT_EQUAL(0U, source_counts.live);
	ASSERT_EQUAL(3U, source_counts.destructions);
	RETURN_TEST(0);
}

int test_empty_assignment_and_reset() {
	CallableCounts counts;
	DeserializePacketFunction empty;
	DeserializePacketFunction destination{CountedCallable{counts}};
	ASSERT_EQUAL(&destination, &(destination = empty));
	ASSERT_FALSE(destination);
	ASSERT_FALSE(empty);
	ASSERT_EQUAL(0U, counts.live);
	ASSERT_EQUAL(2U, counts.destructions);
	ASSERT_NULL(destination(0, Buffer::Consumer{}, {}).get());
	destination = CountedCallable{counts};
	ASSERT_EQUAL(0, CheckInvocation(destination, 1));
	ASSERT_EQUAL(&destination, &(destination = DeserializePacketFunction{}));
	ASSERT_FALSE(destination);
	ASSERT_EQUAL(0U, counts.live);
	ASSERT_EQUAL(4U, counts.destructions);
	destination = CountedCallable{counts};
	ASSERT_EQUAL(&destination, &(destination = std::move(empty)));
	ASSERT_FALSE(destination);
	ASSERT_FALSE(empty);
	ASSERT_EQUAL(0U, counts.live);
	ASSERT_EQUAL(6U, counts.destructions);
	RETURN_TEST(0);
}

int test_move_assignment_replaces_and_transfers() {
	CallableCounts source_counts;
	CallableCounts replaced_counts;
	{
		DeserializePacketFunction source{CountedCallable{source_counts}};
		DeserializePacketFunction destination{CountedCallable{replaced_counts}};
		ASSERT_EQUAL(0, CheckInvocation(source, 1));
		ASSERT_EQUAL(&destination, &(destination = std::move(source)));
		ASSERT_FALSE(source);
		ASSERT_TRUE(destination);
		ASSERT_EQUAL(0U, replaced_counts.live);
		ASSERT_EQUAL(2U, replaced_counts.destructions);
		ASSERT_EQUAL(1U, source_counts.live);
		ASSERT_EQUAL(0U, source_counts.copies);
		ASSERT_EQUAL(1U, source_counts.moves);
		ASSERT_NULL(source(0, Buffer::Consumer{}, {}).get());
		ASSERT_EQUAL(0, CheckInvocation(destination, 2));
		source = std::move(destination);
		ASSERT_FALSE(destination);
		ASSERT_EQUAL(0, CheckInvocation(source, 3));
	}
	ASSERT_EQUAL(0U, source_counts.live);
	ASSERT_EQUAL(2U, source_counts.destructions);
	RETURN_TEST(0);
}

int test_self_assignment_preserves_target() {
	CallableCounts counts;
	{
		DeserializePacketFunction function{CountedCallable{counts}};
		DeserializePacketFunction* alias = &function;
		ASSERT_EQUAL(0, CheckInvocation(function, 1));
		ASSERT_EQUAL(&function, &(function = *alias));
		ASSERT_EQUAL(0, CheckInvocation(function, 2));
		ASSERT_EQUAL(&function, &(function = std::move(*alias)));
		ASSERT_EQUAL(0, CheckInvocation(function, 3));
		ASSERT_EQUAL(1U, counts.live);
		ASSERT_EQUAL(0U, counts.copies);
		ASSERT_EQUAL(1U, counts.moves);
		ASSERT_EQUAL(1U, counts.destructions);
	}
	ASSERT_EQUAL(0U, counts.live);
	ASSERT_EQUAL(2U, counts.destructions);
	DeserializePacketFunction empty;
	DeserializePacketFunction* alias = &empty;
	ASSERT_EQUAL(&empty, &(empty = *alias));
	ASSERT_EQUAL(&empty, &(empty = std::move(*alias)));
	ASSERT_FALSE(empty);
	ASSERT_NULL(empty(0, Buffer::Consumer{}, {}).get());
	RETURN_TEST(0);
}

// -------------------
// Ownership
// -------------------

int test_owning_capture_survives_source_and_clone() {
	Safe::Weak<Transport::Packet> observer;
	DeserializePacketFunction retained;
	{
		auto packet = PacketPointer::MakePointer<DeserializePacketFixture>(73);
		observer = packet;
		Safe::Binary bytes{std::string_view{"a\0b", 3}};
		DeserializePacketFunction source{[packet, bytes](DeserializePacketFunction::OpcodeType,
			Buffer::Consumer, Safe::Shared<Logger::Log>) -> PacketPointer {
			const Safe::Binary expected{std::string_view{"a\0b", 3}};
			return bytes == expected ? packet : PacketPointer{};
		}};
		retained = source;
		packet.reset();
		bytes.clear();
		ASSERT_FALSE(observer.expired());
		ASSERT_EQUAL(0, CheckInvocation(source, 73));
	}
	ASSERT_FALSE(observer.expired());
	ASSERT_EQUAL(0, CheckInvocation(retained, 73));
	auto returned = retained(0, Buffer::Consumer{}, {});
	ASSERT_NOT_NULL(returned.get());
	retained = DeserializePacketFunction{};
	ASSERT_FALSE(retained);
	ASSERT_FALSE(observer.expired());
	ASSERT_EQUAL(Transport::Packet::OpcodeType{73}, returned->Opcode());
	returned.reset();
	ASSERT_TRUE(observer.expired());
	RETURN_TEST(0);
}

int main() {
	int result = 0;

	// -------------------
	// Classification
	// -------------------
	result += test_maybe_safe_traits();

	// -------------------
	// Construct
	// -------------------
	result += test_copy_construction_clones_state();
	result += test_empty_construction_copy_and_move();
	result += test_function_pointer_construction();
	result += test_lvalue_callable_construction();
	result += test_move_construction_transfers_target();
	result += test_rvalue_callable_construction();

	// -------------------
	// Interface
	// -------------------
	result += test_forwarding_payload_and_logger();
	result += test_null_result_and_empty_arguments();
	result += test_public_signatures();

	// -------------------
	// Lifecycle
	// -------------------
	result += test_copy_assignment_replaces_and_clones();
	result += test_empty_assignment_and_reset();
	result += test_move_assignment_replaces_and_transfers();
	result += test_self_assignment_preserves_target();

	// -------------------
	// Ownership
	// -------------------
	result += test_owning_capture_survives_source_and_clone();

	RETURN_TEST(result);
}
