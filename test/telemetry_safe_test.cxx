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

#include <StormByte/network/telemetry.hxx>
#include <StormByte/safe/pointers.hxx>
#include <StormByte/safe/string.hxx>
#include <StormByte/safe/thread.hxx>
#include <StormByte/telemetry.hxx>
#include <StormByte/test_handlers.h>
#include <StormByte/type_traits.hxx>

#include <chrono>
#include <cstdint>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

namespace {
	/**
	 * @class TelemetryFixture
	 * @brief Test-provider leaf exposing only protected measurement operations.
	 * @details No extra state is owned. Copy and move are disabled; construction,
	 * conversion and destruction remain in this executable's provider. Network
	 * and Base must remain loaded until all owners and samples are released.
	 */
	class TelemetryFixture final: public StormByte::Network::Telemetry {
		public:
			/**
			 * @brief Construct an empty named-clock drawer.
			 */
			TelemetryFixture() noexcept = default;

			/**
			 * @brief Copy construction is disabled.
			 * @param other Source fixture.
			 */
			TelemetryFixture(const TelemetryFixture& other) = delete;

			/**
			 * @brief Move construction is disabled.
			 * @param other Source fixture.
			 */
			TelemetryFixture(TelemetryFixture&& other) = delete;

			/**
			 * @brief Destroy the drawer without invalidating outstanding samples.
			 */
			~TelemetryFixture() noexcept override = default;

			/**
			 * @brief Copy assignment is disabled.
			 * @param other Source fixture.
			 * @return No value; this operation is deleted.
			 */
			TelemetryFixture& operator=(const TelemetryFixture& other) = delete;

			/**
			 * @brief Move assignment is disabled.
			 * @param other Source fixture.
			 * @return No value; this operation is deleted.
			 */
			TelemetryFixture& operator=(TelemetryFixture&& other) = delete;

			/**
			 * @brief Start a sample through Network's protected Measure operation.
			 * @param name Named operation.
			 * @return Independent Base-owned sample.
			 */
			StormByte::Clock::Sample Start(std::string_view name) noexcept {
				return Measure(name);
			}

			/**
			 * @brief Expose mutable access to the protected named clock.
			 * @param name Named operation.
			 * @return Aggregate clock, created if necessary.
			 */
			StormByte::Clock& NamedClock(std::string_view name) {
				return Clock(name);
			}

			/**
			 * @brief Expose read-only access without creating a missing clock.
			 * @param name Named operation.
			 * @return Existing clock, or Base's empty fallback.
			 */
			const StormByte::Clock& NamedClock(std::string_view name) const {
				return Clock(name);
			}

			/**
			 * @brief Produce an owned string in the test provider.
			 * @return Fixture text owned by Base.
			 */
			operator StormByte::Safe::String() const override {
				return StormByte::Safe::String{"network-telemetry"};
			}
	};
}

STORMBYTE_DECLARE_MAYBE_SAFE(TelemetryFixture);

using namespace StormByte;

// -------------------
// Construct
// -------------------

int test_initial_clocks() {
	TelemetryFixture telemetry;
	const TelemetryFixture& view = telemetry;
	const auto missing = view.NamedClock("missing").GetValues();
	ASSERT_EQUAL(0ull, missing.Count);
	ASSERT_EQUAL(std::chrono::microseconds{0}, missing.Time);
	ASSERT_EQUAL(std::chrono::microseconds{0}, missing.MeanDuration);
	const auto& clock = telemetry.NamedClock("created");
	ASSERT_EQUAL(0ull, clock.Count());
	ASSERT_EQUAL(std::chrono::microseconds{0}, clock.Time());
	ASSERT_EQUAL(std::chrono::microseconds{0}, clock.MeanDuration());
	ASSERT_EQUAL(&clock, &telemetry.NamedClock("created"));
	ASSERT_EQUAL(&clock, &view.NamedClock("created"));
	RETURN_TEST(0);
}

