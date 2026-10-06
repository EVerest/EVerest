// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#include <iso15118/detail/d20/state/ac_der_sae_convert.hpp>

#include <iso15118/detail/helper.hpp>

#include <cstdint>
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

using DerFn = sae::DerBitMapFunctions;

// The per family gates below are shared by the charge parameter discovery and the charge loop responses, which
// carry the identical leaf wire types.
void gate_function(dt_sae::VoltageTrip& out, std::uint32_t supported_modes) {
    gate_enable(out.over_voltage_must_trip_curve.enable,
                is_function_set(supported_modes, DerFn::HighVoltageMustTripFunction), "over voltage must trip curve");
    gate_enable(out.under_voltage_must_trip_curve.enable,
                is_function_set(supported_modes, DerFn::LowVoltageMustTripFunction), "under voltage must trip curve");
    gate_optional_curve(out.over_voltage_momentary_cessation_trip_curve,
                        is_function_set(supported_modes, DerFn::HighVoltageMomentaryCessationFunction),
                        "over voltage momentary cessation trip curve");
    gate_optional_curve(out.under_voltage_momentary_cessation_trip_curve,
                        is_function_set(supported_modes, DerFn::LowVoltageMomentaryCessationFunction),
                        "under voltage momentary cessation trip curve");
    gate_optional_curve(out.over_voltage_may_trip_curve,
                        is_function_set(supported_modes, DerFn::HighVoltageMayTripFunction),
                        "over voltage may trip curve");
    gate_optional_curve(out.under_voltage_may_trip_curve,
                        is_function_set(supported_modes, DerFn::LowVoltageMayTripFunction),
                        "under voltage may trip curve");
}

void gate_function(dt_sae::FrequencyTrip& out, std::uint32_t supported_modes) {
    gate_enable(out.over_frequency_must_trip_curve.enable,
                is_function_set(supported_modes, DerFn::HighFrequencyMustTripFunction),
                "over frequency must trip curve");
    gate_enable(out.under_frequency_must_trip_curve.enable,
                is_function_set(supported_modes, DerFn::LowFrequencyMustTripFunction),
                "under frequency must trip curve");
    gate_optional_curve(out.over_frequency_may_trip_curve,
                        is_function_set(supported_modes, DerFn::HighFrequencyMayTripFunction),
                        "over frequency may trip curve");
    gate_optional_curve(out.under_frequency_may_trip_curve,
                        is_function_set(supported_modes, DerFn::LowFrequencyMayTripFunction),
                        "under frequency may trip curve");
}

constexpr DerFn excitation_function(dt_sae::PowerFactorExcitation excitation) {
    return excitation == dt_sae::PowerFactorExcitation::OverExcited ? DerFn::ConstantPowerFactorOverExcitedFunction
                                                                    : DerFn::ConstantPowerFactorUnderExcitedFunction;
}

bool excitation_declared(std::uint32_t supported_modes, dt_sae::PowerFactorExcitation excitation) {
    return is_function_set(supported_modes, excitation_function(excitation));
}

bool excitation_declared(std::uint32_t supported_modes,
                         const std::optional<dt_sae::PowerFactorExcitation>& excitation) {
    return not excitation.has_value() or excitation_declared(supported_modes, excitation.value());
}

// The direction that costs the function its enable, so the log line names the phase direction actually
// missing a bit rather than whatever the base phase happens to carry.
std::optional<dt_sae::PowerFactorExcitation> undeclared_excitation(const dt_sae::ConstantPowerFactor& out,
                                                                   std::uint32_t supported_modes) {
    const std::optional<dt_sae::PowerFactorExcitation> phases[] = {
        out.power_factor_excitation, out.power_factor_excitation_L2, out.power_factor_excitation_L3};
    for (const auto& excitation : phases) {
        if (excitation.has_value() and not excitation_declared(supported_modes, excitation.value())) {
            return excitation;
        }
    }
    return std::nullopt;
}

void gate_function(dt_sae::ConstantPowerFactor& out, std::uint32_t supported_modes) {
    // The two excitation directions have separate bits and every phase carries its own direction, so each
    // direction the SECC actually sends needs its matching bit. A single undeclared direction disables the
    // whole function: the enable is one flag, there is no per phase enable to clear.
    const auto undeclared = undeclared_excitation(out, supported_modes);
    gate_enable(out.enable, not undeclared.has_value(),
                undeclared.value_or(out.power_factor_excitation) == dt_sae::PowerFactorExcitation::OverExcited
                    ? "constant power factor over excited"
                    : "constant power factor under excited");
}

