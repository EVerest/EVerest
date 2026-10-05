// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include <gtest/gtest.h>

#include <optional>
#include <utility>
#include <vector>

#include <iso15118/d20/der_functions.hpp>
#include <iso15118/message/ac_der_iec_charge_parameter_discovery.hpp>
#include <iso15118/message/ac_der_sae_charge_parameter_discovery.hpp>
#include <iso15118/message/common_types.hpp>
#include <iso15118/sae_modes.hpp>

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

namespace {

using SaeFn = iso15118::sae::DerBitMapFunctions;
using DT = types::grid_support::DirectiveType;

dt::sae::DER_SAE_AC_CPDReqEnergyTransferMode make_sae_ev_limits() {
    dt::sae::DER_SAE_AC_CPDReqEnergyTransferMode ev{};
    ev.max_charge_power = dt::from_float(11000.0f);
    ev.min_charge_power = dt::from_float(0.0f);
    ev.maximum_discharge_power = dt::from_float(11000.0f);
    return ev;
}

} // namespace

TEST(DerChargingParametersTest, sae_each_mapped_bit_yields_its_directive) {
    // One entry per SAE function with a grid_support DirectiveType counterpart.
    const std::vector<std::pair<SaeFn, DT>> expected = {
        {SaeFn::EnterService, DT::EnterService},
        {SaeFn::ConstantPowerFactorUnderExcitedFunction, DT::FixedPFAbsorb},
        {SaeFn::ConstantPowerFactorOverExcitedFunction, DT::FixedPFInject},
        {SaeFn::ConstantReactivePowerFunction, DT::FixedVar},
        {SaeFn::FrequencyDroopFunction, DT::FreqDroop},
        {SaeFn::HighFrequencyMayTripFunction, DT::HFMayTrip},
        {SaeFn::HighFrequencyMustTripFunction, DT::HFMustTrip},
        {SaeFn::HighVoltageMayTripFunction, DT::HVMayTrip},
        {SaeFn::HighVoltageMomentaryCessationFunction, DT::HVMomCess},
        {SaeFn::HighVoltageMustTripFunction, DT::HVMustTrip},
        {SaeFn::LowFrequencyMustTripFunction, DT::LFMustTrip},
        {SaeFn::LowVoltageMayTripFunction, DT::LVMayTrip},
        {SaeFn::LowVoltageMomentaryCessationFunction, DT::LVMomCess},
        {SaeFn::LowVoltageMustTripFunction, DT::LVMustTrip},
        {SaeFn::LimitMaximumActiveDischargePowerFunction, DT::LimitMaxDischarge},
        {SaeFn::VoltVarFunction, DT::VoltVar},
        {SaeFn::VoltWattFunction, DT::VoltWatt},
        {SaeFn::WattVarFunction, DT::WattVar},
    };

    for (const auto& [function, directive] : expected) {
        SCOPED_TRACE(iso15118::sae::sae_function_names(iso15118::sae::sae_function_bit(function)));

        auto ev = make_sae_ev_limits();
        ev.supported_modes = iso15118::sae::sae_function_bit(function);

        const auto params = module::to_der_charging_parameters(ev);

        ASSERT_TRUE(params.ev_supported_dercontrol.has_value());
        ASSERT_EQ(params.ev_supported_dercontrol->size(), 1u);
        EXPECT_EQ(params.ev_supported_dercontrol->front(), directive);
    }
}

TEST(DerChargingParametersTest, sae_unmappable_bits_are_dropped_without_error) {
    auto ev = make_sae_ev_limits();
    ev.supported_modes = iso15118::sae::sae_function_bit(SaeFn::ChargeFunction) |
                         iso15118::sae::sae_function_bit(SaeFn::DischargeFunction) |
                         iso15118::sae::sae_function_bit(SaeFn::ConstantActivePowerFunction) |
                         iso15118::sae::sae_function_bit(SaeFn::LowFrequencyMayTripFunction) |
                         iso15118::sae::sae_function_bit(SaeFn::EVSETargetReactivePowerFunction) |
                         iso15118::sae::sae_function_bit(SaeFn::EVSETargetActivePowerFunction);

    types::iso15118::DERChargingParameters params{};
    EXPECT_NO_THROW(params = module::to_der_charging_parameters(ev));

    // All bits unmappable: the field must stay unset (minItems:1), not become an empty list.
    EXPECT_FALSE(params.ev_supported_dercontrol.has_value());
}

TEST(DerChargingParametersTest, sae_mapped_bits_survive_unmappable_neighbors) {
    auto ev = make_sae_ev_limits();
    ev.supported_modes = iso15118::sae::sae_function_bit(SaeFn::ChargeFunction) |
                         iso15118::sae::sae_function_bit(SaeFn::VoltVarFunction) |
                         iso15118::sae::sae_function_bit(SaeFn::EVSETargetActivePowerFunction);

    const auto params = module::to_der_charging_parameters(ev);

    ASSERT_TRUE(params.ev_supported_dercontrol.has_value());
    ASSERT_EQ(params.ev_supported_dercontrol->size(), 1u);
    EXPECT_EQ(params.ev_supported_dercontrol->front(), DT::VoltVar);
}

