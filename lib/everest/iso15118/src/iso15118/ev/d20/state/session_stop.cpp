// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#include <iso15118/detail/helper.hpp>
#include <iso15118/ev/d20/context.hpp>
#include <iso15118/ev/d20/state/session_stop.hpp>
#include <iso15118/ev/detail/d20/context_helper.hpp>
#include <iso15118/message/session_stop.hpp>

namespace iso15118::ev::d20::state {

message_20::SessionStopRequest SessionStop::make_request(session::StopReason reason) {
    message_20::SessionStopRequest req;
    req.charging_session = session::is_pause(reason) ? message_20::datatypes::ChargingSession::Pause
                                                     : message_20::datatypes::ChargingSession::Terminate;
    return req;
}

BasePointerType to_session_stop(Context& ctx, session::StopReason reason) {
    const auto raised = session::raise_to_latched_stop(ctx, reason);
    ctx.send_request(SessionStop::make_request(raised));
    return ctx.create_state<SessionStop>(raised);
}

Result SessionStop::feed(Event ev) {
    if (ev != Event::V2GTP_MESSAGE) {
        return Result::ignored();
    }

    const auto variant = m_ctx.pull_response();

    // SessionStop is terminal: a response expect_response rejects has already stopped the session.
    if (expect_response<message_20::SessionStopResponse>(m_ctx, *variant) == nullptr) {
        return Result::stopping();
    }
    if (session::is_pause(m_reason)) {
        // An acknowledged pause keeps the session id joinable.
        m_ctx.pause_session();
    } else {
        m_ctx.stop_session();
    }
    return Result::stopping();
}

} // namespace iso15118::ev::d20::state
