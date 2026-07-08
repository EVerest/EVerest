// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#include <iso15118/d20/state/ac_der_iec_charge_parameter_discovery.hpp>
#include <iso15118/d20/state/schedule_exchange.hpp>

#include <iso15118/detail/d20/context_helper.hpp>
#include <iso15118/detail/d20/state/session_stop.hpp>
#include <iso15118/detail/helper.hpp>

#include <iso15118/message/ac_der_iec_charge_parameter_discovery.hpp>

#include <everest/util/vector/fixed_vector.hpp>

namespace iso15118::d20::state {

namespace dt = message_20::datatypes;

namespace {

void convert(dt::FrequencyWatt& out, const iec::FrequencyWatt& in_) {
    out.f_start = dt::from_float(in_.fstart);
    out.f_stop = dt::from_float(in_.fstop);
    out.intentional_delay_f_stop = in_.intentional_delay_fstop;
    out.slope = dt::from_float(in_.slope);
    out.deactivation_time = in_.deactivation_time;
    out.intentional_delay_power_control = in_.intentional_delay_power_control;
    out.power_reference = static_cast<dt::PowerReference>(in_.power_reference);
    out.hysteresis_control = in_.hysteresis_control;
    out.power_up_ramp = in_.power_up_ramp;
    out.pt1_response_active_power = in_.pt1_response_active_power;
    out.step_response_time_constant_active_power = dt::from_float(in_.step_response_time_constant_active_power);
}

void convert(dt::VoltWatt& out, const iec::VoltWatt& in) {
    out.power_reference = static_cast<dt::PowerReference>(in.power_reference);
    out.u_start = dt::from_float(in.u_start);
    out.u_stop = dt::from_float(in.u_stop);
    out.pt1_response_active_power = in.pt1_response_active_power;
    out.step_response_time_constant_active_power = dt::from_float(in.step_response_time_constant_active_power);
    out.intentional_delay_power_control = in.intentional_delay_power_control;
}

void convert(dt::CurveDataPointsList& out, const iec::CurveDataPointsList& in) {
    for (const auto& in_data_tuple : in) {
        auto& out_data_tuple = out.emplace_back();

        out_data_tuple.x_value = dt::from_float(in_data_tuple.x_value);
        out_data_tuple.y_value.set_point_value = dt::from_float(in_data_tuple.y_value.setpoint_value);
        out_data_tuple.y_value.excitation =
            in_data_tuple.y_value.excitation.has_value()
                ? std::make_optional(static_cast<dt::PowerFactorExcitation>(in_data_tuple.y_value.excitation.value()))
                : std::nullopt;
    }
}

void convert(dt::DerCurve& out, const iec::DERCurve& in) {
    out.x_unit = static_cast<dt::CurveDataPointsUnit>(in.x_unit);
    out.y_unit = static_cast<dt::CurveDataPointsUnit>(in.y_unit);
    convert(out.curve_data_points, in.curve_data_points);
    out.min_cos_phi =
        in.min_cos_phi.has_value() ? std::make_optional(dt::from_float(in.min_cos_phi.value())) : std::nullopt;
    out.lock_value_unit = in.lock_value_unit.has_value()
                              ? std::make_optional(static_cast<dt::LockValueUnit>(in.lock_value_unit.value()))
                              : std::nullopt;
    out.lock_in_value =
        in.lock_in_value.has_value() ? std::make_optional(dt::from_float(in.lock_in_value.value())) : std::nullopt;
    out.lock_out_value =
        in.lock_out_value.has_value() ? std::make_optional(dt::from_float(in.lock_out_value.value())) : std::nullopt;
    out.pt1_response_reactive_power = in.pt1_response_reactive_power;
    out.step_response_time_constant_reactive_power = dt::from_float(in.step_response_time_constant_reactive_power);
    out.intentional_delay = in.intentional_delay.has_value()
                                ? std::make_optional(dt::from_float(in.intentional_delay.value()))
                                : std::nullopt;
}

void convert(dt::ZeroCurrent& out, const iec::ZeroCurrent& in) {
    out.over_voltage_limit = in.over_voltage_limit.has_value()
                                 ? std::make_optional(dt::from_float(in.over_voltage_limit.value()))
                                 : std::nullopt;
    out.under_voltage_limit = in.under_voltage_limit.has_value()
                                  ? std::make_optional(dt::from_float(in.under_voltage_limit.value()))
                                  : std::nullopt;
    out.over_voltage_recovery_limit = in.over_voltage_recovery_limit.has_value()
                                          ? std::make_optional(dt::from_float(in.over_voltage_recovery_limit.value()))
                                          : std::nullopt;
    out.under_voltage_recovery_limit = in.under_voltage_recovery_limit.has_value()
                                           ? std::make_optional(dt::from_float(in.under_voltage_recovery_limit.value()))
                                           : std::nullopt;
    out.pt1_response_active_power = in.pt1_response_active_power;
    out.step_response_time_constant_active_power = dt::from_float(in.step_response_time_constant_active_power);
    out.pt1_response_reactive_power = in.pt1_response_reactive_power;
    out.step_response_time_constant_reactive_power = dt::from_float(in.step_response_time_constant_reactive_power);
}

void convert(dt::FaultRideThrough& out, const iec::FaultRideThrough& in) {
    out.voltage_limit_start_frt = dt::from_float(in.voltage_limit_start_frt);
    out.voltage_limit_stop_frt = in.voltage_limit_stop_frt.has_value()
                                     ? std::make_optional(dt::from_float(in.voltage_limit_stop_frt.value()))
                                     : std::nullopt;
    out.voltage_recovery_limit = in.voltage_recovery_limit.has_value()
                                     ? std::make_optional(dt::from_float(in.voltage_recovery_limit.value()))
                                     : std::nullopt;
    out.voltage_ride_through_positive_curve_k_factor =
        in.voltage_ride_through_positive_curve_k_factor.has_value()
            ? std::make_optional(dt::from_float(in.voltage_ride_through_positive_curve_k_factor.value()))
            : std::nullopt;
    out.voltage_ride_through_negative_curve_k_factor =
        in.voltage_ride_through_negative_curve_k_factor.has_value()
            ? std::make_optional(dt::from_float(in.voltage_ride_through_negative_curve_k_factor.value()))
            : std::nullopt;
    out.pt1_response_active_power = in.pt1_response_active_power;
    out.step_response_time_constant_active_power = dt::from_float(in.step_response_time_constant_active_power);
    out.pt1_response_reactive_power = in.pt1_response_reactive_power;
    out.step_response_time_constant_reactive_power = dt::from_float(in.step_response_time_constant_reactive_power);
}

template <typename T> T& get_or_emplace(std::optional<T>& opt) {
    return opt.has_value() ? opt.value() : opt.emplace();
}

dt::DerControl create_der_control(const std::bitset<12>& selected_der_functions,
                                  const std::map<iec::DERControlName, iec::DERControlFunction>& der_functions) {
    dt::DerControl control{};

    for (const auto& [name, function] : der_functions) {
        if (not selected_der_functions.test(static_cast<size_t>(name))) {
            logf_warning("DER function ignored, not in selected set: %u", static_cast<uint32_t>(name));
            continue;
        }

        switch (name) {
        case iec::DERControlName::OverFrequencyWattMode:
            if (not std::holds_alternative<iec::FrequencyWatt>(function)) {
                logf_warning("Unexpected variant type for OverFrequencyWattMode");
                break;
            }
            convert(get_or_emplace(control.active_power_support).over_frequency_watt.emplace(),
                    std::get<iec::FrequencyWatt>(function));
            break;

        case iec::DERControlName::UnderFrequencyWattMode:
            if (not std::holds_alternative<iec::FrequencyWatt>(function)) {
                logf_warning("Unexpected variant type for UnderFrequencyWattMode");
                break;
            }
            convert(get_or_emplace(control.active_power_support).under_frequency_watt.emplace(),
                    std::get<iec::FrequencyWatt>(function));
            break;

        case iec::DERControlName::VoltWattMode:
            if (not std::holds_alternative<iec::VoltWatt>(function)) {
                logf_warning("Unexpected variant type for VoltWattMode");
                break;
            }
            convert(get_or_emplace(control.active_power_support).volt_watt.emplace(),
                    std::get<iec::VoltWatt>(function));
            break;

        case iec::DERControlName::VoltVarMode:
            if (not std::holds_alternative<iec::DERCurve>(function)) {
                logf_warning("Unexpected variant type for VoltVarMode");
                break;
            }
            {
                auto& reactive_power_support = get_or_emplace(control.reactive_power_support);
                reactive_power_support.name = dt::ReactivePowerSupport::ReactivePowerSupportName::VoltVar;
                convert(reactive_power_support.curve, std::get<iec::DERCurve>(function));
            }
            break;

        case iec::DERControlName::WattVarMode:
            if (not std::holds_alternative<iec::DERCurve>(function)) {
                logf_warning("Unexpected variant type for WattVarMode");
                break;
            }
            {
                auto& reactive_power_support = get_or_emplace(control.reactive_power_support);
                reactive_power_support.name = dt::ReactivePowerSupport::ReactivePowerSupportName::WattVar;
                convert(reactive_power_support.curve, std::get<iec::DERCurve>(function));
            }
            break;

        case iec::DERControlName::WattCosPhiMode:
            if (not std::holds_alternative<iec::DERCurve>(function)) {
                logf_warning("Unexpected variant type for WattCosPhiMode");
                break;
            }
            {
                auto& reactive_power_support = get_or_emplace(control.reactive_power_support);
                reactive_power_support.name = dt::ReactivePowerSupport::ReactivePowerSupportName::WattCosPhi;
                convert(reactive_power_support.curve, std::get<iec::DERCurve>(function));
            }
            break;

        case iec::DERControlName::DSOQSetpointProvision:
        case iec::DERControlName::DSOCosPhiSetpointProvision:
            logf_info("Ignoring for now. DSO setpoints will be set in AcChargeLoopRes");
            break;

        case iec::DERControlName::DCInjectionRestriction:
            if (not std::holds_alternative<iec::MaximumLevelDCInjection>(function)) {
                logf_warning("Unexpected variant type for DCInjectionRestriction");
                break;
            }
            control.max_level_dc_injection.emplace(dt::from_float(std::get<iec::MaximumLevelDCInjection>(function)));
            break;

        case iec::DERControlName::ZeroCurrentMode:
            if (not std::holds_alternative<iec::ZeroCurrent>(function)) {
                logf_warning("Unexpected variant type for ZeroCurrentMode");
                break;
            }
            convert(get_or_emplace(control.zero_current), std::get<iec::ZeroCurrent>(function));
            break;

        case iec::DERControlName::OverVoltageFaultRideThroughMode:
            if (not std::holds_alternative<iec::FaultRideThrough>(function)) {
                logf_warning("Unexpected variant type for OverVoltageFaultRideThroughMode");
                break;
            }
            convert(get_or_emplace(control.over_voltage_fault_ride_through), std::get<iec::FaultRideThrough>(function));
            break;

        case iec::DERControlName::UnderVoltageFaultRideThroughMode:
            if (not std::holds_alternative<iec::FaultRideThrough>(function)) {
                logf_warning("Unexpected variant type for UnderVoltageFaultRideThroughMode");
                break;
            }
            convert(get_or_emplace(control.under_voltage_fault_ride_through),
                    std::get<iec::FaultRideThrough>(function));
            break;

        default:
            logf_warning("Unhandled iec::DERControlName enum value: %u", static_cast<uint32_t>(name));
            break;
        }
    }

    return control;
}

message_20::DER_AC_ChargeParameterDiscoveryResponse
handle_request(const message_20::DER_AC_ChargeParameterDiscoveryRequest& req, const d20::Session& session,
               const d20::AcTransferLimits& limits, const d20::AcPresentPower& powers,
               const std::optional<d20::IecDerTransferLimits> der_limits, dt::OperatingMode operating_mode,
               dt::GridConnectionMode grid_connection_mode, const dt::DerControl& der_control) {

    message_20::DER_AC_ChargeParameterDiscoveryResponse res;

    if (not validate_and_setup_header(res.header, session, req.header.session_id)) {
        return response_with_code(res, message_20::datatypes::ResponseCode::FAILED_UnknownSession);
    }

    if (not der_limits.has_value()) {
        logf_error("No DER limits are provided. Shutdown the session");
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

    const auto& der_limits_ = der_limits.value();
    mode.nominal_charge_power = der_limits_.nominal_charge_power;
    mode.nominal_charge_power_L2 = der_limits_.nominal_charge_power_L2;
    mode.nominal_charge_power_L3 = der_limits_.nominal_charge_power_L3;
    mode.nominal_discharge_power = der_limits_.nominal_discharge_power;
    mode.nominal_discharge_power_L2 = der_limits_.nominal_discharge_power_L2;
    mode.nominal_discharge_power_L3 = der_limits_.nominal_discharge_power_L3;
    mode.max_discharge_power = der_limits_.max_discharge_power;
    mode.max_discharge_power_L2 = der_limits_.max_discharge_power_L2;
    mode.max_discharge_power_L3 = der_limits_.max_discharge_power_L3;

    mode.operating_mode = operating_mode;
    mode.grid_connection_mode = grid_connection_mode;
    mode.der_control = der_control;

    return response_with_code(res, message_20::datatypes::ResponseCode::OK);
}

} // namespace

void AC_DER_IEC_ChargeParameterDiscovery::enter() {
    m_ctx.log.enter_state("AC_DER_IEC_ChargeParameterDiscovery");
    present_powers = m_ctx.cache_ac_present_power.value_or(AcPresentPower{});
}

Result AC_DER_IEC_ChargeParameterDiscovery::feed(Event ev) {

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

    if (const auto* const req = variant->get_if<message_20::DER_AC_ChargeParameterDiscoveryRequest>()) {

        m_ctx.session_ev_info.ev_transfer_limits.emplace<dt::DER_AC_CPDReqEnergyTransferMode>(req->transfer_mode);

        // TODO(SL): Should be not a problem but maybe its better to assign the values directly
        const auto operating_mode =
            static_cast<dt::OperatingMode>(m_ctx.session_config.der_iec_setup_config.operating_mode);
        const auto grid_connection_mode =
            static_cast<dt::GridConnectionMode>(m_ctx.session_config.der_iec_setup_config.grid_connection_mode);

        const auto& der_functions = m_ctx.session_config.der_iec_setup_config.supported_der_control_functions;
        const auto& selected_services = m_ctx.session.get_selected_services();

        const auto der_control = create_der_control(selected_services.selected_der_control_functions, der_functions);

        const auto res =
            handle_request(*req, m_ctx.session, m_ctx.session_config.ac_limits, present_powers,
                           m_ctx.session_config.der_iec_limits, operating_mode, grid_connection_mode, der_control);

        m_ctx.respond(res);

        if (res.response_code >= message_20::datatypes::ResponseCode::FAILED) {
            m_ctx.session_stopped = true;
            return {};
        }

        // TODO(SL): Check [V2G20-3154]: It is possible that the EV sends a ServiceDiscoveryReq if the settings from
        // evse is not accepted from the ev.

        m_ctx.feedback.ac_limits(req->transfer_mode);

        if (req->transfer_mode.processing == dt::Processing::Finished) {
            return m_ctx.create_state<ScheduleExchange>(); // [V2G20-3151]
        }
        return {}; // [V2G20-3150]: Stay in the state because ev set processing to Ongoing
    }
    if (const auto* const req = variant->get_if<message_20::SessionStopRequest>()) {
        const auto res = handle_request(*req, m_ctx.session);

        m_ctx.respond(res);
        m_ctx.session_stopped = true;

        return {};
    }
    m_ctx.log("expected AC_ChargeParameterDiscovery! But code type id: %d", variant->get_type());
    m_ctx.session_stopped = true;

    // Sequence Error
    const message_20::Type req_type = variant->get_type();
    send_sequence_error(req_type, m_ctx);

    return {};
}

} // namespace iso15118::d20::state
