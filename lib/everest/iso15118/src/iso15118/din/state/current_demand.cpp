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

namespace {
namespace m20dt = message_20::datatypes;

// Forward the EV setpoint (target V/I) and any per-loop maxima so the DC power supply follows the EV
// (EvseV2G din_server.cpp publish_dc_ev_target_voltage_current). Reuses the dc_charge_loop_req path.
void forward_ev_setpoint(const message_din::CurrentDemandRequest& req, const session::Feedback& feedback) {
    m20dt::Scheduled_DC_CLReqControlMode mode{};
    mode.target_voltage = m20dt::from_float(static_cast<float>(req.ev_target_voltage));
    mode.target_current = m20dt::from_float(static_cast<float>(req.ev_target_current));
    if (req.ev_maximum_voltage_limit.has_value() and req.ev_maximum_current_limit.has_value() and
        req.ev_maximum_power_limit.has_value()) {
        mode.max_voltage = m20dt::from_float(static_cast<float>(req.ev_maximum_voltage_limit.value()));
        mode.max_charge_current = m20dt::from_float(static_cast<float>(req.ev_maximum_current_limit.value()));
        mode.max_charge_power = m20dt::from_float(static_cast<float>(req.ev_maximum_power_limit.value()));
    }
    feedback.dc_charge_loop_req(session::feedback::DcReqControlMode{mode});
}
} // namespace

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

    res.evse_current_limit_achieved = present_current >= config.evse_maximum_current_limit;
    res.evse_voltage_limit_achieved = present_voltage >= config.evse_maximum_voltage_limit;
    res.evse_power_limit_achieved = false;

    // [V2G-DC-391] the SessionID must match the one assigned in SessionSetup; a mismatch is answered with
    // FAILED_UnknownSession (carrying the mandatory parameters filled above) and terminates the session.
    if (session_id != req.header.session_id) {
        return response_with_code(res, dt::ResponseCode::FAILED_UnknownSession);
    }

    return response_with_code(res, dt::ResponseCode::OK);
}

void CurrentDemand::enter() {
    m_ctx.log.enter_state("CurrentDemand");
}

Result CurrentDemand::feed(Event ev) {
    if (ev == Event::CONTROL_MESSAGE) {
        if (const auto* control_data = m_ctx.get_control_event<d20::PresentVoltageCurrent>()) {
            m_ctx.present_voltage = control_data->voltage;
            m_ctx.present_current = control_data->current;
        } else if (const auto* stop = m_ctx.get_control_event<d20::StopCharging>(); stop and static_cast<bool>(*stop)) {
            // Signal an EVSE-initiated stop to the EV via EVSENotification in the loop response.
            m_ctx.charger_stop_requested = true;
        }
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
        if (not charge_loop_started) {
            m_ctx.feedback.signal(session::feedback::Signal::CHARGE_LOOP_STARTED);
            charge_loop_started = true;
        }

        forward_ev_setpoint(*req, m_ctx.feedback);

        const auto res =
            handle_request(*req, m_ctx.session_config, m_ctx.present_voltage, m_ctx.present_current,
                           m_ctx.get_session_id(), m_ctx.charger_stop_requested, m_ctx.error_status_code());
        m_ctx.respond(res);

        if (res.response_code >= dt::ResponseCode::FAILED) {
            m_ctx.session_stopped = true;
        }
        return {};
    }

    m_ctx.log("expected CurrentDemandReq! But code type id: %d", variant->get_type());
    // [V2G-DC-539]: answer with the received-type response carrying FAILED_SequenceError, then close.
    respond_sequence_error(m_ctx, *variant);
    m_ctx.session_stopped = true;
    return {};
}

} // namespace iso15118::din::state
