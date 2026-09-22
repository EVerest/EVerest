// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#include <iso15118/ev/d2/state/session_stop.hpp>

#include <iso15118/detail/helper.hpp>
#include <iso15118/ev/detail/d2/context_helper.hpp>
#include <iso15118/ev/detail/d2/state/session_stop.hpp>

namespace iso15118::ev::d2::state {

namespace session_stop {

message_2::SessionStopRequest create_request(session::StopReason reason) {
    message_2::SessionStopRequest req{};
    req.charging_session = session::is_pause(reason) ? message_2::datatypes::ChargingSession::Pause
                                                     : message_2::datatypes::ChargingSession::Terminate;
    return req;
}

} // namespace session_stop

BasePointerType to_session_stop(Context& ctx, session::StopReason reason) {
    const auto raised = session::raise_to_latched_stop(ctx, reason);
    ctx.send_request(session_stop::create_request(raised));
    return ctx.create_state<SessionStop>(raised);
}

void SessionStop::enter() {
    logf_debug("Enter state: SessionStop (ISO 15118-2)");
}

Result SessionStop::feed(Event ev) {
    if (ev != Event::V2GTP_MESSAGE) {
        return Result::ignored();
    }

    const auto variant = m_ctx.pull_response();
    if (expect_response<message_2::SessionStopResponse>(m_ctx, *variant) == nullptr) {
        return Result::stopping();
    }
    if (session::is_pause(m_reason)) {
        m_ctx.pause_session();
    } else {
        m_ctx.stop_session();
    }
    return Result::stopping();
}

} // namespace iso15118::ev::d2::state
