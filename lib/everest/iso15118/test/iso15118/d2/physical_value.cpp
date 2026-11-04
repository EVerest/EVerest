// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#include <catch2/catch_test_macros.hpp>

#include <cstdint>

#include <iso15118/message_2/common_types.hpp>

namespace dt = iso15118::message_2::datatypes;

namespace {
void require_physical(double in, int16_t value, int8_t multiplier) {
    const auto out = dt::to_physical_value(in, dt::Unit::W);
    CAPTURE(in, out.value, static_cast<int>(out.multiplier));
    REQUIRE(out.value == value);
    REQUIRE(out.multiplier == multiplier);
}
} // namespace

SCENARIO("PhysicalValue conversion from double") {
    GIVEN("a value whose scaled double lies just below the integer") {
        THEN("it rounds instead of truncating") {
            require_physical(1.001, 1001, -3);
            require_physical(10.03, 1003, -2);
            require_physical(static_cast<double>(60.1f), 6010, -2);
            require_physical(static_cast<double>(0.95f), 950, -3);
        }
    }

    GIVEN("negative values") {
        THEN("they round symmetrically") {
            require_physical(-1.001, -1001, -3);
            require_physical(static_cast<double>(-60.1f), -6010, -2);
        }
    }

    GIVEN("zero") {
        THEN("it is zero with multiplier zero") {
            require_physical(0.0, 0, 0);
        }
    }

    GIVEN("a value too large for the multiplier range") {
        THEN("the value is clamped to int16") {
            require_physical(1e9, INT16_MAX, 3);
            require_physical(-1e9, INT16_MIN, 3);
        }
        THEN("a value that rounds past the int16 range is clamped after rounding") {
            // 32767.6 rounds to 32768 and -32768.6 to -32769, one past the int16 range, so the
            // clamp has to follow the rounding or the cast is out of range.
            require_physical(32767.6e3, INT16_MAX, 3);
            require_physical(-32768.6e3, INT16_MIN, 3);
        }
    }

    GIVEN("a value too small for the multiplier range") {
        THEN("it keeps the precision of multiplier -3") {
            require_physical(0.0004, 0, -3);
            require_physical(0.0006, 1, -3);
        }
    }
}