void gate_function(dt_sae::VoltVar& out, std::uint32_t supported_modes) {
    gate_enable(out.enable, is_function_set(supported_modes, DerFn::VoltVarFunction), "volt var");
}

void gate_function(dt_sae::WattVar& out, std::uint32_t supported_modes) {
    gate_enable(out.enable, is_function_set(supported_modes, DerFn::WattVarFunction), "watt var");
}

void gate_function(dt_sae::ConstantVar& out, std::uint32_t supported_modes) {
    gate_enable(out.enable, is_function_set(supported_modes, DerFn::ConstantReactivePowerFunction), "constant var");
}

void gate_function(dt_sae::FrequencyDroop& out, std::uint32_t supported_modes) {
    gate_enable(out.enable, is_function_set(supported_modes, DerFn::FrequencyDroopFunction), "frequency droop");
}

void gate_function(dt_sae::VoltWatt& out, std::uint32_t supported_modes) {
    gate_enable(out.enable, is_function_set(supported_modes, DerFn::VoltWattFunction), "volt watt");
}

void gate_function(dt_sae::ConstantWatt& out, std::uint32_t supported_modes) {
    gate_enable(out.enable, is_function_set(supported_modes, DerFn::ConstantActivePowerFunction), "constant watt");
}

void gate_function(dt_sae::LimitMaxDischargePower& out, std::uint32_t supported_modes) {
    gate_enable(out.enable, is_function_set(supported_modes, DerFn::LimitMaximumActiveDischargePowerFunction),
                "limit maximum discharge power");
}

template <typename T> void gate_optional_function(std::optional<T>& block, std::uint32_t supported_modes) {
    if (block.has_value()) {
        gate_function(block.value(), supported_modes);
    }
}

void warn_unpaired_enter_service_delay(const sae::EnterServiceCPDRes& in) {
    if (in.enter_service_delay.has_value() and not in.enter_service_ramp_time.has_value()) {
        logf_warning("enter_service_delay is configured without enter_service_ramp_time");
    }
    if (not in.enter_service_delay.has_value() and not in.enter_service_randomized_delay.has_value()) {
        logf_warning("Neither enter_service_delay nor enter_service_randomized_delay is configured");
    }
}

// The threshold members are mandatory in EnterServiceCPDRes and optional in EnterServiceCLRes, so both accept
// the same assignments.
template <typename Out> void convert_enter_service(Out& out, const sae::EnterServiceCPDRes& in) {
    warn_unpaired_enter_service_delay(in);

    out.permit_service = in.permit_service;
    out.enter_service_voltage_high = dt::from_float(in.enter_service_voltage_high);
    out.enter_service_voltage_low = dt::from_float(in.enter_service_voltage_low);
    out.enter_service_frequency_high = dt::from_float(in.enter_service_frequency_high);
    out.enter_service_frequency_low = dt::from_float(in.enter_service_frequency_low);
    out.enter_service_delay = convert_optional(in.enter_service_delay);
    out.enter_service_randomized_delay = convert_optional(in.enter_service_randomized_delay);
    out.enter_service_ramp_time = convert_optional(in.enter_service_ramp_time);
}

} // namespace

void gate_enable(bool& enable, bool supported, const char* function_name) {
    if (enable and not supported) {
        logf_warning("Clearing enable of %s: EV did not declare support for it", function_name);
        enable = false;
    }
}

void gate_optional_curve(std::optional<dt_sae::DERCurve>& curve, bool supported, const char* function_name) {
    if (curve.has_value()) {
        gate_enable(curve.value().enable, supported, function_name);
    }
}

