// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include <gtest/gtest.h>

#include <everest/conversions/ocpp/ocpp_conversions.hpp>

namespace {

constexpr std::uint32_t NUMBER_OF_DECIMALS = 2;

struct CostCase {
    double cost;
    int expected_minor_units;
};

// Includes costs whose binary value lies just below the exact decimal, e.g. 0.29 * 100 = 28.999999999999996.
class ToMinorUnitsTest : public ::testing::TestWithParam<CostCase> {};

TEST_P(ToMinorUnitsTest, rounds_to_nearest_minor_unit) {
    EXPECT_EQ(ocpp_conversions::to_minor_units(GetParam().cost, NUMBER_OF_DECIMALS), GetParam().expected_minor_units);
}

INSTANTIATE_TEST_SUITE_P(OcppConversions, ToMinorUnitsTest,
                         ::testing::Values(CostCase{0.07, 7}, CostCase{0.29, 29}, CostCase{0.57, 57},
                                           CostCase{1.13, 113}, CostCase{50.0, 5000}));

} // namespace
