// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include <gtest/gtest.h>

#include "ev_side_voltage/EvSideVoltageCheck.hpp"

#include <chrono>

using namespace module;
using namespace std::chrono_literals;

namespace {

using State = EvSideVoltageCheck::State;

const EvSideVoltageCheck::Clock::time_point T0{1h};

TEST(EvSideVoltageCheckTest, source_that_never_published_is_no_sample) {
    const EvSideVoltageCheck check;
    EXPECT_EQ(check.state(T0), State::NoSample);
    EXPECT_FALSE(check.last_voltage_V().has_value());
    EXPECT_FALSE(check.reverse_voltage());
}

TEST(EvSideVoltageCheckTest, below_60V_is_safe) {
    EvSideVoltageCheck check;
    check.update(59.9, T0);
    EXPECT_EQ(check.state(T0), State::Safe);
    EXPECT_EQ(check.last_voltage_V(), 59.9);
}

TEST(EvSideVoltageCheckTest, exactly_60V_is_unsafe) {
    EvSideVoltageCheck check;
    check.update(60.0, T0);
    EXPECT_EQ(check.state(T0), State::Unsafe);
}

TEST(EvSideVoltageCheckTest, the_absolute_value_counts) {
    EvSideVoltageCheck check;
    check.update(-59.9, T0);
    EXPECT_EQ(check.state(T0), State::Safe);
    check.update(-200.0, T0 + 1s);
    EXPECT_EQ(check.state(T0 + 1s), State::Unsafe);
}

TEST(EvSideVoltageCheckTest, voltage_dropping_turns_safe) {
    EvSideVoltageCheck check;
    check.update(400.0, T0);
    EXPECT_EQ(check.state(T0), State::Unsafe);
    check.update(12.0, T0 + 1s);
    EXPECT_EQ(check.state(T0 + 1s), State::Safe);
}

TEST(EvSideVoltageCheckTest, sample_is_used_until_max_sample_age) {
    EvSideVoltageCheck check;
    check.update(0.0, T0);
    EXPECT_EQ(check.state(T0 + 3000ms), State::Safe);
    EXPECT_EQ(check.state(T0 + 3001ms), State::Stale);
}

TEST(EvSideVoltageCheckTest, stale_source_recovers_with_the_next_sample) {
    EvSideVoltageCheck check;
    check.update(0.0, T0);
    ASSERT_EQ(check.state(T0 + 10s), State::Stale);
    check.update(0.0, T0 + 10s);
    EXPECT_EQ(check.state(T0 + 10s), State::Safe);
}

TEST(EvSideVoltageCheckTest, max_sample_age_is_configurable) {
    EvSideVoltageCheck::Timing timing;
    timing.max_sample_age = 100ms;
    EvSideVoltageCheck check{timing};
    check.update(0.0, T0);
    EXPECT_EQ(check.state(T0 + 101ms), State::Stale);
}

TEST(EvSideVoltageCheckTest, single_reverse_sample_is_no_reverse_voltage) {
    EvSideVoltageCheck check;
    check.update(-100.0, T0);
    EXPECT_FALSE(check.reverse_voltage());
}

TEST(EvSideVoltageCheckTest, reverse_voltage_after_400ms_of_reverse_samples) {
    EvSideVoltageCheck check;
    check.update(-100.0, T0);
    check.update(-100.0, T0 + 399ms);
    EXPECT_FALSE(check.reverse_voltage());
    check.update(-100.0, T0 + 400ms);
    EXPECT_TRUE(check.reverse_voltage());
}

TEST(EvSideVoltageCheckTest, minus_60V_is_no_reverse_voltage) {
    EvSideVoltageCheck check;
    check.update(-60.0, T0);
    check.update(-60.0, T0 + 1s);
    EXPECT_FALSE(check.reverse_voltage());
}

TEST(EvSideVoltageCheckTest, sample_above_the_limit_restarts_the_reverse_voltage_time) {
    EvSideVoltageCheck check;
    check.update(-100.0, T0);
    check.update(0.0, T0 + 300ms);
    check.update(-100.0, T0 + 350ms);
    check.update(-100.0, T0 + 700ms);
    EXPECT_FALSE(check.reverse_voltage());
    check.update(-100.0, T0 + 750ms);
    EXPECT_TRUE(check.reverse_voltage());
}

TEST(EvSideVoltageCheckTest, restart_forgets_earlier_reverse_samples) {
    EvSideVoltageCheck check;
    check.update(-100.0, T0);
    check.update(-100.0, T0 + 1s);
    ASSERT_TRUE(check.reverse_voltage());

    check.restart_reverse_voltage();
    EXPECT_FALSE(check.reverse_voltage());
    check.update(-100.0, T0 + 2s);
    EXPECT_FALSE(check.reverse_voltage());
    check.update(-100.0, T0 + 2400ms);
    EXPECT_TRUE(check.reverse_voltage());
}

TEST(EvSideVoltageCheckTest, positive_voltage_is_no_reverse_voltage) {
    EvSideVoltageCheck check;
    check.update(400.0, T0);
    check.update(400.0, T0 + 1s);
    EXPECT_FALSE(check.reverse_voltage());
}

} // namespace
