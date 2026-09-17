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
    logf_debug("Enter state: SessionStop");
}

// An action rather than a state of its own, because both Figure 103's SessionStop and Figure 104's
// welding bubble process the request. The DC states apply the [V2G2-920] CP State B gate before
// calling it; the AC path (8.7.4.3 has no such gate) calls it at once.
Result process_session_stop(Context& m_ctx, const message_2::SessionStopRequest& req) {
    const auto res = handle_request(req, m_ctx.get_session_id());
    m_ctx.respond(res);

    if (req.charging_session == dt::ChargingSession::Pause) {
        // Retain the session id so the returning EV can re-join with OK_OldSessionJoined, and the payment
        // option so the resumed ServiceDiscoveryRes can offer only that one [V2G2-741].
        m_ctx.session_paused = true;
        const auto selected_option =
            m_ctx.session().contract_selected ? dt::PaymentOption::Contract : dt::PaymentOption::ExternalPayment;
        m_ctx.pause_ctx = PauseContext{m_ctx.get_session_id(), selected_option};
    } else {
        m_ctx.session_stopped = true;
        m_ctx.pause_ctx.reset();
    }

    // Reported once the response actually hit the wire (Session::send_response).
    m_ctx.session_stop_res_pending = (req.charging_session == dt::ChargingSession::Pause)
                                         ? session::feedback::SessionStopAction::Pause
                                         : session::feedback::SessionStopAction::Terminate;

    return {};
}

Result SessionStop::on_event(Event) {
    return {};
}

Result SessionStop::on_request(const message_2::Variant& received) {
    // [V2G2-568]: only a SessionStopReq is in sequence here.
    const auto type = received.get_type();
    if (type == message_2::Type::SessionStopReq) {
        return process_session_stop(m_ctx, received.get<message_2::SessionStopRequest>());
    } else {
        logf_warning("Expected SessionStopReq! But got type id: %d", received.get_type());
        respond_sequence_error(m_ctx, received.get_type());
        return {};
    }
}

} // namespace iso15118::d2::state
