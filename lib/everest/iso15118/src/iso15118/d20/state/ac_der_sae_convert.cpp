// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#include <iso15118/detail/d20/state/ac_der_sae_convert.hpp>

#include <optional>

namespace iso15118::d20::state {

namespace dt = message_20::datatypes;

namespace {

// The unit and reference enums are cast by value. The static_asserts below pin the matching enumerator order.
static_assert(static_cast<int>(sae::DERUnit::V) == static_cast<int>(dt_sae::DERUnit::V));
static_assert(static_cast<int>(sae::DERUnit::Hz) == static_cast<int>(dt_sae::DERUnit::Hz));
static_assert(static_cast<int>(sae::DERUnit::W) == static_cast<int>(dt_sae::DERUnit::W));
static_assert(static_cast<int>(sae::DERUnit::s) == static_cast<int>(dt_sae::DERUnit::s));
static_assert(static_cast<int>(sae::DERUnit::var) == static_cast<int>(dt_sae::DERUnit::var));
static_assert(static_cast<int>(sae::DERUnit::PercentageEVMaximumConfiguredActivePower) ==
              static_cast<int>(dt_sae::DERUnit::PercentageEVMaximumConfiguredActivePower));
static_assert(static_cast<int>(sae::DERUnit::PercentageEVMaximumConfiguredReactivePower) ==
              static_cast<int>(dt_sae::DERUnit::PercentageEVMaximumConfiguredReactivePower));
static_assert(static_cast<int>(sae::DERUnit::PercentageEVMaximumConfiguredApparentPower) ==
              static_cast<int>(dt_sae::DERUnit::PercentageEVMaximumConfiguredApparentPower));
static_assert(static_cast<int>(sae::DERUnit::PercentageEVMaximumAvailableActivePower) ==
              static_cast<int>(dt_sae::DERUnit::PercentageEVMaximumAvailableActivePower));
static_assert(static_cast<int>(sae::DERUnit::PercentageEVMaximumAvailableReactivePower) ==
              static_cast<int>(dt_sae::DERUnit::PercentageEVMaximumAvailableReactivePower));
static_assert(static_cast<int>(sae::DERUnit::PercentageV) == static_cast<int>(dt_sae::DERUnit::PercentageV));
static_assert(static_cast<int>(sae::PowerFactorExcitation::OverExcited) ==
              static_cast<int>(dt_sae::PowerFactorExcitation::OverExcited));
static_assert(static_cast<int>(sae::PowerFactorExcitation::UnderExcited) ==
              static_cast<int>(dt_sae::PowerFactorExcitation::UnderExcited));
static_assert(static_cast<int>(sae::PowerReference::MaximumActivePower) ==
              static_cast<int>(dt_sae::PowerReference::MaximumActivePower));
static_assert(static_cast<int>(sae::PowerReference::MomentaryPower) ==
              static_cast<int>(dt_sae::PowerReference::MomentaryPower));

std::optional<dt::RationalNumber> convert_optional(const std::optional<float>& in) {
    if (not in.has_value()) {
        return std::nullopt;
    }
    return dt::from_float(in.value());
}

std::optional<dt_sae::PowerFactorExcitation> convert_optional(const std::optional<sae::PowerFactorExcitation>& in) {
    if (not in.has_value()) {
        return std::nullopt;
    }
    return static_cast<dt_sae::PowerFactorExcitation>(in.value());
}

std::optional<dt_sae::PowerReference> convert_optional(const std::optional<sae::PowerReference>& in) {
    if (not in.has_value()) {
        return std::nullopt;
    }
    return static_cast<dt_sae::PowerReference>(in.value());
}

template <typename Out, typename In> std::optional<Out> convert_optional_element(const std::optional<In>& in) {
    if (not in.has_value()) {
        return std::nullopt;
    }
    Out out{};
    convert(out, in.value());
    return out;
}

} // namespace

void convert(dt_sae::CurveDataPointsList& out, const sae::CurveDataPointsList& in) {
    out.clear();
    for (const auto& in_point : in) {
        auto& out_point = out.emplace_back();
        out_point.x_value = dt::from_float(in_point.x_value);
        out_point.y_value = dt::from_float(in_point.y_value);
    }
}

void convert(dt_sae::DERCurve& out, const sae::DERCurve& in) {
    out.enable = in.enable;
    out.priority = in.priority;
    out.x_unit = static_cast<dt_sae::DERUnit>(in.x_unit);
    out.y_unit = static_cast<dt_sae::DERUnit>(in.y_unit);
    convert(out.curve_data_points, in.curve_data_points);
    out.curve_data_points_L2 = convert_optional_element<dt_sae::CurveDataPointsList>(in.curve_data_points_L2);
    out.curve_data_points_L3 = convert_optional_element<dt_sae::CurveDataPointsList>(in.curve_data_points_L3);
}

void convert(dt_sae::VoltageTrip& out, const sae::VoltageTrip& in) {
    convert(out.over_voltage_must_trip_curve, in.over_voltage_must_trip_curve);
    convert(out.under_voltage_must_trip_curve, in.under_voltage_must_trip_curve);
    out.over_voltage_momentary_cessation_trip_curve =
        convert_optional_element<dt_sae::DERCurve>(in.over_voltage_momentary_cessation_trip_curve);
    out.under_voltage_momentary_cessation_trip_curve =
        convert_optional_element<dt_sae::DERCurve>(in.under_voltage_momentary_cessation_trip_curve);
    out.over_voltage_may_trip_curve = convert_optional_element<dt_sae::DERCurve>(in.over_voltage_may_trip_curve);
    out.under_voltage_may_trip_curve = convert_optional_element<dt_sae::DERCurve>(in.under_voltage_may_trip_curve);
}

void convert(dt_sae::FrequencyTrip& out, const sae::FrequencyTrip& in) {
    convert(out.over_frequency_must_trip_curve, in.over_frequency_must_trip_curve);
    convert(out.under_frequency_must_trip_curve, in.under_frequency_must_trip_curve);
    out.over_frequency_may_trip_curve = convert_optional_element<dt_sae::DERCurve>(in.over_frequency_may_trip_curve);
    out.under_frequency_may_trip_curve = convert_optional_element<dt_sae::DERCurve>(in.under_frequency_may_trip_curve);
}

void convert(dt_sae::EnterServiceCPDRes& out, const sae::EnterServiceCPDRes& in) {
    out.permit_service = in.permit_service;
    out.enter_service_voltage_high = dt::from_float(in.enter_service_voltage_high);
    out.enter_service_voltage_low = dt::from_float(in.enter_service_voltage_low);
    out.enter_service_frequency_high = dt::from_float(in.enter_service_frequency_high);
    out.enter_service_frequency_low = dt::from_float(in.enter_service_frequency_low);
    out.enter_service_delay = convert_optional(in.enter_service_delay);
    out.enter_service_randomized_delay = convert_optional(in.enter_service_randomized_delay);
    out.enter_service_ramp_time = convert_optional(in.enter_service_ramp_time);
}

void convert(dt_sae::ConstantPowerFactor& out, const sae::ConstantPowerFactor& in) {
    out.enable = in.enable;
    out.priority = in.priority;
    out.power_factor_value = dt::from_float(in.power_factor_value);
    out.power_factor_value_L2 = convert_optional(in.power_factor_value_L2);
    out.power_factor_value_L3 = convert_optional(in.power_factor_value_L3);
    out.power_factor_excitation = static_cast<dt_sae::PowerFactorExcitation>(in.power_factor_excitation);
    out.power_factor_excitation_L2 = convert_optional(in.power_factor_excitation_L2);
    out.power_factor_excitation_L3 = convert_optional(in.power_factor_excitation_L3);
}

void convert(dt_sae::VoltVar& out, const sae::VoltVar& in) {
    out.enable = in.enable;
    out.priority = in.priority;
    out.x_unit = static_cast<dt_sae::DERUnit>(in.x_unit);
    out.y_unit = static_cast<dt_sae::DERUnit>(in.y_unit);
    convert(out.curve_data_points, in.curve_data_points);
    out.curve_data_points_L2 = convert_optional_element<dt_sae::CurveDataPointsList>(in.curve_data_points_L2);
    out.curve_data_points_L3 = convert_optional_element<dt_sae::CurveDataPointsList>(in.curve_data_points_L3);
    out.open_loop_response_time = dt::from_float(in.open_loop_response_time);
    out.time_constant_pt1 = in.time_constant_pt1;
    out.reference_voltage = dt::from_float(in.reference_voltage);
    out.autonomous_reference_voltage_adjustment_enable = in.autonomous_reference_voltage_adjustment_enable;
    out.reference_voltage_adjustment_time_constant = in.reference_voltage_adjustment_time_constant;
}

void convert(dt_sae::WattVar& out, const sae::WattVar& in) {
    out.enable = in.enable;
    out.priority = in.priority;
    out.x_unit = static_cast<dt_sae::DERUnit>(in.x_unit);
    out.y_unit = static_cast<dt_sae::DERUnit>(in.y_unit);
    convert(out.curve_data_points, in.curve_data_points);
    out.curve_data_points_L2 = convert_optional_element<dt_sae::CurveDataPointsList>(in.curve_data_points_L2);
    out.curve_data_points_L3 = convert_optional_element<dt_sae::CurveDataPointsList>(in.curve_data_points_L3);
    out.open_loop_response_time = convert_optional(in.open_loop_response_time);
    out.time_constant_pt1 = in.time_constant_pt1;
}

void convert(dt_sae::ConstantVar& out, const sae::ConstantVar& in) {
    out.enable = in.enable;
    out.priority = in.priority;
    out.var_setpoint = dt::from_float(in.var_setpoint);
    out.var_setpoint_L2 = convert_optional(in.var_setpoint_L2);
    out.var_setpoint_L3 = convert_optional(in.var_setpoint_L3);
    out.unit = static_cast<dt_sae::DERUnit>(in.unit);
}

void convert(dt_sae::ReactivePowerSupportCPDRes& out, const sae::ReactivePowerSupportCPDRes& in) {
    convert(out.constant_power_factor, in.constant_power_factor);
    convert(out.volt_var, in.volt_var);
    convert(out.watt_var, in.watt_var);
    convert(out.constant_var, in.constant_var);
}

void convert(dt_sae::FrequencyDroopSettings& out, const sae::FrequencyDroopSettings& in) {
    out.db = dt::from_float(in.db);
    out.droop_factor = dt::from_float(in.droop_factor);
    out.droop_factor_L2 = convert_optional(in.droop_factor_L2);
    out.droop_factor_L3 = convert_optional(in.droop_factor_L3);
    out.power_reference = static_cast<dt_sae::PowerReference>(in.power_reference);
    out.power_reference_L2 = convert_optional(in.power_reference_L2);
    out.power_reference_L3 = convert_optional(in.power_reference_L3);
    out.open_loop_response_time = dt::from_float(in.open_loop_response_time);
}

void convert(dt_sae::FrequencyDroop& out, const sae::FrequencyDroop& in) {
    out.enable = in.enable;
    out.priority = in.priority;
    out.over_frequency_droop = convert_optional_element<dt_sae::FrequencyDroopSettings>(in.over_frequency_droop);
    out.under_frequency_droop = convert_optional_element<dt_sae::FrequencyDroopSettings>(in.under_frequency_droop);
}

void convert(dt_sae::VoltWatt& out, const sae::VoltWatt& in) {
    out.enable = in.enable;
    out.priority = in.priority;
    out.x_unit = static_cast<dt_sae::DERUnit>(in.x_unit);
    out.y_unit = static_cast<dt_sae::DERUnit>(in.y_unit);
    convert(out.curve_data_points, in.curve_data_points);
    out.curve_data_points_L2 = convert_optional_element<dt_sae::CurveDataPointsList>(in.curve_data_points_L2);
    out.curve_data_points_L3 = convert_optional_element<dt_sae::CurveDataPointsList>(in.curve_data_points_L3);
    out.open_loop_response_time = dt::from_float(in.open_loop_response_time);
    out.time_constant_pt1 = in.time_constant_pt1;
}

void convert(dt_sae::ConstantWatt& out, const sae::ConstantWatt& in) {
    out.enable = in.enable;
    out.priority = in.priority;
    out.watt_setpoint = dt::from_float(in.watt_setpoint);
    out.watt_setpoint_L2 = convert_optional(in.watt_setpoint_L2);
    out.watt_setpoint_L3 = convert_optional(in.watt_setpoint_L3);
    out.unit = static_cast<dt_sae::DERUnit>(in.unit);
}

void convert(dt_sae::LimitMaxDischargePower& out, const sae::LimitMaxDischargePower& in) {
    out.enable = in.enable;
    out.priority = in.priority;
    out.percentage_value = in.percentage_value;
    out.percentage_value_L2 = in.percentage_value_L2;
    out.percentage_value_L3 = in.percentage_value_L3;
    out.open_loop_response_time = convert_optional(in.open_loop_response_time);
}

void convert(dt_sae::ActivePowerSupportCPDRes& out, const sae::ActivePowerSupportCPDRes& in) {
    convert(out.frequency_droop, in.frequency_droop);
    convert(out.volt_watt, in.volt_watt);
    convert(out.constant_watt, in.constant_watt);
    convert(out.limit_max_discharge_power, in.limit_max_discharge_power);
}

void convert(dt_sae::DERControlCPDRes& out, const sae::DERControl& in) {
    convert(out.voltage_trip, in.voltage_trip);
    convert(out.frequency_trip, in.frequency_trip);
    convert(out.enter_service_cpd_res, in.enter_service);
    convert(out.reactive_power_support_cpd_res, in.reactive_power_support);
    convert(out.active_power_support_cpd_res, in.active_power_support);
}

} // namespace iso15118::d20::state
