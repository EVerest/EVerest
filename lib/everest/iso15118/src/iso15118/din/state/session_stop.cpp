// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Pionix GmbH and Contributors to EVerest
#include <iso15118/din/state/session_stop.hpp>

#include <iso15118/detail/din/state/sequence_error.hpp>
#include <iso15118/detail/din/state/session_stop.hpp>
#include <iso15118/detail/din/state/state_helper.hpp>
#include <iso15118/detail/helper.hpp>

namespace iso15118::din::state {

message_din::SessionStopResponse handle_request([[maybe_unused]] const message_din::SessionStopRequest& req,
                                                const dt::SessionId& session_id) {
    message_din::SessionStopResponse res;

    setup_header(res.header, session_id);

    return response_with_code(res, dt::ResponseCode::OK);
}

void SessionStop::enter() {
    logf_debug("Enter state: SessionStop");
}

Result process_session_stop(Context& m_ctx, [[maybe_unused]] const message_din::SessionStopRequest& req) {
    // The session ends here rather than by transitioning, so nothing downstream releases these timers.
    m_ctx.stop_timeout(d20::TimeoutType::ONGOING);
    m_ctx.clear_cp_state_timeout();

    const auto res = handle_request(req, m_ctx.get_session_id());
    m_ctx.respond(res);

    // [V2G-DC-451]: DIN signals a pause only by a later re-join, so this always terminates.
    m_ctx.session_stopped = true;

    // [V2G-DC-968]: a positive Res anchors the CP-oscillator retain time; a FAILED one ends the session
    // with immediate oscillator-off instead. Reported once the response actually hit the wire.
    m_ctx.session_stop_res_pending = (res.response_code == dt::ResponseCode::OK)
                                         ? session::feedback::SessionStopAction::Terminate
                                         : session::feedback::SessionStopAction::FailedTermination;
    return {};
}

// The CP State B gate that used to live here is gone: [V2G-DC-988] governs the request following
// PowerDelivery(off), which [V2G-DC-459] awaits in the welding-detection node, so nothing reaches
// SessionStop with power_delivery_stopped set and the gate could never fire.
Result SessionStop::on_request(const message_din::Variant& received) {
    if (const auto req = received.get_if<message_din::SessionStopRequest>()) {
        return process_session_stop(m_ctx, *req);
    }

    logf_warning("Expected SessionStopReq! But code type id: %d", received.get_type());
    respond_sequence_error(m_ctx, received);
    m_ctx.session_stopped = true;
    return {};
}

} // namespace iso15118::din::state
