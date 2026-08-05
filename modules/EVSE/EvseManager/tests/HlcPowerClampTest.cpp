// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include <gtest/gtest.h>

#include "../hlc_power_clamp.hpp"

namespace module {

// The clamp turns the EV's requested current into power using a voltage, and reduces the
// current when that power exceeds what the EVSE can deliver. Which voltage it uses is the
// whole question: an EV that reports a present voltage of zero used to disable the clamp for
// an entire DC session, because a reported zero is still a reported value.

TEST(HlcPowerClampTest, clamps_current_when_requested_power_exceeds_the_limit) {
    // 200 A at 400 V is 80 kW against a 50 kW limit, so the current comes down to 125 A.
    const auto result = clamp_hlc_power(200.0, 400.0, /*present_voltage=*/400.0, 50000.0);

    EXPECT_TRUE(result.limit_exceeded);
    EXPECT_NEAR(result.current, 125.0, 1e-9);
}

TEST(HlcPowerClampTest, leaves_current_alone_when_within_the_limit) {
    const auto result = clamp_hlc_power(100.0, 400.0, /*present_voltage=*/400.0, 50000.0);

    EXPECT_FALSE(result.limit_exceeded);
    EXPECT_NEAR(result.current, 100.0, 1e-9);
}

TEST(HlcPowerClampTest, uses_the_measured_voltage_in_preference_to_the_target) {
    // Measuring 500 V while targeting 400 V puts 200 A over the limit sooner, and the
    // clamp has to use what was measured.
    const auto result = clamp_hlc_power(200.0, 400.0, /*present_voltage=*/500.0, 50000.0);

    EXPECT_TRUE(result.limit_exceeded);
    EXPECT_NEAR(result.current, 100.0, 1e-9);
}

// The regression this whole helper exists for.
TEST(HlcPowerClampTest, still_clamps_when_the_ev_reports_zero_volts) {
    // A reported zero is not a measurement. Treating it as one makes target_power zero, so
    // the limit can never be exceeded and the clamp silently stops working.
    const auto result = clamp_hlc_power(200.0, 400.0, /*present_voltage=*/0.0, 50000.0);

    EXPECT_TRUE(result.limit_exceeded);
    EXPECT_NEAR(result.current, 125.0, 1e-9);
}

TEST(HlcPowerClampTest, still_clamps_when_the_ev_reports_no_voltage_at_all) {
    const auto result = clamp_hlc_power(200.0, 400.0, /*present_voltage=*/std::nullopt, 50000.0);

    EXPECT_TRUE(result.limit_exceeded);
    EXPECT_NEAR(result.current, 125.0, 1e-9);
}

TEST(HlcPowerClampTest, does_not_divide_by_zero_when_no_voltage_is_known) {
    // Neither a measurement nor a target. There is no power to compute, so the request has
    // to pass through untouched rather than dividing by zero.
    const auto result = clamp_hlc_power(200.0, 0.0, /*present_voltage=*/0.0, 50000.0);

    EXPECT_FALSE(result.limit_exceeded);
    EXPECT_NEAR(result.current, 200.0, 1e-9);
}

TEST(HlcPowerClampTest, treats_a_negative_reported_voltage_as_absent) {
    const auto result = clamp_hlc_power(200.0, 400.0, /*present_voltage=*/-12.0, 50000.0);

    EXPECT_TRUE(result.limit_exceeded);
    EXPECT_NEAR(result.current, 125.0, 1e-9);
}

} // namespace module
