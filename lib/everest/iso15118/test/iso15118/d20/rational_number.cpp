// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <cstdint>
#include <limits>

#include <iso15118/message/common_types.hpp>

namespace dt = iso15118::message_20::datatypes;

SCENARIO("RationalNumber conversion to float") {
    GIVEN("values within the float range") {
        THEN("they convert exactly") {
            REQUIRE(dt::from_RationalNumber({4000, -1}) == Catch::Approx(400.0f));
            REQUIRE(dt::from_RationalNumber({-125, 2}) == Catch::Approx(-12500.0f));
            REQUIRE(dt::from_RationalNumber({0, 127}) == 0.0f);
        }
    }

    GIVEN("an exponent beyond the float range") {
        const auto positive = dt::from_RationalNumber({std::numeric_limits<int16_t>::max(), 127});
        const auto negative = dt::from_RationalNumber({std::numeric_limits<int16_t>::min(), 127});
        const auto tiny = dt::from_RationalNumber({std::numeric_limits<int16_t>::max(), -128});

        THEN("the result is clamped to a finite float") {
            REQUIRE(std::isfinite(positive));
            REQUIRE(positive == std::numeric_limits<float>::max());
            REQUIRE(std::isfinite(negative));
            REQUIRE(negative == -std::numeric_limits<float>::max());
            REQUIRE(std::isfinite(tiny));
            REQUIRE(tiny == Catch::Approx(0.0f));
        }
    }

    GIVEN("the smallest exponent that overflows a float") {
        THEN("the result is still finite") {
            REQUIRE(std::isfinite(dt::from_RationalNumber({1, 39})));
            REQUIRE(std::isfinite(dt::from_RationalNumber({-1, 39})));
        }
    }
}
