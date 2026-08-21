// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#include <catch2/catch_test_macros.hpp>

#include <iso15118/ev/ac_phase_split.hpp>

using namespace iso15118;
namespace dt = message_20::datatypes;

// ISO 15118-20 conditions the meaning of the base element on the selected AC connector:
// under SinglePhase only the base element may be used, under ThreePhase the base element is
// the sum across all three lines when no _L2/_L3 peer is present, and the L1 value when one is.
SCENARIO("ISO15118-20 EV splits an advertised AC total across the selected connector") {

    GIVEN("A three-phase EV advertising an 11040 W three-phase total") {
        constexpr float total = 11040.0f;
        constexpr uint8_t phase_count = 3;

        WHEN("the selected parameter set is ThreePhase") {
            const auto split = ev::split_ac_limit(total, phase_count, dt::AcConnector::ThreePhase);

            THEN("each line carries a third of the total, so the base element means L1") {
                REQUIRE(split.base == 3680.0f);
                REQUIRE(split.l2.has_value());
                REQUIRE(split.l3.has_value());
                REQUIRE(*split.l2 == 3680.0f);
                REQUIRE(*split.l3 == 3680.0f);
            }

            THEN("the three lines sum back to the advertised total") {
                REQUIRE(split.base + *split.l2 + *split.l3 == total);
            }
        }

        WHEN("the selected parameter set is SinglePhase") {
            const auto split = ev::split_ac_limit(total, phase_count, dt::AcConnector::SinglePhase);

            THEN("only the base element is emitted, carrying what one phase can draw") {
                REQUIRE(split.base == 3680.0f);
                REQUIRE_FALSE(split.l2.has_value());
                REQUIRE_FALSE(split.l3.has_value());
            }

            THEN("it does not advertise the three-phase total on a single phase") {
                REQUIRE(split.base != total);
            }
        }
    }

    GIVEN("A single-phase EV advertising a 3680 W total") {
        constexpr float total = 3680.0f;
        constexpr uint8_t phase_count = 1;

        WHEN("the selected parameter set is SinglePhase") {
            const auto split = ev::split_ac_limit(total, phase_count, dt::AcConnector::SinglePhase);

            THEN("the base element carries the whole total and no peers are emitted") {
                REQUIRE(split.base == total);
                REQUIRE_FALSE(split.l2.has_value());
                REQUIRE_FALSE(split.l3.has_value());
            }
        }

        WHEN("the selected parameter set is ThreePhase") {
            const auto split = ev::split_ac_limit(total, phase_count, dt::AcConnector::ThreePhase);

            THEN("the peers are emitted as zero so the base element is read as L1 alone") {
                REQUIRE(split.base == total);
                REQUIRE(split.l2.has_value());
                REQUIRE(split.l3.has_value());
                REQUIRE(*split.l2 == 0.0f);
                REQUIRE(*split.l3 == 0.0f);
            }

            THEN("the lines sum to the total rather than tripling it") {
                REQUIRE(split.base + *split.l2 + *split.l3 == total);
            }
        }
    }

    GIVEN("A two-phase EV advertising a 7360 W total") {
        // Two-phase EVs are real hardware, and phase_count is the EV's own line count rather
        // than a property of the connector, so the total has to reach the lines the EV can
        // actually draw on.
        constexpr float total = 7360.0f;
        constexpr uint8_t phase_count = 2;

        WHEN("the selected parameter set is SinglePhase") {
            const auto split = ev::split_ac_limit(total, phase_count, dt::AcConnector::SinglePhase);

            THEN("only the base element is emitted, carrying what one line can draw") {
                REQUIRE(split.base == 3680.0f);
                REQUIRE_FALSE(split.l2.has_value());
                REQUIRE_FALSE(split.l3.has_value());
            }
        }

        WHEN("the selected parameter set is ThreePhase") {
            const auto split = ev::split_ac_limit(total, phase_count, dt::AcConnector::ThreePhase);

            THEN("the two lines the EV draws on carry the total and the third is zero") {
                REQUIRE(split.base == 3680.0f);
                REQUIRE(split.l2.has_value());
                REQUIRE(split.l3.has_value());
                REQUIRE(*split.l2 == 3680.0f);
                REQUIRE(*split.l3 == 0.0f);
            }

            THEN("the lines sum back to the advertised total") {
                REQUIRE(split.base + *split.l2 + *split.l3 == total);
            }
        }
    }

    GIVEN("A line count above three, which no AC connector provides") {
        constexpr float total = 11040.0f;

        WHEN("the total is split across a ThreePhase connector") {
            const auto split = ev::split_ac_limit(total, 4, dt::AcConnector::ThreePhase);

            THEN("the count is clamped to three so the lines still sum to the total") {
                REQUIRE(split.base == 3680.0f);
                REQUIRE(*split.l2 == 3680.0f);
                REQUIRE(*split.l3 == 3680.0f);
                REQUIRE(split.base + *split.l2 + *split.l3 == total);
            }
        }
    }

    GIVEN("A zero limit") {
        WHEN("split across three phases") {
            const auto split = ev::split_ac_limit(0.0f, 3, dt::AcConnector::ThreePhase);

            THEN("every line is zero") {
                REQUIRE(split.base == 0.0f);
                REQUIRE(*split.l2 == 0.0f);
                REQUIRE(*split.l3 == 0.0f);
            }
        }
    }

    GIVEN("A phase count of zero, which no config should produce") {
        WHEN("splitting a total") {
            const auto split = ev::split_ac_limit(11040.0f, 0, dt::AcConnector::SinglePhase);

            THEN("the total passes through undivided rather than dividing by zero") {
                REQUIRE(split.base == 11040.0f);
                REQUIRE_FALSE(split.l2.has_value());
            }
        }
    }
}
