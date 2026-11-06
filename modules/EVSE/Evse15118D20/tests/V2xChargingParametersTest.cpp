// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include <gtest/gtest.h>

#include <iso15118/d20/limits.hpp>
#include <iso15118/message/ac_charge_parameter_discovery.hpp>
#include <iso15118/message/dc_charge_parameter_discovery.hpp>

#include "utils.hpp"

namespace dt = iso15118::message_20::datatypes;

namespace {

// [V2G20-1035]: the EV sends its discharge limits as <=0, and negative_bidirectional_limits (the
// default) stores the EVSE's as <=0 too. V2XChargingParameters wants >= 0, so the merge reads both
// operands by magnitude. Each pair has a distinct tighter side, EVSE or EV.

iso15118::d20::DcTransferLimits make_dc_evse_limits(float sign) {
    iso15118::d20::DcTransferLimits limits{};
    limits.charge_limits.power = {dt::from_float(50000.0f), dt::from_float(0.0f)};
    limits.charge_limits.current = {dt::from_float(125.0f), dt::from_float(0.0f)};
    limits.voltage = {dt::from_float(500.0f), dt::from_float(200.0f)};
    auto& discharge = limits.discharge_limits.emplace();
    discharge.power = {dt::from_float(sign * 30000.0f), dt::from_float(sign * 100.0f)};
    discharge.current = {dt::from_float(sign * 80.0f), dt::from_float(sign * 3.0f)};
    return limits;
}

dt::BPT_DC_CPDReqEnergyTransferMode make_dc_ev_limits(float sign) {
    dt::BPT_DC_CPDReqEnergyTransferMode mode{};
    mode.max_charge_power = dt::from_float(40000.0f);
    mode.min_charge_power = dt::from_float(0.0f);
    mode.max_charge_current = dt::from_float(100.0f);
    mode.min_charge_current = dt::from_float(0.0f);
    mode.max_voltage = dt::from_float(450.0f);
    mode.min_voltage = dt::from_float(250.0f);
    mode.max_discharge_power = dt::from_float(sign * 20000.0f);
    mode.min_discharge_power = dt::from_float(sign * 500.0f);
    mode.max_discharge_current = dt::from_float(sign * 60.0f);
    mode.min_discharge_current = dt::from_float(sign * 2.0f);
    return mode;
}

iso15118::d20::AcTransferLimits make_ac_evse_limits(float sign) {
    iso15118::d20::AcTransferLimits limits{};
    limits.charge_power = {dt::from_float(22000.0f), dt::from_float(0.0f)};
    limits.nominal_frequency = dt::from_float(50.0f);
    limits.discharge_power = {{dt::from_float(sign * 11000.0f), dt::from_float(sign * 100.0f)}};
    limits.discharge_power_L2 = {{dt::from_float(sign * 3600.0f), dt::from_float(sign * 30.0f)}};
    limits.discharge_power_L3 = {{dt::from_float(sign * 2800.0f), dt::from_float(sign * 220.0f)}};
    return limits;
}

dt::BPT_AC_CPDReqEnergyTransferMode make_ac_ev_limits(float sign) {
    dt::BPT_AC_CPDReqEnergyTransferMode mode{};
    mode.max_charge_power = dt::from_float(11000.0f);
    mode.min_charge_power = dt::from_float(1000.0f);
    mode.max_discharge_power = dt::from_float(sign * 9000.0f);
    mode.min_discharge_power = dt::from_float(sign * 800.0f);
    mode.max_discharge_power_L2 = dt::from_float(sign * 3000.0f);
    mode.min_discharge_power_L2 = dt::from_float(sign * 200.0f);
    mode.max_discharge_power_L3 = dt::from_float(sign * 2900.0f);
    mode.min_discharge_power_L3 = dt::from_float(sign * 210.0f);
    return mode;
}

types::iso15118::V2XChargingParameters merge_dc(float evse_sign, float ev_sign) {
    types::iso15118::V2XChargingParameters out{};
    module::charger::fill_v2x_charging_parameters(out, make_dc_evse_limits(evse_sign), make_dc_ev_limits(ev_sign));
    return out;
}

types::iso15118::V2XChargingParameters merge_ac(float evse_sign, float ev_sign) {
    types::iso15118::V2XChargingParameters out{};
    module::charger::fill_v2x_charging_parameters(out, make_ac_evse_limits(evse_sign), make_ac_ev_limits(ev_sign));
    return out;
}

// The sign of negative_bidirectional_limits: true stores the EVSE discharge limits negative.
constexpr float EVSE_NEGATIVE = -1.0f;
constexpr float EVSE_POSITIVE = 1.0f;
constexpr float EV_COMPLIANT = -1.0f;
constexpr float EV_POSITIVE = 1.0f;

void expect_dc_tighter_magnitudes(const types::iso15118::V2XChargingParameters& out) {
    EXPECT_FLOAT_EQ(out.max_discharge_power.value(), 20000.0f);
    EXPECT_FLOAT_EQ(out.min_discharge_power.value(), 500.0f);
    EXPECT_FLOAT_EQ(out.max_discharge_current.value(), 60.0f);
    EXPECT_FLOAT_EQ(out.min_discharge_current.value(), 3.0f);
}

void expect_ac_tighter_magnitudes(const types::iso15118::V2XChargingParameters& out) {
    EXPECT_FLOAT_EQ(out.max_discharge_power.value(), 9000.0f);
    EXPECT_FLOAT_EQ(out.min_discharge_power.value(), 800.0f);
    EXPECT_FLOAT_EQ(out.max_discharge_power_l2.value(), 3000.0f);
    EXPECT_FLOAT_EQ(out.min_discharge_power_l2.value(), 200.0f);
    EXPECT_FLOAT_EQ(out.max_discharge_power_l3.value(), 2800.0f);
    EXPECT_FLOAT_EQ(out.min_discharge_power_l3.value(), 220.0f);
}

} // namespace

TEST(V2xChargingParametersTest, dc_negative_bidirectional_limits_merge_the_tighter_magnitude) {
    for (const auto ev_sign : {EV_COMPLIANT, EV_POSITIVE}) {
        SCOPED_TRACE(ev_sign);
        expect_dc_tighter_magnitudes(merge_dc(EVSE_NEGATIVE, ev_sign));
    }
}

TEST(V2xChargingParametersTest, dc_positive_bidirectional_limits_merge_the_tighter_magnitude) {
    for (const auto ev_sign : {EV_COMPLIANT, EV_POSITIVE}) {
        SCOPED_TRACE(ev_sign);
        expect_dc_tighter_magnitudes(merge_dc(EVSE_POSITIVE, ev_sign));
    }
}

TEST(V2xChargingParametersTest, ac_negative_bidirectional_limits_merge_the_tighter_magnitude) {
    for (const auto ev_sign : {EV_COMPLIANT, EV_POSITIVE}) {
        SCOPED_TRACE(ev_sign);
        expect_ac_tighter_magnitudes(merge_ac(EVSE_NEGATIVE, ev_sign));
    }
}

TEST(V2xChargingParametersTest, ac_positive_bidirectional_limits_merge_the_tighter_magnitude) {
    for (const auto ev_sign : {EV_COMPLIANT, EV_POSITIVE}) {
        SCOPED_TRACE(ev_sign);
        expect_ac_tighter_magnitudes(merge_ac(EVSE_POSITIVE, ev_sign));
    }
}