// The SECC expresses itself only through the per-function Enable flags, so a function the EV does not
// list in SupportedModes must not be enabled. This follows a semantics statement, not a numbered
// requirement.
// permit_service is not gated: an authorization, not an Enable, so false denies rather than fails to
// activate. See ADR-0023.
// The call order below follows the DERControlCPDRes declaration order so it can be audited side by side.
void gate_enables_by_supported_modes(dt_sae::DERControlCPDRes& out, std::uint32_t supported_modes) {
    gate_function(out.voltage_trip, supported_modes);
    gate_function(out.frequency_trip, supported_modes);

    auto& reactive = out.reactive_power_support_cpd_res;
    gate_function(reactive.constant_power_factor, supported_modes);
    gate_function(reactive.volt_var, supported_modes);
    gate_function(reactive.watt_var, supported_modes);
    gate_function(reactive.constant_var, supported_modes);

    auto& active = out.active_power_support_cpd_res;
    gate_function(active.frequency_droop, supported_modes);
    gate_function(active.volt_watt, supported_modes);
    gate_function(active.constant_watt, supported_modes);
    gate_function(active.limit_max_discharge_power, supported_modes);
}

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
    convert_enter_service(out, in);
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

dt_sae::RequiredDEROperatingMode convert(sae::RequiredDEROperatingMode in) {
    return in == sae::RequiredDEROperatingMode::GridForming ? dt_sae::RequiredDEROperatingMode::GridForming
                                                            : dt_sae::RequiredDEROperatingMode::GridFollowing;
}

dt_sae::GridConnectionMode convert(sae::GridConnectionMode in) {
    return in == sae::GridConnectionMode::GridIslanded ? dt_sae::GridConnectionMode::GridIslanded
                                                       : dt_sae::GridConnectionMode::GridConnected;
}

void convert(dt_sae::EnterServiceCLRes& out, const sae::EnterServiceCPDRes& in) {
    convert_enter_service(out, in);
}

void convert(dt_sae::ReactivePowerSupportCLRes& out, const sae::ReactivePowerSupportCPDRes& in) {
    convert(out.constant_power_factor.emplace(), in.constant_power_factor);
    convert(out.volt_var.emplace(), in.volt_var);
    convert(out.watt_var.emplace(), in.watt_var);
    convert(out.constant_var.emplace(), in.constant_var);
}

void convert(dt_sae::ActivePowerSupportCLRes& out, const sae::ActivePowerSupportCPDRes& in) {
    convert(out.frequency_droop.emplace(), in.frequency_droop);
    convert(out.volt_watt.emplace(), in.volt_watt);
    convert(out.constant_watt.emplace(), in.constant_watt);
    convert(out.limit_max_discharge_power.emplace(), in.limit_max_discharge_power);
}

void build_der_control_cl_res(dt_sae::DERControlCLRes& out, const DerSaeSetupConfig& config, bool changed_since_cpd,
                              std::uint32_t ev_supported_modes) {
    const auto& der_control = config.der_control;

    // The unchanged path runs once per charge loop message, so it converts and logs nothing: only
    // permit_service is mandatory in EnterServiceCLRes and the others need not be repeated [V2G20-3236].
    if (not changed_since_cpd) {
        out.voltage_trip.reset();
        out.frequency_trip.reset();
        out.reactive_power_support_cl_res.reset();
        out.active_power_support_cl_res.reset();

        out.enter_service_cl_res = {};
        out.enter_service_cl_res.permit_service = der_control.enter_service.permit_service;
        return;
    }

    convert(out.enter_service_cl_res, der_control.enter_service);
    convert(out.voltage_trip.emplace(), der_control.voltage_trip);
    convert(out.frequency_trip.emplace(), der_control.frequency_trip);
    convert(out.reactive_power_support_cl_res.emplace(), der_control.reactive_power_support);
    convert(out.active_power_support_cl_res.emplace(), der_control.active_power_support);

    gate_optional_function(out.voltage_trip, ev_supported_modes);
    gate_optional_function(out.frequency_trip, ev_supported_modes);

    auto& reactive = out.reactive_power_support_cl_res.value();
    gate_optional_function(reactive.constant_power_factor, ev_supported_modes);
    gate_optional_function(reactive.volt_var, ev_supported_modes);
    gate_optional_function(reactive.watt_var, ev_supported_modes);
    gate_optional_function(reactive.constant_var, ev_supported_modes);

    auto& active = out.active_power_support_cl_res.value();
    gate_optional_function(active.frequency_droop, ev_supported_modes);
    gate_optional_function(active.volt_watt, ev_supported_modes);
    gate_optional_function(active.constant_watt, ev_supported_modes);
    gate_optional_function(active.limit_max_discharge_power, ev_supported_modes);
}

} // namespace iso15118::d20::state
