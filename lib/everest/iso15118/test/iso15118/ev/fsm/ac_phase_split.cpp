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

            THEN("everything goes on L1 and the peers are emitted as zero so L1 is not read as a sum") {
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

// from_float keeps four significant digits, so a per-line value can lose up to one unit in the
// fourth digit on the wire. Truncation is toward zero, so a limit is never overstated.
SCENARIO("ISO15118-20 EV emits a non-divisible AC total without overstating it") {
    GIVEN("A three-line EV advertising 11000 W on a ThreePhase connector") {
        dt::RationalNumber base{};
        std::optional<dt::RationalNumber> l2;
        std::optional<dt::RationalNumber> l3;

        WHEN("the total is emitted per line") {
            ev::emit_ac_limit(11000.0f, 3, dt::AcConnector::ThreePhase, base, l2, l3);

            THEN("each line decodes to 3666 W") {
                REQUIRE(l2.has_value());
                REQUIRE(l3.has_value());
                REQUIRE(dt::from_RationalNumber(base) == 3666.0f);
                REQUIRE(dt::from_RationalNumber(*l2) == 3666.0f);
                REQUIRE(dt::from_RationalNumber(*l3) == 3666.0f);
            }

            THEN("the decoded lines sum to at most the advertised total") {
                const auto sum =
                    dt::from_RationalNumber(base) + dt::from_RationalNumber(*l2) + dt::from_RationalNumber(*l3);
                REQUIRE(sum <= 11000.0f);
            }
        }
    }
}

SCENARIO("ISO15118-20 EV emits a present measurement as one aggregate unless the connector is ThreePhase") {
    dt::RationalNumber base{};
    std::optional<dt::RationalNumber> l2 = dt::from_float(1.0f);
    std::optional<dt::RationalNumber> l3 = dt::from_float(1.0f);

    GIVEN("A three-line EV measuring 5100 W") {
        WHEN("the connector is SinglePhase") {
            ev::emit_ac_present(5100.0f, 3, dt::AcConnector::SinglePhase, base, l2, l3);

            THEN("the whole reading is on the base element and the peers are cleared") {
                REQUIRE(dt::from_RationalNumber(base) == 5100.0f);
                REQUIRE_FALSE(l2.has_value());
                REQUIRE_FALSE(l3.has_value());
            }
        }

        WHEN("the connector is ThreePhase") {
            ev::emit_ac_present(5100.0f, 3, dt::AcConnector::ThreePhase, base, l2, l3);

            THEN("the reading is divided per line like a limit") {
                REQUIRE(dt::from_RationalNumber(base) == 1700.0f);
                REQUIRE(dt::from_RationalNumber(*l2) == 1700.0f);
                REQUIRE(dt::from_RationalNumber(*l3) == 1700.0f);
            }
        }
    }
}
