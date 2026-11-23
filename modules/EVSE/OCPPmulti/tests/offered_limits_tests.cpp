// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include <offered_limits.hpp>

#include <gtest/gtest.h>

namespace {

using ocpp_multi::to_offered_limits;

float supply_230V() {
    return 230.0F;
}

TEST(OfferedLimits, empty) {
    const auto offered = to_offered_limits({}, supply_230V);
    EXPECT_FALSE(offered.max_current_A.has_value());
    EXPECT_FALSE(offered.max_power_W.has_value());
}

TEST(OfferedLimits, powerDerivedFromCurrentLimit) {
    types::energy::LimitsRes limits;
    limits.ac_max_current_A = {16.5F, "source"};
    limits.ac_max_phase_count = {3, "source"};

    const auto offered = to_offered_limits(limits, supply_230V);
    ASSERT_TRUE(offered.max_current_A.has_value());
    ASSERT_TRUE(offered.max_power_W.has_value());
    EXPECT_FLOAT_EQ(offered.max_current_A.value(), 16.5F);
    EXPECT_FLOAT_EQ(offered.max_power_W.value(), 11385.0F);

    limits.ac_max_phase_count = {1, "source"};
    const auto single_phase = to_offered_limits(limits, [] { return 240.0F; });
    ASSERT_TRUE(single_phase.max_power_W.has_value());
    EXPECT_FLOAT_EQ(single_phase.max_power_W.value(), 3960.0F);
}

TEST(OfferedLimits, powerLimitIsNotDerived) {
    types::energy::LimitsRes limits;
    limits.ac_max_current_A = {32.0F, "source"};
    limits.ac_max_phase_count = {3, "source"};
    limits.total_power_W = {7400.0F, "source"};

    bool supply_voltage_read{false};
    const auto offered = to_offered_limits(limits, [&supply_voltage_read] {
        supply_voltage_read = true;
        return 230.0F;
    });
    ASSERT_TRUE(offered.max_power_W.has_value());
    EXPECT_FLOAT_EQ(offered.max_current_A.value_or(0.0F), 32.0F);
    EXPECT_FLOAT_EQ(offered.max_power_W.value(), 7400.0F);
    EXPECT_FALSE(supply_voltage_read);
}

TEST(OfferedLimits, powerNeedsPhaseCountToBeDerived) {
    types::energy::LimitsRes limits;
    limits.ac_max_current_A = {32.0F, "source"};

    const auto offered = to_offered_limits(limits, supply_230V);
    EXPECT_FLOAT_EQ(offered.max_current_A.value_or(0.0F), 32.0F);
    EXPECT_FALSE(offered.max_power_W.has_value());
}

TEST(OfferedLimits, powerOnlyLimit) {
    types::energy::LimitsRes limits;
    limits.total_power_W = {50000.0F, "source"};

    const auto offered = to_offered_limits(limits, supply_230V);
    EXPECT_FALSE(offered.max_current_A.has_value());
    EXPECT_FLOAT_EQ(offered.max_power_W.value_or(0.0F), 50000.0F);
}

TEST(OfferedLimitsCache, repeatedLimitsDoNotReadTheSupplyVoltageAgain) {
    types::energy::LimitsRes limits;
    limits.ac_max_current_A = {16.0F, "source"};
    limits.ac_max_phase_count = {3, "source"};

    int supply_voltage_reads{0};
    const auto supply_voltage_V = [&supply_voltage_reads] {
        ++supply_voltage_reads;
        return 230.0F;
    };

    ocpp_multi::OfferedLimitsCache cache;
    const auto first = cache.update(1, limits, supply_voltage_V);
    ASSERT_TRUE(first.has_value());
    EXPECT_FLOAT_EQ(first->max_power_W.value_or(0.0F), 11040.0F);
    EXPECT_FALSE(cache.update(1, limits, supply_voltage_V).has_value());
    EXPECT_EQ(supply_voltage_reads, 1);
    EXPECT_FLOAT_EQ(cache.get(1).max_power_W.value_or(0.0F), 11040.0F);

    limits.ac_max_current_A = {10.0F, "source"};
    const auto changed = cache.update(1, limits, supply_voltage_V);
    ASSERT_TRUE(changed.has_value());
    EXPECT_FLOAT_EQ(changed->max_power_W.value_or(0.0F), 6900.0F);
    EXPECT_EQ(supply_voltage_reads, 2);
}

TEST(OfferedLimitsCache, entriesAreByEvse) {
    types::energy::LimitsRes limits;
    limits.total_power_W = {50000.0F, "source"};

    ocpp_multi::OfferedLimitsCache cache;
    EXPECT_TRUE(cache.update(1, limits, supply_230V).has_value());
    EXPECT_TRUE(cache.update(2, limits, supply_230V).has_value());
    EXPECT_FALSE(cache.get(3).max_power_W.has_value());

    cache.clear();
    EXPECT_FALSE(cache.get(1).max_power_W.has_value());
    EXPECT_TRUE(cache.update(1, limits, supply_230V).has_value());
}

} // namespace
