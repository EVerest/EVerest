// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Pionix GmbH and Contributors to EVerest
#include <iso15118/d2/state/session_stop.hpp>

#include <iso15118/detail/d2/state/sequence_error.hpp>
#include <iso15118/detail/d2/state/session_stop.hpp>
#include <iso15118/detail/helper.hpp>

namespace iso15118::d2::state {

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

Result SessionStop::feed(Event ev) {
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

    const auto res = handle_request(*req, m_ctx.get_session_id());
    m_ctx.respond(res);

    if (req->charging_session == dt::ChargingSession::Pause) {
        // Retain the session id so the returning EV can re-join with OK_OldSessionJoined.
        m_ctx.session_paused = true;
        m_ctx.pause_ctx = PauseContext{m_ctx.get_session_id()};
    } else {
        m_ctx.session_stopped = true;
        m_ctx.pause_ctx.reset();
    }

    return {};
}

} // namespace iso15118::d2::state
