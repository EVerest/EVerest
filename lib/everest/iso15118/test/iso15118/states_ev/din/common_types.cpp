// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Pionix GmbH and Contributors to EVerest
#include <catch2/catch_test_macros.hpp>

#include <iso15118/message_din/common_types.hpp>

#include <cbv2g/din/din_msgDefDatatypes.h>

using namespace iso15118::message_din;
namespace dt = iso15118::message_din::datatypes;

SCENARIO("DIN to_physical_value encoding") {
    GIVEN("A whole-unit value") {
        const auto pv = to_physical_value(400.0, dt::Unit::V);
        THEN("It is encoded with multiplier 0 and the unit flagged used") {
            REQUIRE(pv.Value == 400);
            REQUIRE(pv.Multiplier == 0);
            REQUIRE(pv.Unit_isUsed == 1);
            REQUIRE(pv.Unit == static_cast<din_unitSymbolType>(dt::Unit::V));
            REQUIRE(from_physical_value(pv) == 400.0);
        }
    }

    GIVEN("A sub-unit fractional value (0.5)") {
        const auto pv = to_physical_value(0.5, dt::Unit::A);
        THEN("A negative multiplier retains precision") {
            REQUIRE(pv.Multiplier == -1);
            REQUIRE(pv.Value == 5);
            REQUIRE(from_physical_value(pv) == 0.5);
        }
    }

    GIVEN("A fractional value needing two decimals (0.25)") {
        const auto pv = to_physical_value(0.25, dt::Unit::A);
        THEN("It round-trips exactly") {
            REQUIRE(pv.Multiplier == -2);
            REQUIRE(pv.Value == 25);
            REQUIRE(from_physical_value(pv) == 0.25);
        }
    }

    GIVEN("A negative fractional value (-0.5)") {
        const auto pv = to_physical_value(-0.5, dt::Unit::A);
        THEN("The sign is preserved and it round-trips") {
            REQUIRE(pv.Value == -5);
            REQUIRE(pv.Multiplier == -1);
            REQUIRE(from_physical_value(pv) == -0.5);
        }
    }

    GIVEN("A large value beyond int16 range (100000)") {
        const auto pv = to_physical_value(100000.0, dt::Unit::W);
        THEN("A positive multiplier keeps the mantissa in range") {
            REQUIRE(pv.Multiplier >= -3);
            REQUIRE(pv.Multiplier <= 3);
            REQUIRE(from_physical_value(pv) == 100000.0);
        }
    }

    GIVEN("A value too large even at multiplier +3 (1e9)") {
        const auto pv = to_physical_value(1.0e9, dt::Unit::W);
        THEN("The mantissa is clamped to int16 range and the multiplier stays within spec") {
            REQUIRE(pv.Value == 32767);
            REQUIRE(pv.Multiplier == 3);
        }
    }

    GIVEN("A large negative value (-1e9)") {
        const auto pv = to_physical_value(-1.0e9, dt::Unit::W);
        THEN("The mantissa is clamped to the int16 minimum") {
            REQUIRE(pv.Value == -32768);
            REQUIRE(pv.Multiplier == 3);
        }
    }

    GIVEN("A zero value") {
        const auto pv = to_physical_value(0.0, dt::Unit::V);
        THEN("It encodes as zero with multiplier 0") {
            REQUIRE(pv.Value == 0);
            REQUIRE(pv.Multiplier == 0);
            REQUIRE(from_physical_value(pv) == 0.0);
        }
    }
}
