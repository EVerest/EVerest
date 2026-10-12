// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#include <catch2/catch_test_macros.hpp>

#include <optional>
#include <vector>

#include <iso15118/d20/power_timeline.hpp>

namespace dt = iso15118::message_20::datatypes;

namespace {

dt::PowerScheduleEntry entry(uint32_t duration, int16_t watts) {
    return {duration, dt::RationalNumber{watts, 0}, std::nullopt, std::nullopt};
}

} // namespace

SCENARIO("A power timeline tells which entry applies at an instant") {
    const std::vector<dt::PowerScheduleEntry> entries{entry(60, 11000), entry(30, 0), entry(60, 5000)};
    const auto timeline = iso15118::d20::PowerTimeline::from(1000, entries);

    THEN("each slot carries its entry's power") {
        REQUIRE(timeline.power_at(1000) == 11000.0f);
        REQUIRE(timeline.power_at(1059) == 11000.0f);
        REQUIRE(timeline.power_at(1060) == 0.0f);
        REQUIRE(timeline.power_at(1089) == 0.0f);
        REQUIRE(timeline.power_at(1090) == 5000.0f);
    }

    THEN("no entry is applied before the anchor or after the last entry") {
        REQUIRE_FALSE(timeline.power_at(999).has_value());
        REQUIRE_FALSE(timeline.power_at(1150).has_value());
    }

    GIVEN("a three phase entry with power on another phase") {
        dt::PowerScheduleEntry three_phase = entry(60, 0);
        three_phase.power_l2 = dt::RationalNumber{3000, 0};
        const std::vector<dt::PowerScheduleEntry> phases{three_phase};
        THEN("the phases add up, so it is not 0 kW") {
            REQUIRE(iso15118::d20::PowerTimeline::from(1000, phases).power_at(1000) == 3000.0f);
        }
    }
}
