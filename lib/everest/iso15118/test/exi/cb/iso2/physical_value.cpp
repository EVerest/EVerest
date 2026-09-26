// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <iso15118/message_2/common_types.hpp>

#include <cbv2g/iso_2/iso2_msgDefDatatypes.h>

using namespace iso15118::message_2::datatypes;
using Catch::Approx;

// from_physical_value reads only value and multiplier, so a wrong unit survives any
// message round-trip. These cases are the only place the mapping is checked.
SCENARIO("ISO-2 physical value unit mapping") {

    GIVEN("Every Unit the domain enum offers") {
        const std::pair<Unit, iso2_unitSymbolType> mapping[] = {
            {Unit::h, iso2_unitSymbolType_h},   {Unit::m, iso2_unitSymbolType_m}, {Unit::s, iso2_unitSymbolType_s},
            {Unit::A, iso2_unitSymbolType_A},   {Unit::V, iso2_unitSymbolType_V}, {Unit::W, iso2_unitSymbolType_W},
            {Unit::Wh, iso2_unitSymbolType_Wh},
        };

        THEN("Its numeric value matches the cbv2g symbol it is cast to") {
            for (const auto& [unit, expected] : mapping) {
                REQUIRE(to_physical_value(1.0, unit).unit == unit);
                REQUIRE(static_cast<int>(unit) == static_cast<int>(expected));
            }
        }
    }
}

SCENARIO("ISO-2 physical value scaling") {

    GIVEN("Zero") {
        THEN("It is carried as a plain zero") {
            const auto out = to_physical_value(0.0, Unit::V);
            REQUIRE(out.value == 0);
            REQUIRE(out.multiplier == 0);
            REQUIRE(from_physical_value(out) == Approx(0.0));
        }
    }

    GIVEN("A fractional value") {
        THEN("It is scaled up to a negative multiplier to keep its precision") {
            const auto tenth = to_physical_value(0.1, Unit::A);
            REQUIRE(tenth.multiplier == -3);
            REQUIRE(from_physical_value(tenth) == Approx(0.1));

            const auto milli = to_physical_value(0.001, Unit::A);
            REQUIRE(milli.multiplier == -3);
            REQUIRE(from_physical_value(milli) == Approx(0.001));
        }
    }

    GIVEN("A value larger than the int16 mantissa") {
        THEN("It is scaled down to a positive multiplier") {
            const auto out = to_physical_value(60000.0, Unit::W);
            REQUIRE(out.value == 6000);
            REQUIRE(out.multiplier == 1);
            REQUIRE(from_physical_value(out) == Approx(60000.0));
        }
    }

    GIVEN("A negative fractional value") {
        THEN("Sign and precision both survive") {
            const auto out = to_physical_value(-400.5, Unit::V);
            REQUIRE(from_physical_value(out) == Approx(-400.5));
        }
    }

    GIVEN("A value too large to represent even at the maximum multiplier") {
        THEN("It saturates instead of wrapping") {
            const auto out = to_physical_value(1.0e9, Unit::W);
            REQUIRE(out.multiplier == 3);
            REQUIRE(out.value == 32767);
            REQUIRE(from_physical_value(out) == Approx(32767000.0));
        }
    }

    GIVEN("Assorted values across the representable range") {
        THEN("The multiplier stays inside the ISO 15118-2 [-3, 3] constraint") {
            for (const double value : {0.0, 0.001, 0.1, 2.5, -2.5, 42.0, -400.5, 60000.0, 1.0e9, -1.0e9}) {
                const auto out = to_physical_value(value, Unit::V);
                REQUIRE(out.multiplier >= -3);
                REQUIRE(out.multiplier <= 3);
            }
        }
    }
}
