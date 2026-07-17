// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Pionix GmbH and Contributors to EVerest
#include <iso15118/d2/state/session_stop.hpp>

#include <iso15118/detail/d2/state/sequence_error.hpp>
#include <iso15118/detail/d2/state/session_stop.hpp>
#include <iso15118/detail/helper.hpp>

namespace iso15118::d2::state {

namespace {
// V2G_SECC_CPState_Detection_Timeout (Table 77): maximum time to wait for CP State B after receiving
// the request following PowerDelivery(Stop) ([V2G2-920]..[V2G2-922]).
constexpr uint32_t TIMEOUT_CPSTATE_DETECTION_MS = 1500;
} // namespace

message_2::SessionStopResponse handle_request([[maybe_unused]] const message_2::SessionStopRequest& req,
                                              const dt::SessionId& session_id) {
    message_2::SessionStopResponse res;
    res.header.session_id = session_id;
    res.response_code = dt::ResponseCode::OK;
    return res;
}

void SessionStop::enter() {
    m_ctx.log.enter_state("SessionStop");
}

void SessionStop::process_request(const message_2::SessionStopRequest& req) {
    const auto res = handle_request(req, m_ctx.get_session_id());
    m_ctx.respond(res);

    if (req.charging_session == dt::ChargingSession::Pause) {
        // Retain the session id so the returning EV can re-join with OK_OldSessionJoined.
        m_ctx.session_paused = true;
        m_ctx.pause_ctx = PauseContext{m_ctx.get_session_id()};
    } else {
        m_ctx.session_stopped = true;
        m_ctx.pause_ctx.reset();
    }

    // The positive Res anchors the CP-oscillator retain time (the res built above is always OK here);
    // reported once the response actually hit the wire (Session::send_response).
    m_ctx.session_stop_res_pending = (req.charging_session == dt::ChargingSession::Pause)
                                         ? session::feedback::SessionStopAction::Pause
                                         : session::feedback::SessionStopAction::Terminate;
}

Result SessionStop::feed(Event ev) {
    if (ev == Event::CONTROL_MESSAGE) {
        // Parked while waiting for CP State B ([V2G2-920]..[V2G2-922]): resume as soon as it arrives.
        if (pending_req.has_value() and m_ctx.current_cp_state == d20::CpState::B) {
            m_ctx.stop_timeout(d20::TimeoutType::CPSTATE);
            const auto req = pending_req.value();
            pending_req.reset();
            process_request(req);
        }
        return {};
    }

    if (ev == Event::TIMEOUT) {
        const auto* timeout = m_ctx.get_active_timeout();
        if (timeout and *timeout == d20::TimeoutType::CPSTATE and pending_req.has_value()) {
            // [V2G2-922] No CP State B within V2G_SECC_CPState_Detection_Timeout after the
            // SessionStopReq: respond FAILED and end the session (the FAILED response arms the
            // FailedTermination path: oscillator off + SECC-side TCP close).
            m_ctx.log("no CP State B within V2G_SECC_CPState_Detection_Timeout, SessionStop -> FAILED");
            pending_req.reset();
            auto res = handle_request(message_2::SessionStopRequest{}, m_ctx.get_session_id());
            res.response_code = dt::ResponseCode::FAILED;
            m_ctx.respond(res);
            m_ctx.session_stopped = true;
        }
        return {};
    }

    if (ev != Event::V2GTP_MESSAGE) {
        return {};
    }

    const auto variant = m_ctx.pull_request();

    const auto req = variant->get_if<message_2::SessionStopRequest>();
    if (req == nullptr) {
        m_ctx.log("expected SessionStopReq! But code type id: %d", variant->get_type());
        // [V2G2-539]: answer with the received-type response carrying FAILED_SequenceError, then close.
        respond_sequence_error(m_ctx, *variant);
        m_ctx.session_stopped = true;
        return {};
    }

    // The request is the expected type but must echo the assigned SessionID; a mismatch is answered with
    // SessionStopRes/FAILED_UnknownSession and terminates the session.
    if (reject_unknown_session(m_ctx, *variant)) {
        return {};
    }

    // [V2G2-920]..[V2G2-922] After PowerDelivery(Stop) the EV must be in CP State B; give it
    // V2G_SECC_CPState_Detection_Timeout from the request before answering FAILED. (A Pause request
    // ends the charge loop into sleep mode and is not subject to this DC stop gate.)
    if (m_ctx.power_delivery_stopped and req->charging_session != dt::ChargingSession::Pause and
        m_ctx.current_cp_state != d20::CpState::B) {
        pending_req = *req;
        m_ctx.start_timeout(d20::TimeoutType::CPSTATE, TIMEOUT_CPSTATE_DETECTION_MS);
        return {};
    }

    process_request(*req);
    return {};
}

} // namespace iso15118::d2::state
