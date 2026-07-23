// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Pionix GmbH and Contributors to EVerest
#include <iso15118/din/state/session_stop.hpp>

#include <iso15118/detail/din/state/sequence_error.hpp>
#include <iso15118/detail/din/state/session_stop.hpp>
#include <iso15118/detail/din/state/state_helper.hpp>
#include <iso15118/detail/helper.hpp>

namespace iso15118::din::state {

namespace {
// V2G_SECC_CPState_Detection_Timeout (Table 77): maximum time to wait for CP State B after receiving
// the request following PowerDelivery(off) [V2G-DC-988]/[V2G-DC-556].
constexpr uint32_t TIMEOUT_CPSTATE_DETECTION_MS = 1500;
} // namespace

message_din::SessionStopResponse handle_request(const message_din::SessionStopRequest& req,
                                                const dt::SessionId& session_id) {
    message_din::SessionStopResponse res;

    if (not validate_and_setup_header(res.header, session_id, req.header.session_id)) {
        return response_with_code(res, dt::ResponseCode::FAILED_UnknownSession);
    }

    return response_with_code(res, dt::ResponseCode::OK);
}

void SessionStop::enter() {
    m_ctx.log.enter_state("SessionStop");
}

void SessionStop::process_request(const message_din::SessionStopRequest& req) {
    const auto res = handle_request(req, m_ctx.get_session_id());
    m_ctx.respond(res);

    // [V2G-DC-451] The session is terminated after sending the response. DIN signals a pause only by a
    // later re-join, so the SessionStopReq itself always terminates on the SECC side.
    m_ctx.session_stopped = true;

    // [V2G-DC-968] A positive Res anchors the CP-oscillator retain time; a FAILED Res
    // (unknown session) ends the session with immediate oscillator-off + SECC-side TCP close
    // instead ([V2G-DC-942]/[V2G-DC-940]). Reported once the response actually hit the wire
    // (Session::send_response).
    m_ctx.session_stop_res_pending = (res.response_code == dt::ResponseCode::OK)
                                         ? session::feedback::SessionStopAction::Terminate
                                         : session::feedback::SessionStopAction::FailedTermination;
}

Result SessionStop::feed(Event ev) {
    if (ev == Event::CONTROL_MESSAGE) {
        // Parked while waiting for CP State B ([V2G-DC-988]): resume as soon as it is reported.
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
            // [V2G-DC-556] No CP State B within V2G_SECC_CPState_Detection_Timeout after the
            // SessionStopReq: respond FAILED and end the session (the FAILED response arms the
            // FailedTermination path: oscillator off without delay + SECC-side TCP close).
            m_ctx.log("no CP State B within V2G_SECC_CPState_Detection_Timeout, SessionStop -> FAILED");
            pending_req.reset();
            message_din::SessionStopResponse res;
            setup_header(res.header, m_ctx.get_session_id());
            m_ctx.respond(response_with_code(res, dt::ResponseCode::FAILED));
            m_ctx.session_stopped = true;
        }
        return {};
    }

    if (ev != Event::V2GTP_MESSAGE) {
        return {};
    }

    const auto variant = m_ctx.pull_request();

    if (const auto req = variant->get_if<message_din::SessionStopRequest>()) {
        // [V2G-DC-988] After PowerDelivery(off) the EV must signal CP State B before/around the next
        // request; give it V2G_SECC_CPState_Detection_Timeout from the request before failing.
        if (m_ctx.power_delivery_stopped and m_ctx.current_cp_state != d20::CpState::B) {
            pending_req = *req;
            m_ctx.start_timeout(d20::TimeoutType::CPSTATE, TIMEOUT_CPSTATE_DETECTION_MS);
            return {};
        }
        process_request(*req);
        return {};
    }

    m_ctx.log("expected SessionStopReq! But code type id: %d", variant->get_type());
    // [V2G-DC-539]: answer with the received-type response carrying FAILED_SequenceError, then close.
    respond_sequence_error(m_ctx, *variant);
    m_ctx.session_stopped = true;
    return {};
}

} // namespace iso15118::din::state
