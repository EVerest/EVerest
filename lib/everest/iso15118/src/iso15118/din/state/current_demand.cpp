// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Pionix GmbH and Contributors to EVerest
#include <iso15118/din/state/current_demand.hpp>

#include <iso15118/din/state/power_delivery.hpp>
#include <iso15118/din/state/session_stop.hpp>

#include <iso15118/message/common_types.hpp>
#include <iso15118/message/dc_charge_loop.hpp>

#include <iso15118/detail/din/state/current_demand.hpp>
#include <iso15118/detail/din/state/sequence_error.hpp>
#include <iso15118/detail/din/state/state_helper.hpp>
#include <iso15118/detail/helper.hpp>

namespace iso15118::din::state {

namespace m20dt = message_20::datatypes;

// Forward the EV setpoint (target V/I) and any per-loop maxima so the DC power supply follows the EV
// (EvseV2G din_server.cpp publish_dc_ev_target_voltage_current). Reuses the dc_charge_loop_req path.
m20dt::Scheduled_DC_CLReqControlMode build_ev_setpoint(const message_din::CurrentDemandRequest& req) {
    m20dt::Scheduled_DC_CLReqControlMode mode{};
    mode.target_voltage = m20dt::from_float(static_cast<float>(req.ev_target_voltage));
    mode.target_current = m20dt::from_float(static_cast<float>(req.ev_target_current));
    if (req.ev_maximum_voltage_limit.has_value()) {
        mode.max_voltage = m20dt::from_float(static_cast<float>(req.ev_maximum_voltage_limit.value()));
    }
    if (req.ev_maximum_current_limit.has_value()) {
        mode.max_charge_current = m20dt::from_float(static_cast<float>(req.ev_maximum_current_limit.value()));
    }
    if (req.ev_maximum_power_limit.has_value()) {
        mode.max_charge_power = m20dt::from_float(static_cast<float>(req.ev_maximum_power_limit.value()));
    }
    return mode;
}

message_din::CurrentDemandResponse handle_request(const message_din::CurrentDemandRequest& req,
                                                  const SessionConfig& config, float present_voltage,
                                                  float present_current, const dt::SessionId& session_id,
                                                  bool charger_stop,
                                                  std::optional<dt::DcEvseStatusCode> error_status_code) {
    message_din::CurrentDemandResponse res;
    setup_header(res.header, session_id);

    // DC_EVSEStatus, EVSEPresentVoltage/Current and the limit-achieved flags are mandatory in
    // CurrentDemandRes and must be present even on a FAILED_UnknownSession response, so populate them
    // before the SessionID check below.
    // A module-reported EVSE error (Malfunction / UtilityInterruptEvent / EmergencyShutdown) overrides the
    // status code so the EV sees the fault mid-charge-loop (mirrors EvseV2G send_error).
    res.dc_evse_status.evse_status_code = error_status_code.value_or(charger_stop ? dt::DcEvseStatusCode::EVSE_Shutdown
                                                                                  : dt::DcEvseStatusCode::EVSE_Ready);
    res.dc_evse_status.evse_isolation_status = dt::IsolationLevel::Valid;
    // [V2G-DC-500]: for DC charging the EVSENotification shall always be "None". An EVSE-initiated stop
    // is signalled to the EV via EVSEStatusCode = EVSE_Shutdown (above), not via EVSENotification.
    res.dc_evse_status.evse_notification = dt::EvseNotification::None;

    res.evse_present_voltage = present_voltage;
    res.evse_present_current = present_current;

    res.evse_maximum_current_limit = config.evse_maximum_current_limit;
    res.evse_maximum_voltage_limit = config.evse_maximum_voltage_limit;
    res.evse_maximum_power_limit = config.evse_maximum_power_limit;

    // The three *LimitAchieved flags state that the EVSE "has reached" its current / voltage / power
    // limit (DIN SPEC 70121 CurrentDemandRes semantics, identical wording in ISO 15118-2 Table 71).
    // They describe the EVSE's own output, not what the EV asked for, so all three are derived from the
    // present output values against the advertised maxima. EVSEMaximumPowerLimit is optional: with no
    // power limit advertised there is none to reach.
    res.evse_current_limit_achieved = present_current >= config.evse_maximum_current_limit;
    res.evse_voltage_limit_achieved = present_voltage >= config.evse_maximum_voltage_limit;
    res.evse_power_limit_achieved =
        config.evse_maximum_power_limit.has_value() and
        static_cast<double>(present_voltage) * present_current >= config.evse_maximum_power_limit.value();

    // [V2G-DC-391] the SessionID must match the one assigned in SessionSetup; a mismatch is answered with
    // FAILED_UnknownSession (carrying the mandatory parameters filled above) and terminates the session.
    if (session_id != req.header.session_id) {
        return response_with_code(res, dt::ResponseCode::FAILED_UnknownSession);
    }

    return response_with_code(res, dt::ResponseCode::OK);
}

void CurrentDemand::enter() {
    logf_debug("Enter state: CurrentDemand");
}

Result CurrentDemand::feed(Event ev) {
    if (ev == Event::CONTROL_MESSAGE) {
        if (const auto* control_data = m_ctx.get_control_event<d20::PresentVoltageCurrent>()) {
            m_ctx.present_voltage = control_data->voltage;
            m_ctx.present_current = control_data->current;
        }
        // An EVSE-initiated stop (StopCharging) is latched on the context by the engine, in any state.
        return {};
    }

    if (ev != Event::V2GTP_MESSAGE) {
        return {};
    }

    // An EV aborting sends SessionStopReq; hand it to SessionStop for a clean SessionStopRes.
    if (m_ctx.peek_request_type() == message_din::Type::SessionStopReq) {
        return m_ctx.create_state<SessionStop>();
    }

    // The EVCC ends charging by sending a PowerDeliveryReq(ready=false); defer it to the PowerDelivery
    // state without consuming the request (WAIT_FOR_CURRENTDEMAND_POWERDELIVERY).
    if (m_ctx.peek_request_type() == message_din::Type::PowerDeliveryReq) {
        return m_ctx.create_state<PowerDelivery>();
    }

    const auto variant = m_ctx.pull_request();

    if (const auto req = variant->get_if<message_din::CurrentDemandRequest>()) {
        // [V2G-DC-391]: validate the SessionID before any side effect -- a request from an unknown
        // session must not drive the power supply.
        if (reject_unknown_session(m_ctx, *variant)) {
            return {};
        }

        m_ctx.report_ev_status(req->dc_ev_status);

        // Remaining charging times and the completion flags the EV reports in every charge-loop request.
        session::feedback::DcEvChargeProgress progress{};
        if (req->remaining_time_to_full_soc.has_value()) {
            progress.remaining_time_to_full_soc = static_cast<float>(req->remaining_time_to_full_soc.value());
        }
        if (req->remaining_time_to_bulk_soc.has_value()) {
            progress.remaining_time_to_bulk_soc = static_cast<float>(req->remaining_time_to_bulk_soc.value());
        }
        progress.charging_complete = req->charging_complete;
        progress.bulk_charging_complete = req->bulk_charging_complete;
        m_ctx.report_charge_progress(progress);

        if (not charge_loop_started) {
            m_ctx.feedback.signal(session::feedback::Signal::CHARGE_LOOP_STARTED);
            charge_loop_started = true;
        }

        const EvSetpoint setpoint{req->ev_target_voltage, req->ev_target_current, req->ev_maximum_voltage_limit,
                                  req->ev_maximum_current_limit, req->ev_maximum_power_limit};
        if (last_forwarded_setpoint != setpoint) {
            m_ctx.feedback.dc_charge_loop_req(session::feedback::DcReqControlMode{build_ev_setpoint(*req)});
            last_forwarded_setpoint = setpoint;
        }

        // A graceful HLC shutdown (Session::request_shutdown, e.g. the module stopping the stack) is
        // signalled to the EV the same way as an EVSE-initiated stop: EVSEStatusCode = EVSE_Shutdown.
        const bool charger_stop = m_ctx.charger_stop_requested or m_ctx.shutdown_requested();

        auto res = handle_request(*req, m_ctx.session_config, m_ctx.present_voltage, m_ctx.present_current,
                                  m_ctx.get_session_id(), charger_stop, m_ctx.error_status_code());
        apply_isolation_status(m_ctx, res.dc_evse_status);
        m_ctx.respond(res);

        if (res.response_code >= dt::ResponseCode::FAILED) {
            m_ctx.session_stopped = true;
        }
        return {};
    }

    logf_warning("Expected CurrentDemandReq! But code type id: %d", variant->get_type());
    // [V2G-DC-539]: answer with the received-type response carrying FAILED_SequenceError, then close.
    respond_sequence_error(m_ctx, *variant);
    m_ctx.session_stopped = true;
    return {};
}

} // namespace iso15118::din::state
