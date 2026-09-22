// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#include <iso15118/ev/din/state/session_stop.hpp>

#include <iso15118/detail/helper.hpp>
#include <iso15118/ev/detail/din/context_helper.hpp>
#include <iso15118/ev/detail/din/state/session_stop.hpp>

namespace iso15118::ev::din::state {

namespace session_stop {

message_din::SessionStopRequest create_request() {
    return {};
}

} // namespace session_stop

BasePointerType to_session_stop(Context& ctx, session::StopReason reason) {
    const auto raised = session::raise_to_latched_stop(ctx, reason);
    ctx.send_request(session_stop::create_request());
    return ctx.create_state<SessionStop>(raised);
}

void SessionStop::enter() {
    logf_debug("Enter state: SessionStop (DIN 70121)");
}

Result SessionStop::feed(Event ev) {
    if (ev != Event::V2GTP_MESSAGE) {
        return Result::ignored();
    }

    const auto variant = m_ctx.pull_response();
    if (expect_response<message_din::SessionStopResponse>(m_ctx, *variant) == nullptr) {
        return Result::stopping();
    }
    // DIN 70121 has no pause on the wire; an acknowledged pause keeps the session id to re-join.
    if (session::is_pause(m_reason)) {
        m_ctx.pause_session();
    } else {
        m_ctx.stop_session();
    }
    return Result::stopping();
}

} // namespace iso15118::ev::din::state
