// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#include <iso15118/d20/state/ac_der_sae_charge_loop.hpp>

#include <iso15118/message/ac_der_sae_charge_loop.hpp>

#include <iso15118/d20/state/power_delivery.hpp>
#include <iso15118/d20/state/session_stop.hpp>
#include <iso15118/detail/d20/context_helper.hpp>
#include <iso15118/detail/d20/state/ac_der_sae_charge_loop.hpp>
#include <iso15118/detail/d20/state/ac_der_sae_convert.hpp>
#include <iso15118/detail/d20/state/power_delivery.hpp>
#include <iso15118/detail/helper.hpp>

#include <cstdint>

namespace iso15118::d20::state {

namespace dt = message_20::datatypes;

using Scheduled_DER_Req = dt::sae::DER_Scheduled_AC_CLReqControlMode;
using Dynamic_DER_Req = dt::sae::DER_Dynamic_AC_CLReqControlMode;

using Scheduled_DER_Res = dt::sae::DER_Scheduled_AC_CLResControlMode;
using Dynamic_DER_Res = dt::sae::DER_Dynamic_AC_CLResControlMode;

namespace {

using DerFn = sae::DerBitMapFunctions;

void set_dynamic_parameters_in_res(Dynamic_DER_Res& res_mode, const UpdateDynamicModeParameters& parameters,
                                   std::uint64_t header_timestamp) {
    if (parameters.departure_time) {
        const auto departure_time = static_cast<std::uint64_t>(parameters.departure_time.value());
        if (departure_time > header_timestamp) {
            res_mode.departure_time = static_cast<std::uint32_t>(departure_time - header_timestamp);
        }
    }
    res_mode.target_soc = parameters.target_soc;

    // The minimum soc is only sent when it does not exceed the target soc.
    if (parameters.min_soc.has_value() and parameters.target_soc.has_value() and
        parameters.min_soc.value() <= parameters.target_soc.value()) {
        res_mode.minimum_soc = parameters.min_soc;
    }
    res_mode.ack_max_delay = 30; // TODO(mlitre) what to send here and define 30 seconds as const
}

std::optional<dt::RationalNumber> phase_maximum(const std::optional<Limit<dt::RationalNumber>>& limit) {
    if (not limit.has_value()) {
        return std::nullopt;
    }
    return limit.value().max;
}

// Everything both response control modes share. The target active power differs: optional in scheduled mode,
// mandatory in dynamic mode.
template <typename Res>
void fill_shared(Res& out, const AcTargetPower& targets, const d20::AcPresentPower& present_power,
                 const d20::AcTransferLimits& ac_limits, const d20::SaeDerTransferLimits& sae_limits,
                 const d20::DerSaeSetupConfig& der_config, bool changed_since_cpd, std::uint32_t ev_supported_modes) {
    build_der_control_cl_res(out.der_control_cl_res, der_config, changed_since_cpd, ev_supported_modes);

    out.present_active_power = present_power.present_active_power;
    out.present_active_power_L2 = present_power.present_active_power_L2;
    out.present_active_power_L3 = present_power.present_active_power_L3;

    out.evse_maximum_charge_power = ac_limits.charge_power.max;
    out.evse_maximum_charge_power_L2 = phase_maximum(ac_limits.charge_power_L2);
    out.evse_maximum_charge_power_L3 = phase_maximum(ac_limits.charge_power_L3);

    out.evse_maximum_discharge_power = sae_limits.max_discharge_power;
    out.evse_maximum_discharge_power_L2 = sae_limits.max_discharge_power_L2;
    out.evse_maximum_discharge_power_L3 = sae_limits.max_discharge_power_L3;

    // The target power triples have no Enable in the charge parameter discovery, so the supported modes bitmap
    // is the only gate on them.
    if (is_function_set(ev_supported_modes, DerFn::EVSETargetReactivePowerFunction)) {
        out.target_reactive_power = targets.target_reactive_power;
        out.target_reactive_power_L2 = targets.target_reactive_power_L2;
        out.target_reactive_power_L3 = targets.target_reactive_power_L3;
    }

    // required_der_operating_mode and grid_connection_mode are set in the charge parameter discovery.
    // Resending them mid-session is not implemented, so a change of either does not reach the EV.
}

void fill(Scheduled_DER_Res& out, const AcTargetPower& targets, const d20::AcPresentPower& present_power,
          const d20::AcTransferLimits& ac_limits, const d20::SaeDerTransferLimits& sae_limits,
          const d20::DerSaeSetupConfig& der_config, bool changed_since_cpd, std::uint32_t ev_supported_modes) {
    fill_shared(out, targets, present_power, ac_limits, sae_limits, der_config, changed_since_cpd, ev_supported_modes);

    // Optional in scheduled mode: the target power comes from the negotiated schedule.
    if (is_function_set(ev_supported_modes, DerFn::EVSETargetActivePowerFunction)) {
        out.target_active_power = targets.target_active_power;
        out.target_active_power_L2 = targets.target_active_power_L2;
        out.target_active_power_L3 = targets.target_active_power_L3;
    }
}

void fill(Dynamic_DER_Res& out, const AcTargetPower& targets, const d20::AcPresentPower& present_power,
          const d20::AcTransferLimits& ac_limits, const d20::SaeDerTransferLimits& sae_limits,
          const d20::DerSaeSetupConfig& der_config, bool changed_since_cpd, std::uint32_t ev_supported_modes,
          SaeChargeLoopLogState& log_state) {
    fill_shared(out, targets, present_power, ac_limits, sae_limits, der_config, changed_since_cpd, ev_supported_modes);

    const auto target_active_power_declared = is_function_set(ev_supported_modes, DerFn::EVSETargetActivePowerFunction);

    if (not target_active_power_declared and not log_state.target_active_power_function_reported) {
        logf_debug("EV did not declare the target active power function, sending the mandatory dynamic mode target "
                   "active power anyway");
        log_state.target_active_power_function_reported = true;
    }

    out.target_active_power = targets.target_active_power.value();

    // Only the base field is mandatory on the wire, so the phase values stay behind the same gate as in
    // scheduled mode.
    if (target_active_power_declared) {
        out.target_active_power_L2 = targets.target_active_power_L2;
        out.target_active_power_L3 = targets.target_active_power_L3;
    }
}

void log_ev_der_state(std::uint32_t der_alarm_status, std::uint32_t enabled_modes, std::uint32_t secc_enabled_modes,
                      SaeChargeLoopLogState& log_state) {
    if (der_alarm_status != 0 and log_state.reported_der_alarm_status != der_alarm_status) {
        logf_warning("EV reports DERAlarmStatus 0x%08x", der_alarm_status);
    }
    log_state.reported_der_alarm_status = der_alarm_status;

    // EnabledModes echoes back what the SECC enabled, so that is what it is compared against. Bits outside
    // SAE_ENABLED_MODE_MASK are dropped: the ones this document does not use are ignored per V2G20-3411, and
    // the ones without an Enable in DERControlCPDRes were never the SECC's to enable. No requirement makes
    // the SECC enforce the echo, so a mismatch only warns.
    const auto acknowledged = enabled_modes & SAE_ENABLED_MODE_MASK;
    if (acknowledged != secc_enabled_modes and not log_state.reported_enabled_modes_mismatch) {
        logf_warning("EV EnabledModes 0x%08x do not match the modes the SECC enabled 0x%08x, missing: %s, extra: %s",
                     acknowledged, secc_enabled_modes, sae_function_names(secc_enabled_modes & ~acknowledged).c_str(),
                     sae_function_names(acknowledged & ~secc_enabled_modes).c_str());
        log_state.reported_enabled_modes_mismatch = true;
    }
}

} // namespace

message_20::DER_SAE_AC_ChargeLoopResponse
handle_request(const message_20::DER_SAE_AC_ChargeLoopRequest& req, const d20::Session& session, bool stop, bool pause,
               const AcTargetPower& target_powers, const d20::AcPresentPower& present_powers,
               const UpdateDynamicModeParameters& dynamic_parameters, const d20::AcTransferLimits& ac_limits,
               const std::optional<d20::SaeDerTransferLimits>& sae_limits,
               const std::optional<d20::DerSaeSetupConfig>& der_config, bool changed_since_cpd,
               SaeChargeLoopLogState& log_state) {

    message_20::DER_SAE_AC_ChargeLoopResponse res;

    if (not validate_and_setup_header(res.header, session, req.header.session_id)) {
        return response_with_code(res, dt::ResponseCode::FAILED_UnknownSession);
    }

    if (not sae_limits.has_value()) {
        logf_error("No SAE limits are provided. Shutdown the session");
        return response_with_code(res, dt::ResponseCode::FAILED);
    }

    if (not der_config.has_value()) {
        logf_error("No SAE DER control values are provided. Shutdown the session");
        return response_with_code(res, dt::ResponseCode::FAILED);
    }

    const auto ev_supported_modes = session.get_ev_supported_sae_functions();
    if (not ev_supported_modes.has_value()) {
        logf_error("The EV never declared its SAE SupportedModes. Shutdown the session");
        return response_with_code(res, dt::ResponseCode::FAILED);
    }

    const auto secc_enabled_modes = session.get_enabled_der_control_modes();

    std::visit(
        [secc_enabled_modes, &log_state](const auto& mode) {
            log_ev_der_state(mode.der_alarm_status, mode.enabled_modes, secc_enabled_modes, log_state);
        },
        req.control_mode);

    const auto& selected_services = session.get_selected_services();
    const auto selected_control_mode = selected_services.selected_control_mode;
    const auto selected_mobility_needs_mode = selected_services.selected_mobility_needs_mode;

    // A control mode other than the selected one terminates the session.
    if (std::holds_alternative<Scheduled_DER_Req>(req.control_mode)) {
        if (selected_control_mode != dt::ControlMode::Scheduled) {
            logf_error("EV sent a scheduled mode charge loop request but scheduled mode was not selected");
            return response_with_code(res, dt::ResponseCode::FAILED);
        }

        auto& res_mode = res.control_mode.emplace<Scheduled_DER_Res>();
        fill(res_mode, target_powers, present_powers, ac_limits, sae_limits.value(), der_config.value(),
             changed_since_cpd, ev_supported_modes.value());

    } else if (std::holds_alternative<Dynamic_DER_Req>(req.control_mode)) {
        if (selected_control_mode != dt::ControlMode::Dynamic) {
            logf_error("EV sent a dynamic mode charge loop request but dynamic mode was not selected");
            return response_with_code(res, dt::ResponseCode::FAILED);
        }

        // 0 W is an instruction to stop importing and exporting, so a missing mandatory target is refused
        // instead of fabricated.
        if (not target_powers.target_active_power.has_value()) {
            logf_error("No target active power is available for the mandatory dynamic mode. Shutdown the session");
            return response_with_code(res, dt::ResponseCode::FAILED);
        }

        auto& res_mode = res.control_mode.emplace<Dynamic_DER_Res>();
        fill(res_mode, target_powers, present_powers, ac_limits, sae_limits.value(), der_config.value(),
             changed_since_cpd, ev_supported_modes.value(), log_state);

        if (selected_mobility_needs_mode == dt::MobilityNeedsMode::ProvidedBySecc) {
            set_dynamic_parameters_in_res(res_mode, dynamic_parameters, res.header.timestamp);
        }
    } else {
        logf_error("EV sent an unhandled charge loop control mode");
        return response_with_code(res, dt::ResponseCode::FAILED);
    }

    // Conditional field: it must not be sent unless the operator provided a target frequency.
    if (target_powers.target_frequency.has_value()) {
        res.target_frequency = target_powers.target_frequency;
    }

    // TODO(mlitre): Setting MeterInfo, Receipt

    if (stop) {
        res.status = {0, dt::EvseNotification::Terminate};
    } else if (pause) {
        // The EV is given a full minute to react to the pause notification.
        constexpr auto NotificationMaxDelay = 60;
        res.status = {NotificationMaxDelay, dt::EvseNotification::Pause};
    }

    return response_with_code(res, dt::ResponseCode::OK);
}

void AC_DER_SAE_ChargeLoop::enter() {
    logf_debug("Enter state: AC_DER_SAE_ChargeLoop");
    dynamic_parameters = m_ctx.cache_dynamic_mode_parameters.value_or(UpdateDynamicModeParameters{});
    target_powers = m_ctx.cache_ac_target_power.value_or(AcTargetPower{});
    present_powers = m_ctx.cache_ac_present_power.value_or(AcPresentPower{});
}

Result AC_DER_SAE_ChargeLoop::feed(Event ev) {

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

        // Direct transition, the PowerDelivery state is skipped.
        if (req->charge_progress == dt::Progress::Stop or shutdown_requested) {
            m_ctx.feedback.signal(session::feedback::Signal::CHARGE_LOOP_FINISHED);
            m_ctx.feedback.signal(session::feedback::Signal::AC_OPEN_CONTACTOR);
            return m_ctx.create_state<SessionStop>();
        }

        return {};
    }
    if (const auto* const req = variant->get_if<message_20::DER_SAE_AC_ChargeLoopRequest>()) {
        if (first_entry_in_charge_loop) {
            m_ctx.feedback.signal(session::feedback::Signal::CHARGE_LOOP_STARTED);
            first_entry_in_charge_loop = false;
        }

        const auto& der_config = m_ctx.session_config.der_sae_setup_config;
        const auto changed_since_cpd =
            der_config.has_value() and m_ctx.session.der_control_changed_since_cpd(der_config->der_control_update_time);

        const auto res = handle_request(*req, m_ctx.session, stop, pause, target_powers, present_powers,
                                        dynamic_parameters, m_ctx.session_config.ac_limits,
                                        m_ctx.session_config.der_sae_limits, der_config, changed_since_cpd, log_state);

        m_ctx.respond(res);

        if (res.response_code >= dt::ResponseCode::FAILED) {
            m_ctx.session_stopped = true;
            return {};
        }

        // respond() only stages the message, the session poll writes it. The control set is therefore recorded
        // as sent before it reaches the wire, which is safe because a failed write throws and the session poll
        // tears the session down.
        if (changed_since_cpd) {
            // The re-sent control set replaces the enables the EV echoes from here on, so the stored ones are
            // read back out of the response that just went out rather than derived from the config a second
            // time.
            const auto& der_control_cl_res =
                std::visit([](const auto& mode) -> const dt::sae::DERControlCLRes& { return mode.der_control_cl_res; },
                           res.control_mode);
            m_ctx.session.set_enabled_der_control_modes(derive_enabled_modes(der_control_cl_res));

            m_ctx.session.record_der_control_sent(der_config->der_control_update_time);
        }

        if (const auto* mode = std::get_if<Scheduled_DER_Req>(&req->control_mode)) {
            m_ctx.feedback.ac_charge_loop_req(*mode);
        } else if (const auto* mode = std::get_if<Dynamic_DER_Req>(&req->control_mode)) {
            m_ctx.feedback.ac_charge_loop_req(*mode);
        }

        m_ctx.feedback.ac_charge_loop_req(req->meter_info_requested);
        if (req->display_parameters) {
            m_ctx.feedback.ac_charge_loop_req(*req->display_parameters);
        }

        return {};
    }
    logf_warning("Expected PowerDeliveryReq or DER_SAE_AC_ChargeLoopRequest! But code type id: %d",
                 variant->get_type());

    // Sequence Error
    const message_20::Type req_type = variant->get_type();
    send_sequence_error(req_type, m_ctx);

    m_ctx.session_stopped = true;
    return {};
}

} // namespace iso15118::d20::state
