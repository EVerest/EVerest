// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include <gtest/gtest.h>

#include <chrono>
#include <optional>
#include <vector>

#include "energy_grid/schedule_no_energy.hpp"

namespace module {

using std::chrono::minutes;
using std::chrono::seconds;

namespace {

const auto NOW = Everest::Date::from_rfc3339("2026-01-01T12:00:00.000Z");

types::energy::ScheduleResEntry entry(minutes from_now, std::optional<float> power_W) {
    types::energy::ScheduleResEntry e;
    e.timestamp = Everest::Date::to_rfc3339(NOW + from_now);
    if (power_W.has_value()) {
        e.limits_to_root.total_power_W = types::energy::NumberWithSource{power_W.value(), "test"};
    }
    return e;
}

} // namespace

TEST(ScheduleNoEnergy, empty_schedule_is_unknown) {
    EXPECT_EQ(schedule_no_energy_duration({}, NOW), std::nullopt);
}

TEST(ScheduleNoEnergy, single_entry_does_not_look_ahead) {
    EXPECT_EQ(schedule_no_energy_duration({entry(minutes(0), 0.0f)}, NOW), std::nullopt);
}

TEST(ScheduleNoEnergy, energy_now_is_no_gap) {
    EXPECT_EQ(schedule_no_energy_duration({entry(minutes(0), 5000.0f), entry(minutes(5), 0.0f)}, NOW), std::nullopt);
    EXPECT_EQ(schedule_no_energy_duration({entry(minutes(0), std::nullopt), entry(minutes(5), 0.0f)}, NOW),
              std::nullopt);
}

TEST(ScheduleNoEnergy, lasts_until_the_next_entry_with_energy) {
    const std::vector<types::energy::ScheduleResEntry> schedule{entry(minutes(-5), 0.0f), entry(minutes(5), 0.0f),
                                                                entry(minutes(10), 5000.0f), entry(minutes(15), 0.0f)};
    EXPECT_EQ(schedule_no_energy_duration(schedule, NOW), seconds(600));
}

TEST(ScheduleNoEnergy, the_entry_active_now_decides) {
    const std::vector<types::energy::ScheduleResEntry> schedule{entry(minutes(-10), 5000.0f), entry(minutes(-5), 0.0f),
                                                                entry(minutes(5), 5000.0f)};
    EXPECT_EQ(schedule_no_energy_duration(schedule, NOW), seconds(300));
}

TEST(ScheduleNoEnergy, first_entry_is_active_even_if_in_the_future) {
    const std::vector<types::energy::ScheduleResEntry> schedule{entry(minutes(1), 0.0f), entry(minutes(5), 5000.0f)};
    EXPECT_EQ(schedule_no_energy_duration(schedule, NOW), seconds(300));
}

TEST(ScheduleNoEnergy, no_energy_to_the_end_lasts_at_least_to_last_entry) {
    const std::vector<types::energy::ScheduleResEntry> schedule{entry(minutes(0), 0.0f), entry(minutes(5), 0.0f),
                                                                entry(minutes(10), 0.0f)};
    EXPECT_EQ(schedule_no_energy_duration(schedule, NOW), seconds(600));
}

TEST(ScheduleNoEnergy, a_zero_current_limit_is_no_energy_too) {
    auto zero_current = entry(minutes(0), std::nullopt);
    zero_current.limits_to_root.ac_max_current_A = types::energy::NumberWithSource{0.0f, "test"};
    EXPECT_EQ(schedule_no_energy_duration({zero_current, entry(minutes(5), 5000.0f)}, NOW), seconds(300));
}

} // namespace module
