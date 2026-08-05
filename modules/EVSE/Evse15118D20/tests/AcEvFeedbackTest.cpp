// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include <gtest/gtest.h>

#include <iso15118/message/ac_der_sae_charge_loop.hpp>
#include <iso15118/message/ac_der_sae_charge_parameter_discovery.hpp>

#include "utils.hpp"

namespace dt = iso15118::message_20::datatypes;

namespace {

// Discharge power is negative on the wire, charge power positive. Every phase carries a distinct value so a
// dropped, duplicated or transposed phase cannot pass unnoticed.

dt::sae::DER_SAE_AC_CPDReqEnergyTransferMode make_sae_transfer_mode() {
    dt::sae::DER_SAE_AC_CPDReqEnergyTransferMode mode{};
    mode.max_charge_power = dt::from_float(11000.0f);
    mode.max_charge_power_L2 = dt::from_float(7000.0f);
    mode.max_charge_power_L3 = dt::from_float(5000.0f);
    mode.min_charge_power = dt::from_float(100.0f);
    mode.min_charge_power_L2 = dt::from_float(200.0f);
    mode.min_charge_power_L3 = dt::from_float(300.0f);
    mode.maximum_discharge_power = dt::from_float(-9000.0f);
    mode.maximum_discharge_power_L2 = dt::from_float(-6000.0f);
    mode.maximum_discharge_power_L3 = dt::from_float(-3000.0f);
    mode.minimum_discharge_power = dt::from_float(-110.0f);
    mode.minimum_discharge_power_L2 = dt::from_float(-210.0f);
    mode.minimum_discharge_power_L3 = dt::from_float(-310.0f);
    return mode;
}

dt::sae::DER_Scheduled_AC_CLReqControlMode make_sae_scheduled_mode() {
    dt::sae::DER_Scheduled_AC_CLReqControlMode mode{};
    mode.max_charge_power = dt::from_float(12000.0f);
    mode.max_charge_power_L2 = dt::from_float(7100.0f);
    mode.max_charge_power_L3 = dt::from_float(5100.0f);
    mode.min_charge_power = dt::from_float(120.0f);
    mode.min_charge_power_L2 = dt::from_float(220.0f);
    mode.min_charge_power_L3 = dt::from_float(320.0f);
    mode.present_active_power = dt::from_float(5000.0f);
    mode.present_active_power_L2 = dt::from_float(3000.0f);
    mode.present_active_power_L3 = dt::from_float(1000.0f);
    mode.maximum_discharge_power = dt::from_float(-9100.0f);
    mode.maximum_discharge_power_L2 = dt::from_float(-6100.0f);
    mode.maximum_discharge_power_L3 = dt::from_float(-3100.0f);
    mode.minimum_discharge_power = dt::from_float(-130.0f);
    mode.minimum_discharge_power_L2 = dt::from_float(-230.0f);
    mode.minimum_discharge_power_L3 = dt::from_float(-330.0f);
    return mode;
}

dt::sae::DER_Dynamic_AC_CLReqControlMode make_sae_dynamic_mode() {
    dt::sae::DER_Dynamic_AC_CLReqControlMode mode{};
    mode.max_charge_power = dt::from_float(13000.0f);
    mode.max_charge_power_L2 = dt::from_float(7200.0f);
    mode.max_charge_power_L3 = dt::from_float(5200.0f);
    mode.min_charge_power = dt::from_float(140.0f);
    mode.min_charge_power_L2 = dt::from_float(240.0f);
    mode.min_charge_power_L3 = dt::from_float(340.0f);
    mode.present_active_power = dt::from_float(5100.0f);
    mode.present_active_power_L2 = dt::from_float(3100.0f);
    mode.present_active_power_L3 = dt::from_float(1100.0f);
    mode.present_reactive_power = dt::from_float(1000.0f);
    mode.present_reactive_power_L2 = dt::from_float(800.0f);
    mode.present_reactive_power_L3 = dt::from_float(600.0f);
    mode.maximum_discharge_power = dt::from_float(-9200.0f);
    mode.maximum_discharge_power_L2 = dt::from_float(-6200.0f);
    mode.maximum_discharge_power_L3 = dt::from_float(-3200.0f);
    mode.minimum_discharge_power = dt::from_float(-150.0f);
    mode.minimum_discharge_power_L2 = dt::from_float(-250.0f);
    mode.minimum_discharge_power_L3 = dt::from_float(-350.0f);
    return mode;
}

void expect_power(const types::units::Power& actual, float total, float l1, float l2, float l3) {
    EXPECT_FLOAT_EQ(actual.total, total);
    ASSERT_TRUE(actual.L1.has_value());
    EXPECT_FLOAT_EQ(actual.L1.value(), l1);
    ASSERT_TRUE(actual.L2.has_value());
    EXPECT_FLOAT_EQ(actual.L2.value(), l2);
    ASSERT_TRUE(actual.L3.has_value());
    EXPECT_FLOAT_EQ(actual.L3.value(), l3);
}

void expect_power(const std::optional<types::units::Power>& actual, float total, float l1, float l2, float l3) {
    ASSERT_TRUE(actual.has_value());
    expect_power(actual.value(), total, l1, l2, l3);
}

} // namespace

