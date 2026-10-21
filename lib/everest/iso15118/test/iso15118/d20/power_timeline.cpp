// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#include <catch2/catch_test_macros.hpp>

#include <array>
#include <optional>
#include <vector>

#include <iso15118/d20/power_timeline.hpp>

namespace dt = iso15118::message_20::datatypes;

namespace {

dt::PowerScheduleEntry entry(uint32_t duration, int16_t watts) {
    return {duration, dt::RationalNumber{watts, 0}, std::nullopt, std::nullopt};
}

dt::PowerScheduleEntry per_phase(uint32_t duration, int16_t l1, int16_t l2, int16_t l3) {
    return {duration, dt::RationalNumber{l1, 0}, dt::RationalNumber{l2, 0}, dt::RationalNumber{l3, 0}};
}

iso15118::d20::PowerTimeline::Entry total(float watts) {
    return {0, watts, std::nullopt};
}

iso15118::d20::PowerTimeline::Entry phases(float l1, float l2, float l3) {
    return {0, l1 + l2 + l3, std::array<float, 3>{l1, l2, l3}};
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
        REQUIRE_FALSE(offered.exceeded_by(1000, 4600, total(11000.0f)));
        REQUIRE_FALSE(offered.exceeded_by(4600, 8200, total(0.0f)));
    }

    THEN("a demand above any overlapped slot exceeds it") {
        REQUIRE(offered.exceeded_by(1000, 4600, total(11001.0f)));
        REQUIRE(offered.exceeded_by(4599, 4601, total(1.0f)));
        REQUIRE(offered.exceeded_by(1000, 8200, total(11000.0f)));
    }

    THEN("time the timeline does not cover allows zero power [V2G20-1811]") {
        REQUIRE(offered.exceeded_by(0, 1000, total(1.0f)));
        REQUIRE(offered.exceeded_by(8200, 9000, total(1.0f)));
        REQUIRE(offered.exceeded_by(999, 1001, total(1.0f)));
        REQUIRE_FALSE(offered.exceeded_by(8199, 8201, total(0.0f)));
        REQUIRE_FALSE(offered.exceeded_by(0, 1000, total(0.0f)));
        REQUIRE_FALSE(offered.exceeded_by(8200, 9000, total(0.0f)));
    }

    THEN("an entry without duration demands nothing") {
        REQUIRE_FALSE(offered.exceeded_by(9000, 9000, total(22000.0f)));
    }
}

SCENARIO("A power timeline compares per phase once either side is per phase") {
    using iso15118::d20::PowerTimeline;

    GIVEN("an offer of 7 kW on each phase") {
        const std::vector<dt::PowerScheduleEntry> entries{per_phase(3600, 7000, 7000, 7000)};
        const auto offered = PowerTimeline::from(1000, entries);

        THEN("7 kW on each phase or a 21 kW total fits") {
            REQUIRE_FALSE(offered.exceeded_by(1000, 4600, phases(7000.0f, 7000.0f, 7000.0f)));
            REQUIRE_FALSE(offered.exceeded_by(1000, 4600, total(21000.0f)));
        }

        THEN("a phase above 7 kW exceeds it although the total fits") {
            REQUIRE(offered.exceeded_by(1000, 4600, phases(15000.0f, 3000.0f, 3000.0f)));
        }
    }

    GIVEN("an offer of 21 kW as a total") {
        const std::vector<dt::PowerScheduleEntry> entries{entry(3600, 21000)};
        const auto offered = PowerTimeline::from(1000, entries);

        THEN("it counts as 7 kW on each phase") {
            REQUIRE_FALSE(offered.exceeded_by(1000, 4600, phases(7000.0f, 7000.0f, 7000.0f)));
            REQUIRE(offered.exceeded_by(1000, 4600, phases(15000.0f, 3000.0f, 3000.0f)));
        }
    }
}
