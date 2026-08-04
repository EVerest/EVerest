// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#include <iso15118/d20/state/ac_der_sae_charge_parameter_discovery.hpp>
#include <iso15118/d20/state/schedule_exchange.hpp>

#include <iso15118/detail/d20/context_helper.hpp>
#include <iso15118/detail/d20/state/session_stop.hpp>
#include <iso15118/detail/helper.hpp>

#include <iso15118/message/ac_der_sae_charge_parameter_discovery.hpp>

#include <everest/util/vector/fixed_vector.hpp>

namespace iso15118::d20::state {

namespace dt = message_20::datatypes;

namespace {

// void convert(dt::FrequencyWatt& out, const iec::FrequencyWatt& in_) {
//     out.f_start = dt::from_float(in_.fstart);
//     out.f_stop = dt::from_float(in_.fstop);
//     out.intentional_delay_f_stop = in_.intentional_delay_fstop;
//     out.slope = dt::from_float(in_.slope);
//     out.deactivation_time = in_.deactivation_time;
//     out.intentional_delay_power_control = in_.intentional_delay_power_control;
//     out.power_reference = static_cast<dt::PowerReference>(in_.power_reference);
//     out.hysteresis_control = in_.hysteresis_control;
//     out.power_up_ramp = in_.power_up_ramp;
//     out.pt1_response_active_power = in_.pt1_response_active_power;
//     out.step_response_time_constant_active_power = dt::from_float(in_.step_response_time_constant_active_power);
// }

// void convert(dt::VoltWatt& out, const iec::VoltWatt& in) {
//     out.power_reference = static_cast<dt::PowerReference>(in.power_reference);
//     out.u_start = dt::from_float(in.u_start);
//     out.u_stop = dt::from_float(in.u_stop);
//     out.pt1_response_active_power = in.pt1_response_active_power;
//     out.step_response_time_constant_active_power = dt::from_float(in.step_response_time_constant_active_power);
//     out.intentional_delay_power_control = in.intentional_delay_power_control;
// }

// void convert(dt::CurveDataPointsList& out, const iec::CurveDataPointsList& in) {
//     for (const auto& in_data_tuple : in) {
//         auto& out_data_tuple = out.emplace_back();

//         out_data_tuple.x_value = dt::from_float(in_data_tuple.x_value);
//         out_data_tuple.y_value.set_point_value = dt::from_float(in_data_tuple.y_value.setpoint_value);
//         out_data_tuple.y_value.excitation =
//             in_data_tuple.y_value.excitation.has_value()
//                 ?
//                 std::make_optional(static_cast<dt::PowerFactorExcitation>(in_data_tuple.y_value.excitation.value()))
//                 : std::nullopt;
//     }
// }

// void convert(dt::DerCurve& out, const iec::DERCurve& in) {
//     out.x_unit = static_cast<dt::CurveDataPointsUnit>(in.x_unit);
//     out.y_unit = static_cast<dt::CurveDataPointsUnit>(in.y_unit);
//     convert(out.curve_data_points, in.curve_data_points);
//     out.min_cos_phi =
//         in.min_cos_phi.has_value() ? std::make_optional(dt::from_float(in.min_cos_phi.value())) : std::nullopt;
//     out.lock_value_unit = in.lock_value_unit.has_value()
//                               ? std::make_optional(static_cast<dt::LockValueUnit>(in.lock_value_unit.value()))
//                               : std::nullopt;
//     out.lock_in_value =
//         in.lock_in_value.has_value() ? std::make_optional(dt::from_float(in.lock_in_value.value())) : std::nullopt;
//     out.lock_out_value =
//         in.lock_out_value.has_value() ? std::make_optional(dt::from_float(in.lock_out_value.value())) : std::nullopt;
//     out.pt1_response_reactive_power = in.pt1_response_reactive_power;
//     out.step_response_time_constant_reactive_power = dt::from_float(in.step_response_time_constant_reactive_power);
//     out.intentional_delay = in.intentional_delay.has_value()
//                                 ? std::make_optional(dt::from_float(in.intentional_delay.value()))
//                                 : std::nullopt;
// }

// void convert(dt::ZeroCurrent& out, const iec::ZeroCurrent& in) {
//     out.over_voltage_limit = in.over_voltage_limit.has_value()
//                                  ? std::make_optional(dt::from_float(in.over_voltage_limit.value()))
//                                  : std::nullopt;
//     out.under_voltage_limit = in.under_voltage_limit.has_value()
//                                   ? std::make_optional(dt::from_float(in.under_voltage_limit.value()))
//                                   : std::nullopt;
//     out.over_voltage_recovery_limit = in.over_voltage_recovery_limit.has_value()
//                                           ?
//                                           std::make_optional(dt::from_float(in.over_voltage_recovery_limit.value()))
//                                           : std::nullopt;
//     out.under_voltage_recovery_limit = in.under_voltage_recovery_limit.has_value()
//                                            ?
//                                            std::make_optional(dt::from_float(in.under_voltage_recovery_limit.value()))
//                                            : std::nullopt;
//     out.pt1_response_active_power = in.pt1_response_active_power;
//     out.step_response_time_constant_active_power = dt::from_float(in.step_response_time_constant_active_power);
//     out.pt1_response_reactive_power = in.pt1_response_reactive_power;
//     out.step_response_time_constant_reactive_power = dt::from_float(in.step_response_time_constant_reactive_power);
// }

// void convert(dt::FaultRideThrough& out, const iec::FaultRideThrough& in) {
//     out.voltage_limit_start_frt = dt::from_float(in.voltage_limit_start_frt);
//     out.voltage_limit_stop_frt = in.voltage_limit_stop_frt.has_value()
//                                      ? std::make_optional(dt::from_float(in.voltage_limit_stop_frt.value()))
//                                      : std::nullopt;
//     out.voltage_recovery_limit = in.voltage_recovery_limit.has_value()
//                                      ? std::make_optional(dt::from_float(in.voltage_recovery_limit.value()))
//                                      : std::nullopt;
//     out.voltage_ride_through_positive_curve_k_factor =
//         in.voltage_ride_through_positive_curve_k_factor.has_value()
//             ? std::make_optional(dt::from_float(in.voltage_ride_through_positive_curve_k_factor.value()))
//             : std::nullopt;
//     out.voltage_ride_through_negative_curve_k_factor =
//         in.voltage_ride_through_negative_curve_k_factor.has_value()
//             ? std::make_optional(dt::from_float(in.voltage_ride_through_negative_curve_k_factor.value()))
//             : std::nullopt;
//     out.pt1_response_active_power = in.pt1_response_active_power;
//     out.step_response_time_constant_active_power = dt::from_float(in.step_response_time_constant_active_power);
//     out.pt1_response_reactive_power = in.pt1_response_reactive_power;
//     out.step_response_time_constant_reactive_power = dt::from_float(in.step_response_time_constant_reactive_power);
// }

// template <typename T> T& get_or_emplace(std::optional<T>& opt) {
//     return opt.has_value() ? opt.value() : opt.emplace();
// }

// dt::DerControl create_der_control(const std::bitset<12>& selected_der_functions,
//                                   const std::map<iec::DERControlName, iec::DERControlFunction>& der_functions) {
//     dt::DerControl control{};

//     for (const auto& [name, function] : der_functions) {
//         if (not selected_der_functions.test(static_cast<size_t>(name))) {
//             logf_warning("DER function ignored, not in selected set: %u", static_cast<uint32_t>(name));
//             continue;
//         }

//         switch (name) {
//         case iec::DERControlName::OverFrequencyWattMode:
//             if (not std::holds_alternative<iec::FrequencyWatt>(function)) {
//                 logf_warning("Unexpected variant type for OverFrequencyWattMode");
//                 break;
//             }
//             convert(get_or_emplace(control.active_power_support).over_frequency_watt.emplace(),
//                     std::get<iec::FrequencyWatt>(function));
//             break;

//         case iec::DERControlName::UnderFrequencyWattMode:
//             if (not std::holds_alternative<iec::FrequencyWatt>(function)) {
//                 logf_warning("Unexpected variant type for UnderFrequencyWattMode");
//                 break;
//             }
//             convert(get_or_emplace(control.active_power_support).under_frequency_watt.emplace(),
//                     std::get<iec::FrequencyWatt>(function));
//             break;

//         case iec::DERControlName::VoltWattMode:
//             if (not std::holds_alternative<iec::VoltWatt>(function)) {
//                 logf_warning("Unexpected variant type for VoltWattMode");
//                 break;
//             }
//             convert(get_or_emplace(control.active_power_support).volt_watt.emplace(),
//                     std::get<iec::VoltWatt>(function));
//             break;

//         case iec::DERControlName::VoltVarMode:
//             if (not std::holds_alternative<iec::DERCurve>(function)) {
//                 logf_warning("Unexpected variant type for VoltVarMode");
//                 break;
//             }
//             {
//                 auto& reactive_power_support = get_or_emplace(control.reactive_power_support);
//                 reactive_power_support.name = dt::ReactivePowerSupport::ReactivePowerSupportName::VoltVar;
//                 convert(reactive_power_support.curve, std::get<iec::DERCurve>(function));
//             }
//             break;

//         case iec::DERControlName::WattVarMode:
//             if (not std::holds_alternative<iec::DERCurve>(function)) {
//                 logf_warning("Unexpected variant type for WattVarMode");
//                 break;
//             }
//             {
//                 auto& reactive_power_support = get_or_emplace(control.reactive_power_support);
//                 reactive_power_support.name = dt::ReactivePowerSupport::ReactivePowerSupportName::WattVar;
//                 convert(reactive_power_support.curve, std::get<iec::DERCurve>(function));
//             }
//             break;

//         case iec::DERControlName::WattCosPhiMode:
//             if (not std::holds_alternative<iec::DERCurve>(function)) {
//                 logf_warning("Unexpected variant type for WattCosPhiMode");
//                 break;
//             }
//             {
//                 auto& reactive_power_support = get_or_emplace(control.reactive_power_support);
//                 reactive_power_support.name = dt::ReactivePowerSupport::ReactivePowerSupportName::WattCosPhi;
//                 convert(reactive_power_support.curve, std::get<iec::DERCurve>(function));
//             }
//             break;

//         case iec::DERControlName::DSOQSetpointProvision:
//         case iec::DERControlName::DSOCosPhiSetpointProvision:
//             logf_info("Ignoring for now. DSO setpoints will be set in AcChargeLoopRes");
//             break;

//         case iec::DERControlName::DCInjectionRestriction:
//             if (not std::holds_alternative<iec::MaximumLevelDCInjection>(function)) {
//                 logf_warning("Unexpected variant type for DCInjectionRestriction");
//                 break;
//             }
//             control.max_level_dc_injection.emplace(dt::from_float(std::get<iec::MaximumLevelDCInjection>(function)));
//             break;

//         case iec::DERControlName::ZeroCurrentMode:
//             if (not std::holds_alternative<iec::ZeroCurrent>(function)) {
//                 logf_warning("Unexpected variant type for ZeroCurrentMode");
//                 break;
//             }
//             convert(get_or_emplace(control.zero_current), std::get<iec::ZeroCurrent>(function));
//             break;

//         case iec::DERControlName::OverVoltageFaultRideThroughMode:
//             if (not std::holds_alternative<iec::FaultRideThrough>(function)) {
//                 logf_warning("Unexpected variant type for OverVoltageFaultRideThroughMode");
//                 break;
//             }
//             convert(get_or_emplace(control.over_voltage_fault_ride_through),
//             std::get<iec::FaultRideThrough>(function)); break;

//         case iec::DERControlName::UnderVoltageFaultRideThroughMode:
//             if (not std::holds_alternative<iec::FaultRideThrough>(function)) {
//                 logf_warning("Unexpected variant type for UnderVoltageFaultRideThroughMode");
//                 break;
//             }
//             convert(get_or_emplace(control.under_voltage_fault_ride_through),
//                     std::get<iec::FaultRideThrough>(function));
//             break;

//         default:
//             logf_warning("Unhandled iec::DERControlName enum value: %u", static_cast<uint32_t>(name));
//             break;
//         }
//     }

//     return control;
// }
//

void convert_sae_limits(dt::sae::DER_SAE_AC_CPDResEnergyTransferMode& out, const d20::SaeDerTransferLimits& in) {
    out.nominal_charge_power = in.nominal_charge_power;
    out.nominal_charge_power_L2 = in.nominal_charge_power_L2;
    out.nominal_charge_power_L3 = in.nominal_charge_power_L3;
    out.nominal_discharge_power = in.nominal_discharge_power;
    out.nominal_discharge_power_L2 = in.nominal_discharge_power_L2;
    out.nominal_discharge_power_L3 = in.nominal_discharge_power_L3;
    out.maximum_discharge_power = in.max_discharge_power;
    out.maximum_discharge_power_L2 = in.max_discharge_power_L2;
    out.maximum_discharge_power_L3 = in.max_discharge_power_L3;

    const auto& reactive_power_limits = in.reactive_power_limits;
    out.reactive_power_limits.maximum_var_absorption_during_charging =
        reactive_power_limits.maximum_var_absorption_during_charging;
    out.reactive_power_limits.maximum_var_absorption_during_charging_L2 =
        reactive_power_limits.maximum_var_absorption_during_charging_L2;
    out.reactive_power_limits.maximum_var_absorption_during_charging_L3 =
        reactive_power_limits.maximum_var_absorption_during_charging_L3;
    out.reactive_power_limits.maximum_var_injection_during_charging =
        reactive_power_limits.maximum_var_injection_during_charging;
    out.reactive_power_limits.maximum_var_injection_during_charging_L2 =
        reactive_power_limits.maximum_var_injection_during_charging_L2;
    out.reactive_power_limits.maximum_var_injection_during_charging_L3 =
        reactive_power_limits.maximum_var_injection_during_charging_L3;
    out.reactive_power_limits.maximum_var_absorption_during_discharging =
        reactive_power_limits.maximum_var_absorption_during_discharging;
    out.reactive_power_limits.maximum_var_absorption_during_discharging_L2 =
        reactive_power_limits.maximum_var_absorption_during_discharging_L2;
    out.reactive_power_limits.maximum_var_absorption_during_discharging_L3 =
        reactive_power_limits.maximum_var_absorption_during_discharging_L3;
    out.reactive_power_limits.maximum_var_injection_during_discharging =
        reactive_power_limits.maximum_var_injection_during_discharging;
    out.reactive_power_limits.maximum_var_injection_during_discharging_L2 =
        reactive_power_limits.maximum_var_injection_during_discharging_L2;
    out.reactive_power_limits.maximum_var_injection_during_discharging_L3 =
        reactive_power_limits.maximum_var_injection_during_discharging_L3;

    const auto& grid_limits = in.grid_limits;
    out.grid_limits.nominal_frequency = grid_limits.nominal_frequency;
    out.grid_limits.nominal_voltage = grid_limits.nominal_voltage;
    out.grid_limits.nominal_voltage_offset = grid_limits.nominal_voltage_offset;
    out.grid_limits.min_frequency = grid_limits.min_frequency;
    out.grid_limits.max_frequency = grid_limits.max_frequency;
    out.grid_limits.maximum_voltage = grid_limits.maximum_voltage;
    out.grid_limits.minimum_voltage = grid_limits.minimum_voltage;
}

constexpr auto MAX_FUNCTIONS = message_20::to_underlying_value(sae::DerBitMapFunctions::WattVarFunction) + 1;

bool check_supported_evse_functions([[maybe_unused]] sae::DERControl& control, uint32_t ev_supported_modes,
                                    uint32_t& evse_supported_modes) {

    const auto ev_supported_functions = std::bitset<MAX_FUNCTIONS>(ev_supported_modes);
    std::bitset<MAX_FUNCTIONS> evse_supported_functions;

    if (not(ev_supported_functions.test(static_cast<size_t>(sae::DerBitMapFunctions::ChargeFunction)) and
            ev_supported_functions.test(static_cast<size_t>(sae::DerBitMapFunctions::DischargeFunction)))) {
        logf_error("ChargeFunction and DischargeFunction needs to be enabled from the ev");
        return false;
    }

    evse_supported_functions.set(static_cast<size_t>(sae::DerBitMapFunctions::ChargeFunction), true);
    evse_supported_functions.set(static_cast<size_t>(sae::DerBitMapFunctions::DischargeFunction), true);

    // control.

    evse_supported_modes = evse_supported_functions.to_ulong();

    return true;
}

bool check_enabled_modes(const sae::DERControl& control, const uint32_t& ev_enabled_modes) {
    std::bitset<MAX_FUNCTIONS> evse_enabled_functions;

    evse_enabled_functions.set(static_cast<size_t>(sae::DerBitMapFunctions::ChargeFunction), true);
    evse_enabled_functions.set(static_cast<size_t>(sae::DerBitMapFunctions::DischargeFunction), true);

    evse_enabled_functions.set(static_cast<size_t>(sae::DerBitMapFunctions::EnterService),
                               control.enter_service.permit_service);

    if (control.reactive_power_support.constant_power_factor.enable and
        control.reactive_power_support.constant_power_factor.power_factor_excitation ==
            sae::PowerFactorExcitation::UnderExcited) {
        evse_enabled_functions.set(
            static_cast<size_t>(sae::DerBitMapFunctions::ConstantPowerFactorUnderExcitedFunction));
    } else if (control.reactive_power_support.constant_power_factor.enable and
               control.reactive_power_support.constant_power_factor.power_factor_excitation ==
                   sae::PowerFactorExcitation::OverExcited) {
        evse_enabled_functions.set(
            static_cast<size_t>(sae::DerBitMapFunctions::ConstantPowerFactorOverExcitedFunction));
    }

    // ConstantReactivePowerFunction = 6,
    // ConstantActivePowerFunction = 7,
    // FrequencyDroopFunction = 8,
    // HighFrequencyMayTripFunction = 10,
    // HighFrequencyMustTripFunction = 11,
    // HighVoltageMayTripFunction = 12,
    // HighVoltageMomentaryCessationFunction = 13,
    // HighVoltageMustTripFunction = 14,
    // LowFrequencyMayTripFunction = 15,
    // LowFrequencyMustTripFunction = 16,
    // LowVoltageMayTripFunction = 17,
    // LowVoltageMomentaryCessationFunction = 18,
    // LowVoltageMustTripFunction = 19,
    // LimitMaximumActiveDischargePowerFunction = 20,
    // EVSETargetReactivePowerFunction = 21,
    // EVSETargetActivePowerFunction = 22,
    // VoltVarFunction = 23,
    // VoltWattFunction = 24,
    // WattVarFunction = 26,

    const bool equal_functions = ev_enabled_modes == evse_enabled_functions.to_ulong();
    if (not equal_functions) {
        logf_info("EV enabled functions [%u != %u] EVSE enabled functions", ev_enabled_modes,
                  evse_enabled_functions.to_ulong());
    }

    return equal_functions;
}

message_20::DER_SAE_AC_ChargeParameterDiscoveryResponse
handle_request(const message_20::DER_SAE_AC_ChargeParameterDiscoveryRequest& req, const d20::Session& session,
               const d20::AcTransferLimits& limits, const d20::AcPresentPower& powers,
               const std::optional<d20::SaeDerTransferLimits>& sae_limits,
               const std::optional<DerSaeSetupConfig>& config, uint32_t evse_supported_modes) {

    message_20::DER_SAE_AC_ChargeParameterDiscoveryResponse res;

    if (not validate_and_setup_header(res.header, session, req.header.session_id)) {
        return response_with_code(res, message_20::datatypes::ResponseCode::FAILED_UnknownSession);
    }

    if (not sae_limits.has_value()) {
        logf_error("No SAE limits are provided. Shutdown the session");
        return response_with_code(res, dt::ResponseCode::FAILED_WrongChargeParameter);
    }

    if (not config.has_value()) {
        logf_error("No SAE DER control values are provided. Shutdown the session");
        return response_with_code(res, dt::ResponseCode::FAILED_WrongChargeParameter);
    }

    // NOTE(SL): At this point, it's clear that it can only be DER TransferMode

    auto& mode = res.transfer_mode;
    mode.min_charge_power = limits.charge_power.min;
    mode.max_charge_power = limits.charge_power.max;

    if (limits.charge_power_L2.has_value()) {
        mode.min_charge_power_L2 = limits.charge_power_L2.value().min;
        mode.max_charge_power_L2 = limits.charge_power_L2.value().max;
    }

    if (limits.charge_power_L3.has_value()) {
        mode.min_charge_power_L3 = limits.charge_power_L3.value().min;
        mode.max_charge_power_L3 = limits.charge_power_L3.value().max;
    }

    mode.nominal_frequency = limits.nominal_frequency;
    mode.max_power_asymmetry = limits.max_power_asymmetry;
    mode.power_ramp_limitation = limits.power_ramp_limitation;
    mode.present_active_power = powers.present_active_power;
    mode.present_active_power_L2 = powers.present_active_power_L2;
    mode.present_active_power_L3 = powers.present_active_power_L3;

    // ---------------------------------------------------------------

    // mode.status
    const auto& sae_config = config.value();

    // TODO(SL):
    // 1. Based on req.transfer_mode.supported_modes and sae_config.der_control set mode.der_control_cpd_res + save the
    // enabled in the state
    auto der_control = sae_config.der_control;

    if (not check_supported_evse_functions(der_control, req.transfer_mode.supported_modes, evse_supported_modes)) {
        logf_error(""); // TODO(SL): Write prober error message
        return response_with_code(res, dt::ResponseCode::FAILED);
    }

    // convert(mode.der_control_cpd_res, der_control);

    // 2. Check mode.der_control_cpd_res with req.transfer_mode.enabled_modes -> Same then Processing::Finished + save
    // the agreed/enabled
    mode.processing = check_enabled_modes(der_control, req.transfer_mode.enabled_modes) ? dt::Processing::Finished
                                                                                        : dt::Processing::Ongoing;
    // --------------------------------------------------------

    convert_sae_limits(mode, sae_limits.value());

    if (sae_config.required_der_operating_mode == sae::RequiredDEROperatingMode::GridFollowing) {
        mode.required_der_operating_mode = dt::sae::RequiredDEROperatingMode::GridFollowing;
    } else if (sae_config.required_der_operating_mode == sae::RequiredDEROperatingMode::GridForming) {
        mode.required_der_operating_mode = dt::sae::RequiredDEROperatingMode::GridForming;
    }

    if (sae_config.grid_connection_mode == sae::GridConnectionMode::GridConnected) {
        mode.grid_connection_mode = dt::sae::GridConnectionMode::GridConnected;
    } else if (sae_config.grid_connection_mode == sae::GridConnectionMode::GridIslanded) {
        mode.grid_connection_mode = dt::sae::GridConnectionMode::GridIslanded;
    }

    mode.update_time = sae_config.der_control_update_time;

    return response_with_code(res, message_20::datatypes::ResponseCode::OK);
}

} // namespace

void AC_DER_SAE_ChargeParameterDiscovery::enter() {
    logf_debug("Enter state: AC_DER_SAE_ChargeParameterDiscovery");
    present_powers = m_ctx.cache_ac_present_power.value_or(AcPresentPower{});
    evse_supported_modes = 0;
}

Result AC_DER_SAE_ChargeParameterDiscovery::feed(Event ev) {

    if (ev == Event::CONTROL_MESSAGE) {
        if (const auto* control_data = m_ctx.get_control_event<AcPresentPower>()) {
            present_powers = *control_data;
        }
        return {};
    }

    if (ev != Event::V2GTP_MESSAGE) {
        return {};
    }

    const auto variant = m_ctx.pull_request();

    if (const auto* const req = variant->get_if<message_20::DER_SAE_AC_ChargeParameterDiscoveryRequest>()) {

        // m_ctx.session_ev_info.ev_transfer_limits.emplace<dt::DER_AC_CPDReqEnergyTransferMode>(req->transfer_mode);

        const auto res = handle_request(*req, m_ctx.session, m_ctx.session_config.ac_limits, present_powers,
                                        m_ctx.session_config.der_sae_limits, m_ctx.session_config.der_sae_setup_config,
                                        evse_supported_modes);

        m_ctx.respond(res);

        if (res.response_code >= message_20::datatypes::ResponseCode::FAILED) {
            m_ctx.session_stopped = true;
            return {};
        }

        // TODO(SL): Check [V2G20-]: It is possible that the EV sends a ServiceDiscoveryReq if the settings from
        // evse is not accepted from the ev.

        // m_ctx.feedback.ac_limits(req->transfer_mode);

        if (req->transfer_mode.processing == dt::Processing::Finished and
            res.transfer_mode.processing == dt::Processing::Finished) {
            return m_ctx.create_state<ScheduleExchange>(); // [V2G20-]
        }
        return {}; // [V2G20-3150]: Stay in the state because ev set processing to Ongoing
    }

    if (const auto* const req = variant->get_if<message_20::SessionStopRequest>()) {
        const auto res = handle_request(*req, m_ctx.session);

        m_ctx.respond(res);
        m_ctx.session_stopped = true;

        return {};
    }

    logf_warning("expected DER_SAE_AC_ChargeParameterDiscovery! But code type id: %d", variant->get_type());
    m_ctx.session_stopped = true;

    // Sequence Error
    const message_20::Type req_type = variant->get_type();
    send_sequence_error(req_type, m_ctx);

    return {};
}

} // namespace iso15118::d20::state