TEST(AcEvFeedbackTest, sae_transfer_mode_charge_and_discharge_limits_are_mapped) {
    const auto limits = module::charger::fill_ac_ev_power_limits(make_sae_transfer_mode());

    expect_power(limits.max_charge_power, 23000.0f, 11000.0f, 7000.0f, 5000.0f);
    expect_power(limits.min_charge_power, 600.0f, 100.0f, 200.0f, 300.0f);
    expect_power(limits.max_discharge_power, -18000.0f, -9000.0f, -6000.0f, -3000.0f);
    expect_power(limits.min_discharge_power, -630.0f, -110.0f, -210.0f, -310.0f);
}

TEST(AcEvFeedbackTest, sae_transfer_mode_absent_optionals_stay_unset) {
    auto mode = make_sae_transfer_mode();
    mode.minimum_discharge_power.reset();
    mode.minimum_discharge_power_L2.reset();
    mode.minimum_discharge_power_L3.reset();
    mode.maximum_discharge_power_L3.reset();

    const auto limits = module::charger::fill_ac_ev_power_limits(mode);

    EXPECT_FALSE(limits.min_discharge_power.has_value());

    ASSERT_TRUE(limits.max_discharge_power.has_value());
    EXPECT_FLOAT_EQ(limits.max_discharge_power->total, -15000.0f);
    EXPECT_FALSE(limits.max_discharge_power->L3.has_value());
}

TEST(AcEvFeedbackTest, sae_scheduled_mode_charge_and_discharge_limits_are_mapped) {
    const auto limits = module::charger::fill_ac_ev_power_limits(make_sae_scheduled_mode());

    expect_power(limits.max_charge_power, 24200.0f, 12000.0f, 7100.0f, 5100.0f);
    expect_power(limits.min_charge_power, 660.0f, 120.0f, 220.0f, 320.0f);
    expect_power(limits.max_discharge_power, -18300.0f, -9100.0f, -6100.0f, -3100.0f);
    expect_power(limits.min_discharge_power, -690.0f, -130.0f, -230.0f, -330.0f);
}

TEST(AcEvFeedbackTest, sae_dynamic_mode_charge_and_discharge_limits_are_mapped) {
    const auto limits = module::charger::fill_ac_ev_power_limits(make_sae_dynamic_mode());

    expect_power(limits.max_charge_power, 25400.0f, 13000.0f, 7200.0f, 5200.0f);
    expect_power(limits.min_charge_power, 720.0f, 140.0f, 240.0f, 340.0f);
    expect_power(limits.max_discharge_power, -18600.0f, -9200.0f, -6200.0f, -3200.0f);
    expect_power(limits.min_discharge_power, -750.0f, -150.0f, -250.0f, -350.0f);
}

// The SAE control modes add no present-power fields, so they reuse the base-class mapping.
TEST(AcEvFeedbackTest, sae_modes_present_powers_come_from_the_shared_mapping) {
    const auto scheduled = module::charger::fill_ac_ev_present_power_values(make_sae_scheduled_mode());
    expect_power(scheduled.present_active_power, 9000.0f, 5000.0f, 3000.0f, 1000.0f);
    // The Scheduled base leaves reactive power optional and the fixture does not send it.
    EXPECT_FALSE(scheduled.present_reactive_power.has_value());

    const auto dynamic = module::charger::fill_ac_ev_present_power_values(make_sae_dynamic_mode());
    expect_power(dynamic.present_active_power, 9300.0f, 5100.0f, 3100.0f, 1100.0f);
    expect_power(dynamic.present_reactive_power, 2400.0f, 1000.0f, 800.0f, 600.0f);
}
