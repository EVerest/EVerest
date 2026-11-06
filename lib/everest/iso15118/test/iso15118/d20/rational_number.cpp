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

namespace {
struct Expected {
    double decimal;
    int16_t value;
    int8_t exponent;
};

void require_rational(float in, const Expected& expected) {
    const auto out = dt::from_float(in);
    CAPTURE(in, out.value, static_cast<int>(out.exponent));
    REQUIRE(out.value == expected.value);
    REQUIRE(out.exponent == expected.exponent);
    const auto exact = static_cast<double>(out.value) * std::pow(10.0, out.exponent);
    REQUIRE(exact == Catch::Approx(expected.decimal).epsilon(1e-9));
    REQUIRE(dt::from_RationalNumber(out) == Catch::Approx(static_cast<float>(expected.decimal)));
}
} // namespace

SCENARIO("RationalNumber conversion from float") {
    GIVEN("decimals whose float lies just below the decimal value") {
        THEN("they round to the decimal value instead of truncating") {
            require_rational(0.95f, {0.95, 9500, -4});
            require_rational(0.16f, {0.16, 1600, -4});
            require_rational(0.036f, {0.036, 3600, -5});
            require_rational(60.1f, {60.1, 6010, -2});
            require_rational(0.9f, {0.9, 9000, -4});
            require_rational(211.2f, {211.2, 2112, -1});
        }
    }

    GIVEN("negative values") {
        THEN("they round symmetrically") {
            require_rational(-0.95f, {-0.95, -9500, -4});
            require_rational(-60.1f, {-60.1, -6010, -2});
        }
    }

    GIVEN("a value that rounds up across a digit boundary") {
        THEN("it keeps four digits with the exponent one higher") {
            require_rational(9.99996f, {10.0, 1000, -2});
            require_rational(-9.99996f, {-10.0, -1000, -2});
            require_rational(9999.5f, {10000.0, 1000, 1});
            require_rational(-9999.5f, {-10000.0, -1000, 1});
        }
    }

    GIVEN("the largest digits rounding can reach") {
        THEN("they stay four digits") {
            require_rational(9999.0f, {9999.0, 9999, 0});
            require_rational(-9999.0f, {-9999.0, -9999, 0});
            require_rational(1000.0f, {1000.0, 1000, 0});
        }
    }

    GIVEN("zero") {
        const auto out = dt::from_float(0.0f);
        THEN("it is zero with exponent zero") {
            REQUIRE(out.value == 0);
            REQUIRE(out.exponent == 0);
        }
    }

    GIVEN("very small and very large magnitudes") {
        THEN("they keep four digits") {
            require_rational(1e-30f, {1e-30, 1000, -33});
            require_rational(1e30f, {1e30, 1000, 27});
            const auto denormal = dt::from_float(std::numeric_limits<float>::denorm_min());
            REQUIRE(denormal.value == 1401);
            REQUIRE(denormal.exponent == -48);
            const auto max = dt::from_float(std::numeric_limits<float>::max());
            REQUIRE(max.value == 3403);
            REQUIRE(max.exponent == 35);
        }
    }
}