int test_maybe_safe_traits() {
	static_assert(std::is_abstract_v<Network::Telemetry>);
	static_assert(std::has_virtual_destructor_v<Network::Telemetry>);
	static_assert(Type::MaybeSafe<Network::Telemetry>);
	static_assert(Type::MaybeSafe<const Network::Telemetry&>);
	static_assert(Type::MaybeSafe<TelemetryFixture>);
	static_assert(Type::MaybeSafe<Safe::Shared<TelemetryFixture>>);
	static_assert(Type::MaybeSafe<Safe::Shared<Network::Telemetry>>);
	static_assert(Type::MaybeSafe<Clock::Sample>);
	static_assert(!Type::SafeValue<Clock::Sample>);
	static_assert(!std::is_copy_constructible_v<Clock::Sample>);
	static_assert(!std::is_copy_assignable_v<Clock::Sample>);
	static_assert(std::is_nothrow_move_constructible_v<Clock::Sample>);
	static_assert(std::is_nothrow_move_assignable_v<Clock::Sample>);
	RETURN_TEST(0);
}

// -------------------
// Conversion
// -------------------

int test_polymorphic_strings() {
	const TelemetryFixture telemetry;
	const Network::Telemetry& network = telemetry;
	const StormByte::Telemetry& base = telemetry;
	const Safe::String direct = static_cast<Safe::String>(telemetry);
	const Safe::String through_network = static_cast<Safe::String>(network);
	const Safe::String through_base = static_cast<Safe::String>(base);
	ASSERT_EQUAL(std::string_view{"network-telemetry"}, std::string_view{direct});
	ASSERT_EQUAL(std::string_view{direct}, std::string_view{through_network});
	ASSERT_EQUAL(std::string_view{direct}, std::string_view{through_base});
	ASSERT_EQUAL(std::string{"network-telemetry"}, static_cast<std::string>(telemetry));
	ASSERT_EQUAL(std::string{"network-telemetry"}, static_cast<std::string>(network));
	ASSERT_EQUAL(std::string{"network-telemetry"}, static_cast<std::string>(base));
	RETURN_TEST(0);
}

// -------------------
// Ownership
// -------------------

int test_shared_factory() {
	auto owner = Safe::Shared<Network::Telemetry>::MakePointer<TelemetryFixture>();
	Safe::Weak<Network::Telemetry> observer = owner;
	Safe::Shared<StormByte::Telemetry> base = owner;
	ASSERT_NOT_NULL(owner.get());
	ASSERT_EQUAL(2l, owner.use_count());
	owner.reset();
	ASSERT_FALSE(observer.expired());
	ASSERT_EQUAL(std::string{"network-telemetry"}, static_cast<std::string>(*base));
	Safe::String retained = static_cast<Safe::String>(*base);
	base.reset();
	ASSERT_TRUE(observer.expired());
	ASSERT_EQUAL(std::string_view{"network-telemetry"}, std::string_view{retained});
	RETURN_TEST(0);
}

// -------------------
// Samples
// -------------------

int test_concurrent_samples() {
	TelemetryFixture telemetry;
	constexpr int iterations = 128;
	auto record = [&telemetry]() noexcept {
		for (int iteration = 0; iteration < iterations; ++iteration) {
			auto sample = telemetry.Start("concurrent");
			(void)sample.Stop();
		}
	};
	Safe::Thread first;
	Safe::Thread second;
	try {
		first = Safe::Thread{[&record]() noexcept {
			record();
		}};
		second = Safe::Thread{[&record]() noexcept {
			record();
		}};
	}
	catch (...) {
		if (first.joinable())
			first.join();
		if (second.joinable())
			second.join();
		throw;
	}
	first.join();
	second.join();
	const TelemetryFixture& view = telemetry;
	const auto values = view.NamedClock("concurrent").GetValues();
	ASSERT_EQUAL(static_cast<std::uint64_t>(2 * iterations), values.Count);
	ASSERT_TRUE(values.Time.count() >= 0);
	ASSERT_EQUAL(values.Time / static_cast<std::int64_t>(values.Count), values.MeanDuration);
	RETURN_TEST(0);
}

int test_destructor_records() {
	TelemetryFixture telemetry;
	{
		auto sample = telemetry.Start("destructor");
		ASSERT_TRUE(sample.Active());
		ASSERT_EQUAL(0ull, telemetry.NamedClock("destructor").Count());
	}
	ASSERT_EQUAL(1ull, telemetry.NamedClock("destructor").Count());
	RETURN_TEST(0);
}

