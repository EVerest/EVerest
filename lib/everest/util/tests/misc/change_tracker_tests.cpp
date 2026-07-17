// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include <everest/util/misc/change_tracker.hpp>
#include <gtest/gtest.h>

#include <type_traits>

namespace everest::lib::util {

namespace {
struct TestTelemetry {
    int integer_field{0};
    float float_field{0.f};
};

static_assert(!std::is_copy_constructible_v<change_tracker<TestTelemetry>>);
static_assert(!std::is_copy_assignable_v<change_tracker<TestTelemetry>>);
static_assert(!std::is_move_constructible_v<change_tracker<TestTelemetry>>);
static_assert(!std::is_move_assignable_v<change_tracker<TestTelemetry>>);
} // namespace

class ChangeTrackerTests : public ::testing::Test {};

TEST_F(ChangeTrackerTests, ExactSetReportsChangeAndNoChange) {
    TestTelemetry telemetry;
    change_tracker<TestTelemetry> tracker{telemetry};

    tracker.set(&TestTelemetry::integer_field, 7);
    EXPECT_TRUE(tracker.changed());

    TestTelemetry telemetry_same;
    telemetry_same.integer_field = 7;
    change_tracker<TestTelemetry> unchanged{telemetry_same};
    unchanged.set(&TestTelemetry::integer_field, 7);
    EXPECT_FALSE(unchanged.changed());

    TestTelemetry telemetry_change;
    telemetry_change.integer_field = 7;
    change_tracker<TestTelemetry> changed_again{telemetry_change};
    changed_again.set(&TestTelemetry::integer_field, 7);
    EXPECT_FALSE(changed_again.changed());
    changed_again.set(&TestTelemetry::integer_field, 8);
    EXPECT_TRUE(changed_again.changed());
}

TEST_F(ChangeTrackerTests, SetAlmostEqSuppressesJitter) {
    TestTelemetry telemetry_tiny_jitter;
    change_tracker<TestTelemetry> tiny_jitter_tracker{telemetry_tiny_jitter};

    tiny_jitter_tracker.set_almost_eq<2>(&TestTelemetry::float_field, 1.00f);
    EXPECT_FLOAT_EQ(tiny_jitter_tracker.value().float_field, 1.0f);
    EXPECT_TRUE(tiny_jitter_tracker.changed());
}

TEST_F(ChangeTrackerTests, SetAlmostEqTriggersOnMeaningfulChange) {
    TestTelemetry telemetry;
    telemetry.float_field = 1.0f;
    change_tracker<TestTelemetry> tracker{telemetry};

    tracker.set_almost_eq<2>(&TestTelemetry::float_field, 1.005f);
    EXPECT_FLOAT_EQ(tracker.value().float_field, 1.0f);
    EXPECT_FALSE(tracker.changed());

    tracker.set_almost_eq<2>(&TestTelemetry::float_field, 1.02f);
    EXPECT_FLOAT_EQ(tracker.value().float_field, 1.02f);
    EXPECT_TRUE(tracker.changed());
}

TEST_F(ChangeTrackerTests, MutableValueMarksChanged) {
    TestTelemetry telemetry;
    change_tracker<TestTelemetry> tracker{telemetry};

    tracker.mutable_value().integer_field = 10;
    EXPECT_TRUE(tracker.changed());
    EXPECT_TRUE(tracker.value().integer_field == 10);
}

TEST_F(ChangeTrackerTests, ValueAccessorReturnsConstRef) {
    TestTelemetry telemetry;
    change_tracker<TestTelemetry> tracker{telemetry};

    tracker.set(&TestTelemetry::integer_field, 123);
    tracker.set(&TestTelemetry::float_field, 42.5f);
    EXPECT_TRUE(tracker.changed());

    const auto& value = tracker.value();
    EXPECT_EQ(value.integer_field, 123);
    EXPECT_FLOAT_EQ(value.float_field, 42.5f);
}

} // namespace everest::lib::util
