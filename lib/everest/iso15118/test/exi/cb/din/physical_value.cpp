// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <iso15118/message_din/common_types.hpp>

#include <cbv2g/din/din_msgDefDatatypes.h>

using namespace iso15118::message_din;
using Catch::Approx;
using datatypes::Unit;

// from_physical_value reads only Value and Multiplier, so a wrong Unit survives any
// message round-trip. These cases are the only place the mapping is checked.
SCENARIO("DIN physical value unit mapping") {

    GIVEN("Every Unit the domain enum offers") {
        const std::pair<Unit, din_unitSymbolType> mapping[] = {
            {Unit::h, din_unitSymbolType_h},   {Unit::m, din_unitSymbolType_m},   {Unit::s, din_unitSymbolType_s},
            {Unit::A, din_unitSymbolType_A},   {Unit::Ah, din_unitSymbolType_Ah}, {Unit::V, din_unitSymbolType_V},
            {Unit::VA, din_unitSymbolType_VA}, {Unit::W, din_unitSymbolType_W},   {Unit::W_s, din_unitSymbolType_W_s},
            {Unit::Wh, din_unitSymbolType_Wh},
        };

        THEN("It reaches the wire as the matching cbv2g symbol") {
            for (const auto& [unit, expected] : mapping) {
                const auto out = to_physical_value(1.0, unit);
                REQUIRE(out.Unit_isUsed == 1);
                REQUIRE(out.Unit == expected);
            }
        }
    }
}

SCENARIO("DIN physical value scaling") {

    GIVEN("Values that need no scaling") {
        THEN("They are carried at multiplier zero") {
            for (const double value : {0.0, 42.0, 400.0, 32767.0}) {
                const auto out = to_physical_value(value, Unit::V);
                REQUIRE(out.Multiplier == 0);
                REQUIRE(from_physical_value(out) == Approx(value));
            }
        }
    }

    GIVEN("A fractional value") {
        THEN("It is scaled up to a negative multiplier to keep its precision") {
            const auto tenth = to_physical_value(0.1, Unit::A);
            REQUIRE(tenth.Value == 1);
            REQUIRE(tenth.Multiplier == -1);
            REQUIRE(from_physical_value(tenth) == Approx(0.1));

            const auto milli = to_physical_value(0.001, Unit::A);
            REQUIRE(milli.Value == 1);
            REQUIRE(milli.Multiplier == -3);
            REQUIRE(from_physical_value(milli) == Approx(0.001));
        }
    }

    GIVEN("A value larger than the int16 mantissa") {
        THEN("It is scaled down to a positive multiplier") {
            const auto out = to_physical_value(60000.0, Unit::W);
            REQUIRE(out.Value == 6000);
            REQUIRE(out.Multiplier == 1);
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
            REQUIRE(out.Multiplier == 3);
            REQUIRE(out.Value == 32767);
            REQUIRE(from_physical_value(out) == Approx(32767000.0));
        }
    }

    GIVEN("Assorted values across the representable range") {
        THEN("The multiplier stays inside the DIN [-3, 3] constraint") {
            for (const double value : {0.0, 0.001, 0.1, 2.5, -2.5, 42.0, -400.5, 60000.0, 1.0e9, -1.0e9}) {
                const auto out = to_physical_value(value, Unit::V);
                REQUIRE(out.Multiplier >= -3);
                REQUIRE(out.Multiplier <= 3);
            }
        }
    }

    GIVEN("A value whose Unit_isUsed was never set") {
        THEN("from_physical_value still reads it") {
            din_PhysicalValueType in;
            init_din_PhysicalValueType(&in);
            in.Value = 25;
            in.Multiplier = -1;
            in.Unit_isUsed = 0;

            REQUIRE(from_physical_value(in) == Approx(2.5));
        }
    }
}