int test_distinct_labels() {
	TelemetryFixture telemetry;
	auto first = telemetry.Start("first");
	auto second = telemetry.Start("second");
	auto empty = telemetry.Start("");
	auto embedded = telemetry.Start(std::string_view{"first\0suffix", 12});
	ASSERT_TRUE(first.Active());
	ASSERT_TRUE(second.Active());
	ASSERT_TRUE(empty.Active());
	ASSERT_TRUE(embedded.Active());
	(void)first.Stop();
	(void)second.Stop();
	(void)empty.Stop();
	(void)embedded.Stop();
	const TelemetryFixture& view = telemetry;
	ASSERT_EQUAL(1ull, view.NamedClock("first").Count());
	ASSERT_EQUAL(1ull, view.NamedClock("second").Count());
	ASSERT_EQUAL(1ull, view.NamedClock("").Count());
	ASSERT_EQUAL(1ull, view.NamedClock(std::string_view{"first\0suffix", 12}).Count());
	ASSERT_EQUAL(0ull, view.NamedClock("missing").Count());
	RETURN_TEST(0);
}

int test_empty_sample() {
	Clock::Sample sample;
	ASSERT_FALSE(sample.Active());
	ASSERT_EQUAL(std::chrono::microseconds{0}, sample.Stop());
	ASSERT_EQUAL(std::chrono::microseconds{0}, sample.Stop());
	RETURN_TEST(0);
}

int test_label_is_owned() {
	TelemetryFixture telemetry;
	std::string label{"owned-label"};
	auto first = telemetry.Start(label);
	label.assign("replacement");
	auto second = telemetry.Start("owned-label");
	(void)first.Stop();
	(void)second.Stop();
	const TelemetryFixture& view = telemetry;
	ASSERT_EQUAL(2ull, view.NamedClock("owned-label").Count());
	ASSERT_EQUAL(0ull, view.NamedClock(label).Count());
	RETURN_TEST(0);
}

int test_move_assignment_records_destination() {
	TelemetryFixture telemetry;
	auto source = telemetry.Start("source");
	auto destination = telemetry.Start("destination");
	ASSERT_TRUE(source.Active());
	ASSERT_TRUE(destination.Active());
	destination = std::move(source);
	ASSERT_FALSE(source.Active());
	ASSERT_TRUE(destination.Active());
	ASSERT_EQUAL(1ull, telemetry.NamedClock("destination").Count());
	ASSERT_EQUAL(0ull, telemetry.NamedClock("source").Count());
	(void)destination.Stop();
	(void)source.Stop();
	ASSERT_EQUAL(1ull, telemetry.NamedClock("source").Count());
	ASSERT_EQUAL(1ull, telemetry.NamedClock("destination").Count());
	RETURN_TEST(0);
}

int test_move_constructor_records_once() {
	TelemetryFixture telemetry;
	{
		auto source = telemetry.Start("move");
		ASSERT_TRUE(source.Active());
		Clock::Sample destination{std::move(source)};
		ASSERT_FALSE(source.Active());
		ASSERT_TRUE(destination.Active());
		(void)source.Stop();
		ASSERT_EQUAL(0ull, telemetry.NamedClock("move").Count());
	}
	ASSERT_EQUAL(1ull, telemetry.NamedClock("move").Count());
	RETURN_TEST(0);
}

int test_move_empty_records_destination() {
	TelemetryFixture telemetry;
	auto destination = telemetry.Start("empty-move");
	Clock::Sample source;
	destination = std::move(source);
	ASSERT_FALSE(source.Active());
	ASSERT_FALSE(destination.Active());
	ASSERT_EQUAL(std::chrono::microseconds{0}, destination.Stop());
	ASSERT_EQUAL(1ull, telemetry.NamedClock("empty-move").Count());
	RETURN_TEST(0);
}

int test_move_self_preserves_sample() {
	TelemetryFixture telemetry;
	auto sample = telemetry.Start("self-move");
	auto* self = &sample;
	ASSERT_EQUAL(self, &(sample = std::move(*self)));
	ASSERT_TRUE(sample.Active());
	ASSERT_EQUAL(0ull, telemetry.NamedClock("self-move").Count());
	(void)sample.Stop();
	ASSERT_EQUAL(1ull, telemetry.NamedClock("self-move").Count());
	RETURN_TEST(0);
}

