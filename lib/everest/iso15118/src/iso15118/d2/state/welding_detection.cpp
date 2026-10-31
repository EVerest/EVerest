// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
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
    // Welding detection follows PowerDelivery(Stop), so side B is disabled: EVSE_Shutdown (IEC 61851-23:2023
    // Table CC.9 t104, CC.7.6.17 step 3), and a module fault overrides it.
    res.dc_evse_status.status_code = error_status_code.value_or(dt::DC_EVSEStatusCode::EVSE_Shutdown);

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

// Every WeldingDetectionRes reports a value of the welding detection voltage source measured after its
// request [IEC 61851-23:2023 CC.6.3, Table CC.9 t105 to t106]; see session::WeldingDetectionPhase.
void park_welding_detection(Context& ctx, bool& wait_expired) {
    ctx.welding_detection.request_received();
    wait_expired = false;
    ctx.arm_welding_voltage_timeout();
}

bool welding_voltage_ready(const Context& ctx, bool wait_expired) {
    return ctx.welding_detection.has_fresh_voltage() or wait_expired;
}

// With every update outstanding the source stalled; with one in, the value is at worst one poll interval
// older than the request.
void log_welding_voltage_timeout(const Context& ctx) {
    if (ctx.welding_detection.updates_since_request() == 0) {
        logf_warning("No value from the welding detection voltage source within %d ms of the WeldingDetectionReq, "
                     "answering with the last present voltage",
                     d20::TIMEOUT_WELDING_VOLTAGE);
    } else {
        logf_info("Only one value from the welding detection voltage source within %d ms of the "
                  "WeldingDetectionReq, answering with it",
                  d20::TIMEOUT_WELDING_VOLTAGE);
    }
}

void PostCharge::enter() {
    logf_debug("Enter state: PostCharge");
}

Result PostCharge::accept_welding_detection(const message_2::WeldingDetectionRequest& req) {
    m_ctx.report_ev_status(req.dc_ev_status);

    pending_weld = req;
    if (cp_state_b_outstanding(m_ctx)) {
        m_ctx.arm_cp_state_timeout(CP_STATE_PERFORMANCE_TIME_MS);
    }
    park_welding_detection(m_ctx, welding_voltage_wait_expired);
    return answer_pending_weld();
}

// Answered once CP State B is measured and values of the welding detection voltage source measured after
// the request arrived, or waiting for them expired.
Result PostCharge::answer_pending_weld() {
    if (not pending_weld.has_value()) {
        return {};
    }
    // A session that ended meanwhile (CP State A, error) gets no answer to a parked request.
    if (m_ctx.session_stopped) {
        pending_weld.reset();
        m_ctx.clear_cp_state_timeout();
        m_ctx.clear_welding_voltage_timeout();
        return {};
    }
    if (cp_state_b_outstanding(m_ctx)) {
        return {};
    }
    if (not welding_voltage_ready(m_ctx, welding_voltage_wait_expired)) {
        return {};
    }

    m_ctx.clear_cp_state_timeout();
    m_ctx.clear_welding_voltage_timeout();
    const auto req = pending_weld.value();
    pending_weld.reset();
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
            m_ctx.set_present_values(*control);
        }
        // Parked while waiting for CP State B ([V2G2-920]..[V2G2-922]) or the welding detection voltage:
        // resume as soon as both are there.
        if (pending_weld.has_value()) {
            return answer_pending_weld();
        }
        if (m_ctx.evse().current_cp_state == d20::CpState::B) {
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
        if (timeout and *timeout == d20::TimeoutType::WELDING_VOLTAGE) {
            log_welding_voltage_timeout(m_ctx);
            welding_voltage_wait_expired = true;
            return answer_pending_weld();
        }
        if (timeout and *timeout == d20::TimeoutType::CPSTATE) {
            // [V2G2-922]: no CP State B in time, so respond FAILED in the received message's own type. The
            // FAILED response arms the FailedTermination path (oscillator off + SECC-side TCP close).
            if (pending_weld.has_value()) {
                logf_warning("no CP State B within V2G_SECC_Msg_Performance_Time, WeldingDetection -> FAILED");
                pending_weld.reset();
                m_ctx.clear_welding_voltage_timeout();
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
        m_ctx.welding_detection.finish(m_ctx.feedback);
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
    // Runs on every transition out, so no parked request's timeout survives into the next state.
    m_ctx.clear_cp_state_timeout();
    m_ctx.clear_welding_voltage_timeout();
}

void WeldingDetection::enter() {
    logf_debug("Enter state: WeldingDetection");
}

// A session that ended meanwhile (CP State A, error) gets no answer to a parked request.
void WeldingDetection::answer_pending_weld() {
    if (not pending_weld.has_value()) {
        return;
    }
    if (m_ctx.session_stopped) {
        pending_weld.reset();
        m_ctx.clear_welding_voltage_timeout();
        return;
    }
    if (not welding_voltage_ready(m_ctx, welding_voltage_wait_expired)) {
        return;
    }
    m_ctx.clear_welding_voltage_timeout();
    const auto req = pending_weld.value();
    pending_weld.reset();
    answer_welding_detection(m_ctx, req);
}

Result WeldingDetection::on_event(Event ev) {
    if (ev == Event::CONTROL_MESSAGE) {
        if (const auto* control = m_ctx.get_control_event<d20::PresentVoltageCurrent>()) {
            m_ctx.set_present_values(*control);
        }
        answer_pending_weld();
        return {};
    }

    if (ev == Event::TIMEOUT) {
        const auto* timeout = m_ctx.get_active_timeout();
        if (timeout and *timeout == d20::TimeoutType::WELDING_VOLTAGE and pending_weld.has_value()) {
            log_welding_voltage_timeout(m_ctx);
            welding_voltage_wait_expired = true;
            answer_pending_weld();
        }
    }
    return {};
}

Result WeldingDetection::on_request(const message_2::Variant& received) {
    // [V2G2-597] narrows the node here: no ChargeParameterDiscoveryReq any more.
    const auto type = received.get_type();
    if (type == message_2::Type::WeldingDetectionReq) {
        const auto& req = received.get<message_2::WeldingDetectionRequest>();
        m_ctx.report_ev_status(req.dc_ev_status);
        pending_weld = req;
        park_welding_detection(m_ctx, welding_voltage_wait_expired);
        answer_pending_weld();
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

void WeldingDetection::leave() {
    m_ctx.clear_welding_voltage_timeout();
}

} // namespace iso15118::d2::state
