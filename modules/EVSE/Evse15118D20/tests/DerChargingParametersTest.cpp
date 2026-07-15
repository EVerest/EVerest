// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include <gtest/gtest.h>

#include <optional>

#include <iso15118/message/ac_der_iec_charge_parameter_discovery.hpp>
#include <iso15118/message/common_types.hpp>

#include "der_setup.hpp"

namespace dt = iso15118::message_20::datatypes;

namespace {

dt::DER_AC_CPDReqEnergyTransferMode make_ev_limits_with_reactive() {
    dt::DER_AC_CPDReqEnergyTransferMode ev{};
    // Active-power fields are required by the wire type but are not surfaced into
    // DERChargingParameters.
    ev.max_charge_power = dt::from_float(11000.0f);
    ev.min_charge_power = dt::from_float(0.0f);
    ev.max_discharge_power = dt::from_float(11000.0f);
    ev.min_discharge_power = dt::from_float(0.0f);

    ev.session_total_discharge_energy_available = dt::from_float(20000.0f);

    dt::ReactivePowerLimits reactive{};
    reactive.max_charge_reactive_power = dt::from_float(3000.0f);
    reactive.min_charge_reactive_power = dt::from_float(100.0f);
    reactive.max_discharge_reactive_power = dt::from_float(4000.0f);
    reactive.min_discharge_reactive_power = dt::from_float(200.0f);
    ev.reactive_power_limits = reactive;

    return ev;
}

} // namespace

TEST(DerChargingParametersTest, reactive_limits_and_session_energy_are_mapped) {
    const auto ev = make_ev_limits_with_reactive();

    const auto params = module::to_der_charging_parameters(ev);

    ASSERT_TRUE(params.max_charge_reactive_power.has_value());
    EXPECT_FLOAT_EQ(params.max_charge_reactive_power.value(), 3000.0f);

    ASSERT_TRUE(params.min_charge_reactive_power.has_value());
    EXPECT_FLOAT_EQ(params.min_charge_reactive_power.value(), 100.0f);

    ASSERT_TRUE(params.max_discharge_reactive_power.has_value());
    EXPECT_FLOAT_EQ(params.max_discharge_reactive_power.value(), 4000.0f);

    ASSERT_TRUE(params.min_discharge_reactive_power.has_value());
    EXPECT_FLOAT_EQ(params.min_discharge_reactive_power.value(), 200.0f);

    ASSERT_TRUE(params.ev_session_total_discharge_energy_available.has_value());
    EXPECT_FLOAT_EQ(params.ev_session_total_discharge_energy_available.value(), 20000.0f);
}

TEST(DerChargingParametersTest, per_phase_reactive_limits_are_mapped) {
    auto ev = make_ev_limits_with_reactive();
    ev.reactive_power_limits->max_charge_reactive_power_L2 = dt::from_float(1500.0f);
    ev.reactive_power_limits->max_charge_reactive_power_L3 = dt::from_float(1600.0f);
    ev.reactive_power_limits->max_discharge_reactive_power_L2 = dt::from_float(2500.0f);

    const auto params = module::to_der_charging_parameters(ev);

    ASSERT_TRUE(params.max_charge_reactive_power_l2.has_value());
    EXPECT_FLOAT_EQ(params.max_charge_reactive_power_l2.value(), 1500.0f);
    ASSERT_TRUE(params.max_charge_reactive_power_l3.has_value());
    EXPECT_FLOAT_EQ(params.max_charge_reactive_power_l3.value(), 1600.0f);
    ASSERT_TRUE(params.max_discharge_reactive_power_l2.has_value());
    EXPECT_FLOAT_EQ(params.max_discharge_reactive_power_l2.value(), 2500.0f);

    // Absent per-phase optionals must stay unset, not default to zero.
    EXPECT_FALSE(params.max_discharge_reactive_power_l3.has_value());
}

TEST(DerChargingParametersTest, absent_reactive_limits_leave_target_fields_unset) {
    dt::DER_AC_CPDReqEnergyTransferMode ev{};
    ev.max_charge_power = dt::from_float(11000.0f);
    ev.min_charge_power = dt::from_float(0.0f);
    ev.max_discharge_power = dt::from_float(11000.0f);
    ev.min_discharge_power = dt::from_float(0.0f);
    // No reactive_power_limits and no session_total_discharge_energy_available.

    const auto params = module::to_der_charging_parameters(ev);

    EXPECT_FALSE(params.max_charge_reactive_power.has_value());
    EXPECT_FALSE(params.min_charge_reactive_power.has_value());
    EXPECT_FALSE(params.max_discharge_reactive_power.has_value());
    EXPECT_FALSE(params.min_discharge_reactive_power.has_value());
    EXPECT_FALSE(params.ev_session_total_discharge_energy_available.has_value());
}

TEST(DerChargingParametersTest, ev_supported_dercontrol_is_left_unset) {
    const auto ev = make_ev_limits_with_reactive();

    const auto params = module::to_der_charging_parameters(ev);

    // The supported-DER-control-functions bitmap is not available at ChargeParameterDiscovery in
    // IEC (it lives in ServiceDetail); it must remain unset.
    EXPECT_FALSE(params.ev_supported_dercontrol.has_value());
}

