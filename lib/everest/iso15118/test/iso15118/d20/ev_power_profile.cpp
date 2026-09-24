// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#include <catch2/catch_test_macros.hpp>

#include <optional>
#include <vector>

#include <iso15118/d20/ev_power_profile.hpp>

namespace dt = iso15118::message_20::datatypes;

namespace {
dt::PowerScheduleEntry entry(uint32_t duration, int16_t watts) {
    return {duration, dt::RationalNumber{watts, 0}, std::nullopt, std::nullopt};
}
} // namespace

SCENARIO("The applied EVPowerProfileEntry decides whether a scheduled pause may be notified [V2G20-1198]") {
    const std::vector<dt::PowerScheduleEntry> entries{entry(60, 11000), entry(30, 0), entry(60, 5000)};
    const auto profile = iso15118::d20::EvPowerProfile::from(1000, entries);

    THEN("only the 0 kW entry allows it") {
        REQUIRE_FALSE(profile.zero_power_at(1000));
        REQUIRE_FALSE(profile.zero_power_at(1059));
        REQUIRE(profile.zero_power_at(1060));
        REQUIRE(profile.zero_power_at(1089));
        REQUIRE_FALSE(profile.zero_power_at(1090));
    }

    THEN("no entry is applied before the anchor or after the last entry") {
        REQUIRE_FALSE(profile.zero_power_at(999));
        REQUIRE_FALSE(profile.zero_power_at(1150));
    }

    GIVEN("a three phase entry with power on another phase") {
        dt::PowerScheduleEntry three_phase = entry(60, 0);
        three_phase.power_l2 = dt::RationalNumber{3000, 0};
        const std::vector<dt::PowerScheduleEntry> phases{three_phase};
        THEN("it is not 0 kW") {
            REQUIRE_FALSE(iso15118::d20::EvPowerProfile::from(1000, phases).zero_power_at(1000));
        }
    }
}
