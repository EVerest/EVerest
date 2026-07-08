// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#include <iso15118/d20/state/ac_der_iec_charge_loop.hpp>

#include <iso15118/message/ac_der_iec_charge_loop.hpp>

#include <iso15118/d20/state/power_delivery.hpp>
#include <iso15118/d20/state/session_stop.hpp>
#include <iso15118/detail/d20/context_helper.hpp>
#include <iso15118/detail/d20/state/power_delivery.hpp>
#include <iso15118/detail/helper.hpp>

namespace iso15118::d20::state {

namespace dt = message_20::datatypes;

using Scheduled_DER_Req = dt::DER_Scheduled_AC_CLReqControlMode;
using Dynamic_DER_Req = dt::DER_Dynamic_AC_CLReqControlMode;

using Scheduled_DER_Res = dt::DER_Scheduled_AC_CLResControlMode;
using Dynamic_DER_Res = dt::DER_Dynamic_AC_CLResControlMode;

namespace {
void convert(dt::DsoQSetpoint& out, const iec::DSOQSetpoint& in) {
    out.dso_q_setpoint_value = dt::from_float(in.dso_q_setpoint_value);
    out.dso_q_setpoint_value_L2 = in.dso_q_setpoint_value_l2.has_value()
                                      ? std::make_optional(dt::from_float(in.dso_q_setpoint_value_l2.value()))
                                      : std::nullopt;
    out.dso_q_setpoint_value_L3 = in.dso_q_setpoint_value_l3.has_value()
                                      ? std::make_optional(dt::from_float(in.dso_q_setpoint_value_l3.value()))
                                      : std::nullopt;
    out.pt1_response_reactive_power = in.pt1_response_reactive_power;
    out.step_response_time_constant_reactive_power = dt::from_float(in.step_response_time_constant_reactive_power);
}

void convert(dt::DsoCosPhiSetpoint& out, const iec::DSOCosPhiSetpoint& in) {
    out.dso_cos_phi_setpoint_value = dt::from_float(in.dso_cos_phi_setpoint_value);
    out.dso_cos_phi_setpoint_value_L2 =
        in.dso_cos_phi_setpoint_value_l2.has_value()
            ? std::make_optional(dt::from_float(in.dso_cos_phi_setpoint_value_l2.value()))
            : std::nullopt;
    out.dso_cos_phi_setpoint_value_L3 =
        in.dso_cos_phi_setpoint_value_l3.has_value()
            ? std::make_optional(dt::from_float(in.dso_cos_phi_setpoint_value_l3.value()))
            : std::nullopt;
    out.excitation = static_cast<dt::PowerFactorExcitation>(in.excitation);
    out.pt1_response_reactive_power = in.pt1_response_reactive_power;
    out.step_response_time_constant_reactive_power = dt::from_float(in.step_response_time_constant_reactive_power);
}

void set_dynamic_parameters_in_res(Dynamic_DER_Res& res_mode, const UpdateDynamicModeParameters& parameters,
                                   uint64_t header_timestamp) {
    if (parameters.departure_time) {
        const auto departure_time = static_cast<uint64_t>(parameters.departure_time.value());
        if (departure_time > header_timestamp) {
            res_mode.departure_time = static_cast<uint32_t>(departure_time - header_timestamp);
        }
    }
    res_mode.target_soc = parameters.target_soc;

    // [V2G20-3155]
    if (parameters.min_soc.has_value() and parameters.target_soc.has_value() and
        parameters.min_soc.value() <= parameters.target_soc.value()) {
        res_mode.minimum_soc = parameters.min_soc;
    }
    res_mode.ack_max_delay = 30; // TODO(sl) what to send here and define 30 seconds as const
}

void fill(Scheduled_DER_Res& out, const AcTargetPower& targets, const d20::AcPresentPower& present_power,
          const AcTransferLimits& ac_limits, const IecDerTransferLimits& der_limits,
          const std::optional<dt::DsoQSetpoint>& dso_q_setpoint,
          const std::optional<dt::DsoCosPhiSetpoint>& dso_cos_phi_setpoint) {
    out.target_active_power = targets.target_active_power;
    out.target_active_power_L2 = targets.target_active_power_L2;
    out.target_active_power_L3 = targets.target_active_power_L3;
    out.target_reactive_power = targets.target_reactive_power;
    out.target_reactive_power_L2 = targets.target_reactive_power_L2;
    out.target_reactive_power_L3 = targets.target_reactive_power_L3;
    out.present_active_power = present_power.present_active_power;
    out.present_active_power_L2 = present_power.present_active_power_L2;
    out.present_active_power_L3 = present_power.present_active_power_L3;

    out.max_charge_power = ac_limits.charge_power.max;
    out.max_charge_power_L2 = ac_limits.charge_power_L2.has_value()
                                  ? std::make_optional(ac_limits.charge_power_L2.value().max)
                                  : std::nullopt;
    out.max_charge_power_L3 = ac_limits.charge_power_L3.has_value()
                                  ? std::make_optional(ac_limits.charge_power_L3.value().max)
                                  : std::nullopt;

    out.max_discharge_power = der_limits.max_discharge_power;
    out.max_discharge_power_L2 = der_limits.max_discharge_power_L2;
    out.max_discharge_power_L3 = der_limits.max_discharge_power_L3;
    out.dso_discharge_power = der_limits.dso_discharge_power;
    out.dso_discharge_power_L2 = der_limits.dso_discharge_power_L2;
    out.dso_discharge_power_L3 = der_limits.dso_discharge_power_L3;

    out.dso_q_setpoint = dso_q_setpoint;
    out.dso_cos_phi_setpoint = dso_cos_phi_setpoint;
}

void fill(Dynamic_DER_Res& out, const AcTargetPower& targets, const d20::AcPresentPower& present_power,
          const AcTransferLimits& ac_limits, const IecDerTransferLimits& der_limits,
          const std::optional<dt::DsoQSetpoint>& dso_q_setpoint,
          const std::optional<dt::DsoCosPhiSetpoint>& dso_cos_phi_setpoint) {
    out.target_active_power =
        targets.target_active_power.value_or(dt::RationalNumber{0, 0}); // 0kW if no value is available
    out.target_active_power_L2 = targets.target_active_power_L2;
    out.target_active_power_L3 = targets.target_active_power_L3;
    out.target_reactive_power = targets.target_reactive_power;
    out.target_reactive_power_L2 = targets.target_reactive_power_L2;
    out.target_reactive_power_L3 = targets.target_reactive_power_L3;
    out.present_active_power = present_power.present_active_power;
    out.present_active_power_L2 = present_power.present_active_power_L2;
    out.present_active_power_L3 = present_power.present_active_power_L3;

    out.max_charge_power = ac_limits.charge_power.max;
    out.max_charge_power_L2 = ac_limits.charge_power_L2.has_value()
                                  ? std::make_optional(ac_limits.charge_power_L2.value().max)
                                  : std::nullopt;
    out.max_charge_power_L3 = ac_limits.charge_power_L3.has_value()
                                  ? std::make_optional(ac_limits.charge_power_L3.value().max)
                                  : std::nullopt;

    out.max_discharge_power = der_limits.max_discharge_power;
    out.max_discharge_power_L2 = der_limits.max_discharge_power_L2;
    out.max_discharge_power_L3 = der_limits.max_discharge_power_L3;
    out.dso_discharge_power = der_limits.dso_discharge_power;
    out.dso_discharge_power_L2 = der_limits.dso_discharge_power_L2;
    out.dso_discharge_power_L3 = der_limits.dso_discharge_power_L3;

    out.dso_q_setpoint = dso_q_setpoint;
    out.dso_cos_phi_setpoint = dso_cos_phi_setpoint;
}

message_20::DER_AC_ChargeLoopResponse
handle_request(const message_20::DER_AC_ChargeLoopRequest& req, const d20::Session& session, bool stop, bool pause,
               std::optional<float> target_frequency, const AcTargetPower& target_powers,
               const AcPresentPower& present_powers, const UpdateDynamicModeParameters& dynamic_parameters,
               const AcTransferLimits& ac_limits, const std::optional<IecDerTransferLimits>& der_limits,
               const std::optional<dt::DsoQSetpoint>& dso_q_setpoint,
               const std::optional<dt::DsoCosPhiSetpoint>& dso_cos_phi_setpoint) {

    message_20::DER_AC_ChargeLoopResponse res;

    if (not validate_and_setup_header(res.header, session, req.header.session_id)) {
        return response_with_code(res, dt::ResponseCode::FAILED_UnknownSession);
    }

    const auto& selected_services = session.get_selected_services();
    const auto selected_control_mode = selected_services.selected_control_mode;
    const auto selected_mobility_needs_mode = selected_services.selected_mobility_needs_mode;

    // TODO(SL): How to handle grid_event_condition?

    if (std::holds_alternative<Scheduled_DER_Req>(req.control_mode)) {
        // If the ev sends a false control mode other than the previous selected ones, then the charger should terminate
        // the session
        if (selected_control_mode != dt::ControlMode::Scheduled or not der_limits.has_value()) {
            return response_with_code(res, dt::ResponseCode::FAILED);
        }

        auto& res_mode = res.control_mode.emplace<Scheduled_DER_Res>();
        fill(res_mode, target_powers, present_powers, ac_limits, der_limits.value(), dso_q_setpoint,
             dso_cos_phi_setpoint);

    } else if (std::holds_alternative<Dynamic_DER_Req>(req.control_mode)) {
        // If the ev sends a false control mode other than the previous selected ones, then the charger should terminate
        // the session
        if (selected_control_mode != dt::ControlMode::Dynamic or not der_limits.has_value()) {
            return response_with_code(res, dt::ResponseCode::FAILED);
        }

        auto& res_mode = res.control_mode.emplace<Dynamic_DER_Res>();
        fill(res_mode, target_powers, present_powers, ac_limits, der_limits.value(), dso_q_setpoint,
             dso_cos_phi_setpoint);

        if (selected_mobility_needs_mode == dt::MobilityNeedsMode::ProvidedBySecc) {
            set_dynamic_parameters_in_res(res_mode, dynamic_parameters, res.header.timestamp);
        }
    }

    if (target_frequency.has_value()) {
        res.target_frequency = dt::from_float(target_frequency.value());
    }

    // TODO(sl): Setting EvseStatus, MeterInfo, Receipt

    if (stop) {
        res.status = {0, dt::EvseNotification::Terminate};
    } else if (pause) {
        constexpr auto NotificationMaxDelay = 60; // [V2G20-3308]
        res.status = {NotificationMaxDelay, dt::EvseNotification::Pause};

        // TODO(SL): [V2G20-3318] Decrease notificationmaxdelay based on the reaming seconds if the ev did not perform a
        // pause
    }

    return response_with_code(res, dt::ResponseCode::OK);
}
} // namespace

void AC_DER_IEC_ChargeLoop::enter() {
    m_ctx.log.enter_state("AC_DER_IEC_ChargeLoop");
    dynamic_parameters = m_ctx.cache_dynamic_mode_parameters.value_or(UpdateDynamicModeParameters{});
    target_powers = m_ctx.cache_ac_target_power.value_or(AcTargetPower{});
    present_powers = m_ctx.cache_ac_present_power.value_or(AcPresentPower{});
}

Result AC_DER_IEC_ChargeLoop::feed(Event ev) {

    if (ev == Event::CONTROL_MESSAGE) {
        if (const auto* control_data = m_ctx.get_control_event<StopCharging>()) {
            stop = *control_data;
        } else if (const auto* control_data = m_ctx.get_control_event<PauseCharging>()) {
            pause = *control_data;
        } else if (const auto* control_data = m_ctx.get_control_event<UpdateDynamicModeParameters>()) {
            dynamic_parameters = *control_data;
        } else if (const auto* control_data = m_ctx.get_control_event<AcTargetPower>()) {
            target_powers = *control_data;
        } else if (const auto* control_data = m_ctx.get_control_event<AcPresentPower>()) {
            present_powers = *control_data;
        }

        // Ignore control message
        return {};
    }

    if (ev != Event::V2GTP_MESSAGE) {
        return {};
    }

    const auto variant = m_ctx.pull_request();

    if (const auto* const req = variant->get_if<message_20::PowerDeliveryRequest>()) {
        const auto shutdown_requested = m_ctx.shutdown_requested();

        const auto res = handle_request(*req, m_ctx.session, false, shutdown_requested);

        m_ctx.respond(res);

        if (res.response_code >= dt::ResponseCode::FAILED) {
            m_ctx.session_stopped = true;
            return {};
        }

        // V2G20-3210 -> state machine direct transition (skipped PowerDelivery)
        if (req->charge_progress == dt::Progress::Stop or shutdown_requested) {
            m_ctx.feedback.signal(session::feedback::Signal::CHARGE_LOOP_FINISHED);
            m_ctx.feedback.signal(session::feedback::Signal::AC_OPEN_CONTACTOR);
            return m_ctx.create_state<SessionStop>();
        }

        return {};
    }
    if (const auto* const req = variant->get_if<message_20::DER_AC_ChargeLoopRequest>()) {
        if (first_entry_in_charge_loop) {
            m_ctx.feedback.signal(session::feedback::Signal::CHARGE_LOOP_STARTED);
            first_entry_in_charge_loop = false;
        }

        std::optional<dt::DsoQSetpoint> dso_q_setpoint;
        std::optional<dt::DsoCosPhiSetpoint> dso_cos_phi_setpoint;

        const auto& selected_der_control_functions =
            m_ctx.session.get_selected_services().selected_der_control_functions;

        const auto selected_dsoq =
            selected_der_control_functions.test(static_cast<size_t>(iec::DERControlName::DSOQSetpointProvision));
        const auto selected_dso_cos_phi =
            selected_der_control_functions.test(static_cast<size_t>(iec::DERControlName::DSOCosPhiSetpointProvision));

        const auto& der_functions = m_ctx.session_config.der_iec_setup_config.supported_der_control_functions;

        if (selected_dsoq and der_functions.find(iec::DERControlName::DSOQSetpointProvision) != der_functions.end()) {
            const auto& func_dso_q_setpoint = der_functions.at(iec::DERControlName::DSOQSetpointProvision);
            if (std::holds_alternative<iec::DSOQSetpoint>(func_dso_q_setpoint)) {
                convert(dso_q_setpoint.emplace(), std::get<iec::DSOQSetpoint>(func_dso_q_setpoint));
            }
        }
        if (selected_dso_cos_phi and
            der_functions.find(iec::DERControlName::DSOCosPhiSetpointProvision) != der_functions.end()) {
            const auto& func_dso_cos_phi_setpoint = der_functions.at(iec::DERControlName::DSOCosPhiSetpointProvision);
            if (std::holds_alternative<iec::DSOCosPhiSetpoint>(func_dso_cos_phi_setpoint)) {
                convert(dso_cos_phi_setpoint.emplace(), std::get<iec::DSOCosPhiSetpoint>(func_dso_cos_phi_setpoint));
            }
        }

        const auto res = handle_request(*req, m_ctx.session, stop, pause, target_frequency, target_powers,
                                        present_powers, dynamic_parameters, m_ctx.session_config.ac_limits,
                                        m_ctx.session_config.der_iec_limits, dso_q_setpoint, dso_cos_phi_setpoint);

        m_ctx.respond(res);

        if (res.response_code >= dt::ResponseCode::FAILED) {
            m_ctx.session_stopped = true;
            return {};
        }

        if (const auto* mode = std::get_if<dt::DER_Scheduled_AC_CLReqControlMode>(&req->control_mode)) {
            m_ctx.feedback.ac_charge_loop_req(*mode);
        } else if (const auto* mode = std::get_if<dt::DER_Dynamic_AC_CLReqControlMode>(&req->control_mode)) {
            m_ctx.feedback.ac_charge_loop_req(*mode);
        }

        m_ctx.feedback.ac_charge_loop_req(req->meter_info_requested);
        if (req->display_parameters) {
            m_ctx.feedback.ac_charge_loop_req(*req->display_parameters);
        }

        return {};
    }
    m_ctx.log("Expected PowerDeliveryReq or AC_ChargeLoopRequest! But code type id: %d", variant->get_type());

    // Sequence Error
    const message_20::Type req_type = variant->get_type();
    send_sequence_error(req_type, m_ctx);

    m_ctx.session_stopped = true;
    return {};
}

} // namespace iso15118::d20::state