TEST(DerChargingParametersTest, each_reactive_field_maps_to_its_own_target) {
    // Distinct sentinels per source field catch any L2/L3 transposition or charge/discharge swap in
    // the near-identical mapping lines.
    dt::DER_AC_CPDReqEnergyTransferMode ev{};
    ev.max_charge_power = dt::from_float(11000.0f);
    ev.min_charge_power = dt::from_float(0.0f);
    ev.max_discharge_power = dt::from_float(11000.0f);
    ev.min_discharge_power = dt::from_float(0.0f);

    ev.session_total_discharge_energy_available = dt::from_float(1300.0f);

    dt::ReactivePowerLimits reactive{};
    reactive.max_charge_reactive_power = dt::from_float(101.0f);
    reactive.max_charge_reactive_power_L2 = dt::from_float(102.0f);
    reactive.max_charge_reactive_power_L3 = dt::from_float(103.0f);
    reactive.min_charge_reactive_power = dt::from_float(201.0f);
    reactive.min_charge_reactive_power_L2 = dt::from_float(202.0f);
    reactive.min_charge_reactive_power_L3 = dt::from_float(203.0f);
    reactive.max_discharge_reactive_power = dt::from_float(301.0f);
    reactive.max_discharge_reactive_power_L2 = dt::from_float(302.0f);
    reactive.max_discharge_reactive_power_L3 = dt::from_float(303.0f);
    reactive.min_discharge_reactive_power = dt::from_float(401.0f);
    reactive.min_discharge_reactive_power_L2 = dt::from_float(402.0f);
    reactive.min_discharge_reactive_power_L3 = dt::from_float(403.0f);
    ev.reactive_power_limits = reactive;

    const auto params = module::to_der_charging_parameters(ev);

    ASSERT_TRUE(params.max_charge_reactive_power.has_value());
    EXPECT_FLOAT_EQ(params.max_charge_reactive_power.value(), 101.0f);
    ASSERT_TRUE(params.max_charge_reactive_power_l2.has_value());
    EXPECT_FLOAT_EQ(params.max_charge_reactive_power_l2.value(), 102.0f);
    ASSERT_TRUE(params.max_charge_reactive_power_l3.has_value());
    EXPECT_FLOAT_EQ(params.max_charge_reactive_power_l3.value(), 103.0f);

    ASSERT_TRUE(params.min_charge_reactive_power.has_value());
    EXPECT_FLOAT_EQ(params.min_charge_reactive_power.value(), 201.0f);
    ASSERT_TRUE(params.min_charge_reactive_power_l2.has_value());
    EXPECT_FLOAT_EQ(params.min_charge_reactive_power_l2.value(), 202.0f);
    ASSERT_TRUE(params.min_charge_reactive_power_l3.has_value());
    EXPECT_FLOAT_EQ(params.min_charge_reactive_power_l3.value(), 203.0f);

    ASSERT_TRUE(params.max_discharge_reactive_power.has_value());
    EXPECT_FLOAT_EQ(params.max_discharge_reactive_power.value(), 301.0f);
    ASSERT_TRUE(params.max_discharge_reactive_power_l2.has_value());
    EXPECT_FLOAT_EQ(params.max_discharge_reactive_power_l2.value(), 302.0f);
    ASSERT_TRUE(params.max_discharge_reactive_power_l3.has_value());
    EXPECT_FLOAT_EQ(params.max_discharge_reactive_power_l3.value(), 303.0f);

    ASSERT_TRUE(params.min_discharge_reactive_power.has_value());
    EXPECT_FLOAT_EQ(params.min_discharge_reactive_power.value(), 401.0f);
    ASSERT_TRUE(params.min_discharge_reactive_power_l2.has_value());
    EXPECT_FLOAT_EQ(params.min_discharge_reactive_power_l2.value(), 402.0f);
    ASSERT_TRUE(params.min_discharge_reactive_power_l3.has_value());
    EXPECT_FLOAT_EQ(params.min_discharge_reactive_power_l3.value(), 403.0f);

    ASSERT_TRUE(params.ev_session_total_discharge_energy_available.has_value());
    EXPECT_FLOAT_EQ(params.ev_session_total_discharge_energy_available.value(), 1300.0f);
}

TEST(DerChargingParametersTest, session_energy_maps_independently_of_reactive_limits) {
    dt::DER_AC_CPDReqEnergyTransferMode ev{};
    ev.max_charge_power = dt::from_float(11000.0f);
    ev.min_charge_power = dt::from_float(0.0f);
    ev.max_discharge_power = dt::from_float(11000.0f);
    ev.min_discharge_power = dt::from_float(0.0f);

    // Session energy present, reactive limits absent.
    ev.session_total_discharge_energy_available = dt::from_float(5000.0f);

    const auto params = module::to_der_charging_parameters(ev);

    ASSERT_TRUE(params.ev_session_total_discharge_energy_available.has_value());
    EXPECT_FLOAT_EQ(params.ev_session_total_discharge_energy_available.value(), 5000.0f);

    EXPECT_FALSE(params.max_charge_reactive_power.has_value());
    EXPECT_FALSE(params.min_charge_reactive_power.has_value());
    EXPECT_FALSE(params.max_discharge_reactive_power.has_value());
    EXPECT_FALSE(params.min_discharge_reactive_power.has_value());
}
