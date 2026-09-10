// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Pionix GmbH and Contributors to EVerest
#include <iso15118/d2/state/welding_detection.hpp>

#include <iso15118/d2/state/session_stop.hpp>
#include <iso15118/detail/d2/state/charge_parameter_discovery.hpp>

#include <iso15118/detail/d2/state/sequence_error.hpp>
#include <iso15118/detail/d2/state/session_stop.hpp>
#include <iso15118/detail/d2/state/state_helper.hpp>
#include <iso15118/detail/d2/state/welding_detection.hpp>
#include <iso15118/detail/helper.hpp>

namespace iso15118::d2::state {

namespace {
// V2G_SECC_Msg_Performance_Time(WeldingDetectionRes) = 1,5 s (Table 109): the window
// [V2G2-920]..[V2G2-922] give the SECC to measure CP State B after a WeldingDetectionReq.
constexpr uint32_t CP_STATE_PERFORMANCE_TIME_MS = 1500;
} // namespace

// ISO 15118-2 defines no SECC-side WeldingDetection supervision timer (unlike DIN's
// V2G_SECC_WeldingDetection_Timeout): the wait is bounded solely by V2G_SECC_Sequence_Timeout, and
// the -4 ATS (TC_SECC_DC_VTB_SessionStop_009) asserts no close happens before that expires.
message_2::WeldingDetectionResponse handle_request([[maybe_unused]] const message_2::WeldingDetectionRequest& req,
                                                   const dt::SessionId& session_id, float present_voltage,
                                                   std::optional<dt::DC_EVSEStatusCode> error_status_code,
                                                   bool charger_stop) {
    message_2::WeldingDetectionResponse res;
    res.header.session_id = session_id;
    res.response_code = dt::ResponseCode::OK;

    res.dc_evse_status.notification = charger_stop ? dt::EVSENotification::StopCharging : dt::EVSENotification::None;
    res.dc_evse_status.notification_max_delay = 0;
    res.dc_evse_status.isolation_status = dt::IsolationLevel::Valid;
    // EVSE_Shutdown on a stop, EVSE_Ready otherwise, and a module fault overrides both.
    res.dc_evse_status.status_code = error_status_code.value_or(charger_stop ? dt::DC_EVSEStatusCode::EVSE_Shutdown
                                                                             : dt::DC_EVSEStatusCode::EVSE_Ready);

    res.evse_present_voltage = dt::to_physical_value(present_voltage, dt::Unit::V);
    return res;
}

namespace {
void answer_welding_detection(Context& ctx, const message_2::WeldingDetectionRequest& req) {
    auto res = handle_request(req, ctx.get_session_id(), ctx.evse().present_voltage, ctx.error_status_code(),
                              ctx.evse().charger_stop_requested);
    apply_isolation_status(ctx, res.dc_evse_status);
    ctx.respond(res);
}
} // namespace

void PostCharge::enter() {
    logf_debug("Enter state: PostCharge");
}

Result PostCharge::accept_welding_detection(const message_2::WeldingDetectionRequest& req) {
    m_ctx.report_ev_status(req.dc_ev_status);

    if (cp_state_b_outstanding(m_ctx)) {
        pending_weld = req;
        m_ctx.arm_cp_state_timeout(CP_STATE_PERFORMANCE_TIME_MS);
        return {};
    }

    answer_welding_detection(m_ctx, req);
    return m_ctx.create_state<WeldingDetection>();
}

Result PostCharge::accept_session_stop(const message_2::SessionStopRequest& req) {
    // [V2G2-920] names SessionStopReq alongside WeldingDetectionReq, so this node gates both. The gate
    // used to live in SessionStop, which stopped being possible once the answer became a function call.
    if (req.charging_session != dt::ChargingSession::Pause and cp_state_b_outstanding(m_ctx)) {
        pending_stop = req;
        m_ctx.arm_cp_state_timeout(CP_STATE_PERFORMANCE_TIME_MS);
        return {};
    }

    return process_session_stop(m_ctx, req);
}

Result PostCharge::on_event(Event ev) {
    if (ev == Event::CONTROL_MESSAGE) {
        if (const auto* control = m_ctx.get_control_event<d20::PresentVoltageCurrent>()) {
            m_ctx.set_present_values(control->voltage, control->current);
        }
        // Parked while waiting for CP State B ([V2G2-920]..[V2G2-922]): resume as soon as it arrives.
        if (m_ctx.evse().current_cp_state == d20::CpState::B) {
            if (pending_weld.has_value()) {
                m_ctx.stop_timeout(d20::TimeoutType::CPSTATE);
                const auto req = pending_weld.value();
                pending_weld.reset();
                answer_welding_detection(m_ctx, req);
                return m_ctx.create_state<WeldingDetection>();
            }
            if (pending_stop.has_value()) {
                m_ctx.stop_timeout(d20::TimeoutType::CPSTATE);
                const auto req = pending_stop.value();
                pending_stop.reset();
                return process_session_stop(m_ctx, req);
            }
        }
        return {};
    }

    if (ev == Event::TIMEOUT) {
        const auto* timeout = m_ctx.get_active_timeout();
        if (timeout and *timeout == d20::TimeoutType::CPSTATE) {
            // [V2G2-922]: no CP State B in time, so respond FAILED in the received message's own type. The
            // FAILED response arms the FailedTermination path (oscillator off + SECC-side TCP close).
            if (pending_weld.has_value()) {
                logf_warning("no CP State B within V2G_SECC_Msg_Performance_Time, WeldingDetection -> FAILED");
                pending_weld.reset();
                auto res = handle_request(message_2::WeldingDetectionRequest{}, m_ctx.get_session_id(),
                                          m_ctx.evse().present_voltage, m_ctx.error_status_code(),
                                          m_ctx.evse().charger_stop_requested);
                res.response_code = dt::ResponseCode::FAILED;
                apply_isolation_status(m_ctx, res.dc_evse_status);
                m_ctx.respond(res);
                m_ctx.session_stopped = true;
            } else if (pending_stop.has_value()) {
                logf_warning("no CP State B within V2G_SECC_Msg_Performance_Time, SessionStop -> FAILED");
                pending_stop.reset();
                auto res = state::handle_request(message_2::SessionStopRequest{}, m_ctx.get_session_id());
                res.response_code = dt::ResponseCode::FAILED;
                m_ctx.respond(res);
                m_ctx.session_stopped = true;
            }
        }
        return {};
    }

    return {};
}

Result PostCharge::on_request(const message_2::Variant& received) {
    const auto type = received.get_type();
    if (type == message_2::Type::WeldingDetectionReq) {
        return accept_welding_detection(received.get<message_2::WeldingDetectionRequest>());
    } else if (type == message_2::Type::ChargeParameterDiscoveryReq) {
        // Same path as the PowerDelivery renegotiation [V2G2-813].
        return process_charge_parameter_discovery(m_ctx, received.get<message_2::ChargeParameterDiscoveryRequest>());
    } else if (type == message_2::Type::SessionStopReq) {
        return accept_session_stop(received.get<message_2::SessionStopRequest>());
    } else {
        logf_warning("Expected WeldingDetectionReq, ChargeParameterDiscoveryReq or SessionStopReq! But got "
                     "type id: %d",
                     received.get_type());
        respond_sequence_error(m_ctx, received.get_type());
        return {};
    }
}

void PostCharge::leave() {
    // Runs on every transition out, so no parked request's CP-state timeout survives into the next state.
    m_ctx.clear_cp_state_timeout();
}

void WeldingDetection::enter() {
    logf_debug("Enter state: WeldingDetection");
}

Result WeldingDetection::on_event(Event ev) {
    if (ev == Event::CONTROL_MESSAGE) {
        if (const auto* control = m_ctx.get_control_event<d20::PresentVoltageCurrent>()) {
            m_ctx.set_present_values(control->voltage, control->current);
        }
        return {};
    }

    return {};
}

Result WeldingDetection::on_request(const message_2::Variant& received) {
    // [V2G2-597] narrows the node here: no ChargeParameterDiscoveryReq any more.
    const auto type = received.get_type();
    if (type == message_2::Type::WeldingDetectionReq) {
        const auto& req = received.get<message_2::WeldingDetectionRequest>();
        m_ctx.report_ev_status(req.dc_ev_status);
        answer_welding_detection(m_ctx, req);
        return {};
    } else if (type == message_2::Type::SessionStopReq) {
        // CP State B was confirmed in PostCharge, so the [V2G2-920] gate has nothing left to wait for.
        return process_session_stop(m_ctx, received.get<message_2::SessionStopRequest>());
    } else {
        logf_warning("Expected WeldingDetectionReq or SessionStopReq! But got type id: %d", received.get_type());
        respond_sequence_error(m_ctx, received.get_type());
        return {};
    }
}

} // namespace iso15118::d2::state