int test_overlapping_samples() {
	TelemetryFixture telemetry;
	auto outer = telemetry.Start("overlap");
	auto inner = telemetry.Start("overlap");
	ASSERT_TRUE(outer.Active());
	ASSERT_TRUE(inner.Active());
	ASSERT_EQUAL(0ull, telemetry.NamedClock("overlap").Count());
	const auto inner_elapsed = inner.Stop();
	ASSERT_TRUE(outer.Active());
	ASSERT_EQUAL(1ull, telemetry.NamedClock("overlap").Count());
	const auto outer_elapsed = outer.Stop();
	ASSERT_TRUE(inner_elapsed.count() >= 0);
	ASSERT_TRUE(outer_elapsed >= inner_elapsed);
	const auto values = telemetry.NamedClock("overlap").GetValues();
	ASSERT_EQUAL(2ull, values.Count);
	ASSERT_EQUAL(inner_elapsed + outer_elapsed, values.Time);
	ASSERT_EQUAL(values.Time / 2, values.MeanDuration);
	RETURN_TEST(0);
}

int test_sample_survives_owner_destruction() {
	Clock::Sample stopped;
	Clock::Sample destroyed;
	{
		auto owner = Safe::MakeShared<TelemetryFixture>();
		Safe::Weak<TelemetryFixture> observer = owner;
		stopped = owner->Start("survives-stop");
		destroyed = owner->Start("survives-destructor");
		ASSERT_TRUE(stopped.Active());
		ASSERT_TRUE(destroyed.Active());
		owner.reset();
		ASSERT_TRUE(observer.expired());
	}
	ASSERT_TRUE(stopped.Active());
	ASSERT_TRUE(destroyed.Active());
	const auto elapsed = stopped.Stop();
	ASSERT_TRUE(elapsed.count() >= 0);
	ASSERT_FALSE(stopped.Active());
	ASSERT_EQUAL(elapsed, stopped.Stop());
	RETURN_TEST(0);
}

int test_stop_is_idempotent() {
	TelemetryFixture telemetry;
	std::chrono::microseconds elapsed{0};
	{
		auto sample = telemetry.Start("stop");
		ASSERT_TRUE(sample.Active());
		elapsed = sample.Stop();
		ASSERT_FALSE(sample.Active());
		ASSERT_EQUAL(elapsed, sample.Stop());
		ASSERT_EQUAL(elapsed, sample.Stop());
		ASSERT_EQUAL(1ull, telemetry.NamedClock("stop").Count());
	}
	ASSERT_EQUAL(1ull, telemetry.NamedClock("stop").Count());
	ASSERT_EQUAL(elapsed, telemetry.NamedClock("stop").Time());
	ASSERT_EQUAL(elapsed, telemetry.NamedClock("stop").MeanDuration());
	RETURN_TEST(0);
}

int test_thread_transfer() {
	TelemetryFixture telemetry;
	auto sample = telemetry.Start("transfer");
	ASSERT_TRUE(sample.Active());
	Safe::Thread worker{[taken = std::move(sample)]() mutable noexcept {
		(void)taken.Stop();
	}};
	worker.join();
	ASSERT_FALSE(sample.Active());
	ASSERT_EQUAL(1ull, telemetry.NamedClock("transfer").Count());
	RETURN_TEST(0);
}

int main() {
	int result = 0;

	// -------------------
	// Construct
	// -------------------
	result += test_initial_clocks();
	result += test_maybe_safe_traits();

	// -------------------
	// Conversion
	// -------------------
	result += test_polymorphic_strings();

	// -------------------
	// Ownership
	// -------------------
	result += test_shared_factory();

	// -------------------
	// Samples
	// -------------------
	result += test_concurrent_samples();
	result += test_destructor_records();
	result += test_distinct_labels();
	result += test_empty_sample();
	result += test_label_is_owned();
	result += test_move_assignment_records_destination();
	result += test_move_constructor_records_once();
	result += test_move_empty_records_destination();
	result += test_move_self_preserves_sample();
	result += test_overlapping_samples();
	result += test_sample_survives_owner_destruction();
	result += test_stop_is_idempotent();
	result += test_thread_transfer();
	RETURN_TEST(result);
}
