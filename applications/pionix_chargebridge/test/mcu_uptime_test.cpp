// SPDX-License-Identifier: Apache-2.0
// Copyright 2020 - 2026 Pionix GmbH and Contributors to EVerest

// Reboot detection from the MCU's uptime counter. A false positive here drops the tap carrier (the
// reboot path synthesizes an all-zero link status), so the duplicate and wrap cases matter.

#include <charge_bridge/utilities/mcu_uptime.hpp>
#include <gtest/gtest.h>
#include <limits>

namespace {

using charge_bridge::utilities::mcu_rebooted;
using charge_bridge::utilities::mcu_uptime_wrap_margin_ms;

constexpr std::uint32_t k_max = std::numeric_limits<std::uint32_t>::max();

TEST(mcu_uptime, ordinary_progress_is_not_a_reboot) {
    EXPECT_FALSE(mcu_rebooted(0, 1));
    EXPECT_FALSE(mcu_rebooted(1000, 2000));
    EXPECT_FALSE(mcu_rebooted(3600000, 3601000));
    EXPECT_FALSE(mcu_rebooted(k_max - 1, k_max));
}

TEST(mcu_uptime, the_first_reply_is_not_a_reboot) {
    // The remembered value starts at zero, so the very first observed reply must not count - not even
    // the one from a device that reports an uptime of zero.
    EXPECT_FALSE(mcu_rebooted(0, 0));
    EXPECT_FALSE(mcu_rebooted(0, 500));
}

TEST(mcu_uptime, a_duplicated_reply_is_not_a_reboot) {
    // A duplicated UDP datagram repeats the identical uptime. The old `current <= previous` test
    // counted this as a reset, which now costs a carrier flap plus ~1 s of DAD mid-session.
    EXPECT_FALSE(mcu_rebooted(1, 1));
    EXPECT_FALSE(mcu_rebooted(3600000, 3600000));
    EXPECT_FALSE(mcu_rebooted(k_max, k_max));
}

TEST(mcu_uptime, a_large_gap_without_a_decrease_is_not_a_reboot) {
    // The ChargeBridge can be unreachable for days and come back without having rebooted.
    constexpr std::uint32_t two_days = 2u * 24u * 60u * 60u * 1000u;
    EXPECT_FALSE(mcu_rebooted(1000, 1000 + two_days));
}

TEST(mcu_uptime, a_restart_is_detected) {
    EXPECT_TRUE(mcu_rebooted(3600000, 500));
    EXPECT_TRUE(mcu_rebooted(2, 1));
    EXPECT_TRUE(mcu_rebooted(1, 0));
    // A device up for weeks that restarts: still far enough from the wrap point to be unambiguous.
    constexpr std::uint32_t three_weeks = 21u * 24u * 60u * 60u * 1000u;
    EXPECT_TRUE(mcu_rebooted(three_weeks, 300));
}

TEST(mcu_uptime, the_counter_wrapping_is_not_a_reboot) {
    // ~49.7 days of uptime: the uint32 view of uptime_ms rolls over to zero. The session is healthy
    // and the carrier must not flap.
    EXPECT_FALSE(mcu_rebooted(k_max, 0));
    EXPECT_FALSE(mcu_rebooted(k_max - 500, 500));
    EXPECT_FALSE(mcu_rebooted(k_max - mcu_uptime_wrap_margin_ms + 1, mcu_uptime_wrap_margin_ms - 1));
}

TEST(mcu_uptime, a_decrease_outside_the_wrap_window_is_a_reboot) {
    // Both halves of the wrap test have to hold. A previous value below the window, or a new value
    // above it, cannot be explained by a wrap.
    EXPECT_TRUE(mcu_rebooted(k_max - mcu_uptime_wrap_margin_ms, 100));
    EXPECT_TRUE(mcu_rebooted(k_max, mcu_uptime_wrap_margin_ms));
    EXPECT_TRUE(mcu_rebooted(k_max, mcu_uptime_wrap_margin_ms + 1));
}

TEST(mcu_uptime, a_wrap_is_only_credited_across_the_boundary) {
    // A decrease that stays high on both sides is a reboot of a long-running device, not a wrap: an
    // MCU cannot come back up reporting weeks of uptime.
    EXPECT_TRUE(mcu_rebooted(k_max - 100, k_max - 1000));
}

} // namespace
