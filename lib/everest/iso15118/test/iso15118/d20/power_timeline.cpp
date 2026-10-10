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

SCENARIO("A power timeline tells whether a demand exceeds it") {
    const std::vector<dt::PowerScheduleEntry> entries{entry(3600, 11000), entry(3600, 0)};
    const auto offered = iso15118::d20::PowerTimeline::from(1000, entries);

    THEN("a demand within every slot it overlaps is fine") {
        REQUIRE_FALSE(offered.exceeded_by(1000, 4600, 11000.0f));
        REQUIRE_FALSE(offered.exceeded_by(4600, 8200, 0.0f));
    }

    THEN("a demand above any overlapped slot exceeds it") {
        REQUIRE(offered.exceeded_by(1000, 4600, 11001.0f));
        REQUIRE(offered.exceeded_by(4599, 4601, 1.0f));
        REQUIRE(offered.exceeded_by(1000, 8200, 11000.0f));
    }

    THEN("time the timeline does not cover is unconstrained") {
        REQUIRE_FALSE(offered.exceeded_by(0, 1000, 22000.0f));
        REQUIRE_FALSE(offered.exceeded_by(8200, 9000, 22000.0f));
    }
}
