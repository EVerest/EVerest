// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#include <catch2/catch_test_macros.hpp>

#include <iso15118/ev/ac_phase_split.hpp>

using namespace iso15118;
namespace dt = message_20::datatypes;

// ISO 15118-20 conditions the meaning of the base element on the selected AC connector:
// under SinglePhase only the base element may be used, under ThreePhase the base element is
// the sum across all three lines when no _L2/_L3 peer is present, and the L1 value when one is.
SCENARIO("ISO15118-20 EV states an advertised AC total for the selected connector") {

    GIVEN("A three-phase EV advertising an 11040 W three-phase total") {
        constexpr float total = 11040.0f;
        constexpr uint8_t phase_count = 3;

        WHEN("the selected parameter set is ThreePhase") {
            const auto split = ev::split_ac_limit(total, phase_count, dt::AcConnector::ThreePhase);

            // [V2G20-1820]: a symmetric EV states the sum, and [V2G20-1817] has the SECC read a
            // lone base element as that sum, evenly distributed.
            THEN("the base element carries the whole total and no line elements are emitted") {
                REQUIRE(split.base == total);
                REQUIRE_FALSE(split.l2.has_value());
                REQUIRE_FALSE(split.l3.has_value());
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

            THEN("the count folds to three, so the base element states the total") {
                REQUIRE(split.base == total);
                REQUIRE_FALSE(split.l2.has_value());
                REQUIRE_FALSE(split.l3.has_value());
            }
        }
    }

    GIVEN("A zero limit") {
        WHEN("split across three phases") {
            const auto split = ev::split_ac_limit(0.0f, 3, dt::AcConnector::ThreePhase);

            THEN("the base element is zero") {
                REQUIRE(split.base == 0.0f);
                REQUIRE_FALSE(split.l2.has_value());
                REQUIRE_FALSE(split.l3.has_value());
            }
        }
    }

    // The domain is 1 or 3. A 2 cannot come from config, which validates the count and pins the
    // manifest enum, but it must not land on the single-line reading and overstate L1.
    GIVEN("A line count of two, which no config should produce") {
        WHEN("the total is split across a ThreePhase connector") {
            const auto split = ev::split_ac_limit(11040.0f, 2, dt::AcConnector::ThreePhase);

            THEN("it folds up to three lines rather than stating the total on L1") {
                REQUIRE(split.base == 11040.0f);
                REQUIRE_FALSE(split.l2.has_value());
                REQUIRE_FALSE(split.l3.has_value());
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

// from_float keeps four significant digits, so a value can lose up to one unit in the fourth
// digit on the wire. Truncation is toward zero, so a limit is never overstated.
SCENARIO("ISO15118-20 EV emits a non-divisible AC total without overstating it") {
    GIVEN("A single-line EV advertising 11000 W on a SinglePhase connector") {
        dt::RationalNumber base{};
        std::optional<dt::RationalNumber> l2;
        std::optional<dt::RationalNumber> l3;

        WHEN("a three-line EV is limited to one line") {
            ev::emit_ac_limit(11000.0f, 3, dt::AcConnector::SinglePhase, base, l2, l3);

            THEN("the one line decodes to 3666 W, at most a third of the total") {
                REQUIRE(dt::from_RationalNumber(base) == 3666.0f);
                REQUIRE(dt::from_RationalNumber(base) * 3.0f <= 11000.0f);
                REQUIRE_FALSE(l2.has_value());
                REQUIRE_FALSE(l3.has_value());
            }
        }
    }
}

// [V2G20-1818]: on ThreePhase, L2 and L3 make the base read as L1 rather than as a sum.
SCENARIO("ISO15118-20 EV emits a ratio undivided on every line of the connector") {
    dt::RationalNumber base{};
    std::optional<dt::RationalNumber> l2 = dt::from_float(1.0f);
    std::optional<dt::RationalNumber> l3;
    const auto same = [](const dt::RationalNumber& a, const dt::RationalNumber& b) {
        return a.value == b.value and a.exponent == b.exponent;
    };
    const auto require_every_line = [&] {
        REQUIRE(same(base, dt::from_float(0.9f)));
        REQUIRE(l2.has_value());
        REQUIRE(l3.has_value());
        REQUIRE(same(*l2, base));
        REQUIRE(same(*l3, base));
    };

    GIVEN("A power factor of 0.9") {
        WHEN("the connector is SinglePhase") {
            ev::emit_ac_ratio(0.9f, dt::AcConnector::SinglePhase, base, l2, l3);

            THEN("only the base element carries the undivided ratio") {
                REQUIRE(same(base, dt::from_float(0.9f)));
                REQUIRE_FALSE(l2.has_value());
                REQUIRE_FALSE(l3.has_value());
            }
        }

        WHEN("a three-line EV uses a ThreePhase connector") {
            ev::emit_ac_ratio(0.9f, dt::AcConnector::ThreePhase, base, l2, l3);

            THEN("the base, L2 and L3 all carry the ratio") {
                require_every_line();
            }
        }

        WHEN("a single-line EV uses a ThreePhase connector") {
            ev::emit_ac_ratio(0.9f, dt::AcConnector::ThreePhase, base, l2, l3);
            std::optional<dt::RationalNumber> limit_l2;
            std::optional<dt::RationalNumber> limit_l3;
            dt::RationalNumber limit_base{};
            ev::emit_ac_limit(0.9f, 1, dt::AcConnector::ThreePhase, limit_base, limit_l2, limit_l3);

            THEN("the lines carry the ratio as for three lines, not the zero peers of a limit") {
                require_every_line();
                REQUIRE(limit_l2.has_value());
                REQUIRE(dt::from_RationalNumber(*limit_l2) == 0.0f);
            }
        }
    }
}

SCENARIO("ISO15118-20 EV emits a present measurement as one aggregate reading") {
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

            THEN("the whole reading stays on the base element, as the sum of the three lines") {
                REQUIRE(dt::from_RationalNumber(base) == 5100.0f);
                REQUIRE_FALSE(l2.has_value());
                REQUIRE_FALSE(l3.has_value());
            }
        }
    }
}