TEST(DerChargingParametersTest, sae_empty_supported_modes_leaves_dercontrol_unset) {
    const auto ev = make_sae_ev_limits();

    const auto params = module::to_der_charging_parameters(ev);

    EXPECT_FALSE(params.ev_supported_dercontrol.has_value());
}

TEST(DerChargingParametersTest, sae_excitation_values_round_trip) {
    auto ev = make_sae_ev_limits();
    ev.excitation_limits.specified_over_excited_power_factor = dt::from_float(0.95f);
    ev.excitation_limits.specified_over_excited_discharge_power = dt::from_float(10450.0f);
    ev.excitation_limits.specified_under_excited_power_factor = dt::from_float(0.9f);
    ev.excitation_limits.specified_under_excited_discharge_power = dt::from_float(9900.0f);

    const auto params = module::to_der_charging_parameters(ev);

    // Compare against the value the RationalNumber wire type carries: from_float quantizes (0.9f
    // does not survive exactly), and the mapper must not add any loss on top of that.
    ASSERT_TRUE(params.ev_over_excited_power_factor.has_value());
    EXPECT_FLOAT_EQ(params.ev_over_excited_power_factor.value(),
                    dt::from_RationalNumber(ev.excitation_limits.specified_over_excited_power_factor));
    ASSERT_TRUE(params.ev_over_excited_max_discharge_power.has_value());
    EXPECT_FLOAT_EQ(params.ev_over_excited_max_discharge_power.value(), 10450.0f);
    ASSERT_TRUE(params.ev_under_excited_power_factor.has_value());
    EXPECT_FLOAT_EQ(params.ev_under_excited_power_factor.value(),
                    dt::from_RationalNumber(ev.excitation_limits.specified_under_excited_power_factor));
    ASSERT_TRUE(params.ev_under_excited_max_discharge_power.has_value());
    EXPECT_FLOAT_EQ(params.ev_under_excited_max_discharge_power.value(), 9900.0f);

    // The two quantized power factors must still land near their sources and stay distinct.
    EXPECT_NEAR(params.ev_over_excited_power_factor.value(), 0.95f, 0.001f);
    EXPECT_NEAR(params.ev_under_excited_power_factor.value(), 0.9f, 0.001f);
}

TEST(DerChargingParametersTest, sae_fields_without_a_source_stay_unset) {
    const auto ev = make_sae_ev_limits();

    const auto params = module::to_der_charging_parameters(ev);

    // The IEC-style charge/discharge reactive fields are not mapped: SAE var absorption/injection
    // semantics differ from the IEC charge/discharge reactive fields. Session energy is optional in
    // the SAE request; absent must stay unset.
    EXPECT_FALSE(params.max_charge_reactive_power.has_value());
    EXPECT_FALSE(params.min_charge_reactive_power.has_value());
    EXPECT_FALSE(params.max_discharge_reactive_power.has_value());
    EXPECT_FALSE(params.min_discharge_reactive_power.has_value());
    EXPECT_FALSE(params.ev_session_total_discharge_energy_available.has_value());
}

TEST(DerChargingParametersTest, sae_session_energy_round_trips_when_present) {
    auto ev = make_sae_ev_limits();
    ev.session_total_discharge_energy_available = dt::from_float(20000.0f);

    const auto params = module::to_der_charging_parameters(ev);

    ASSERT_TRUE(params.ev_session_total_discharge_energy_available.has_value());
    EXPECT_FLOAT_EQ(params.ev_session_total_discharge_energy_available.value(), 20000.0f);
}

TEST(DerChargingParametersTest, sae_multi_bit_bitmap_maps_in_table_order) {
    auto ev = make_sae_ev_limits();
    ev.supported_modes = iso15118::sae::sae_function_bit(SaeFn::EnterService) |
                         iso15118::sae::sae_function_bit(SaeFn::FrequencyDroopFunction) |
                         iso15118::sae::sae_function_bit(SaeFn::WattVarFunction);

    const auto params = module::to_der_charging_parameters(ev);

    ASSERT_TRUE(params.ev_supported_dercontrol.has_value());
    const std::vector<DT> expected{DT::EnterService, DT::FreqDroop, DT::WattVar};
    EXPECT_EQ(params.ev_supported_dercontrol.value(), expected);
}

TEST(DerChargingParametersTest, sae_full_bitmap_yields_all_mappable_directives) {
    auto ev = make_sae_ev_limits();
    ev.supported_modes = iso15118::sae::SAE_MODE_BITMAP_MASK;

    const auto params = module::to_der_charging_parameters(ev);

    // 24 defined bits, 6 with no DirectiveType counterpart (charge, discharge, constant watt,
    // under frequency may trip, the two EVSE target powers).
    ASSERT_TRUE(params.ev_supported_dercontrol.has_value());
    EXPECT_EQ(params.ev_supported_dercontrol->size(), 18u);
}
