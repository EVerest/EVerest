// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#include <iso15118/ev/din/state/session_stop.hpp>

#include <iso15118/detail/helper.hpp>
#include <iso15118/ev/detail/din/context_helper.hpp>
#include <iso15118/message_din/session_stop.hpp>

namespace iso15118::ev::din::state {

void SessionStop::enter() {
    logf_debug("Enter state: SessionStop (DIN 70121)");
    m_ctx.send_request(message_din::SessionStopRequest{});
}

Result SessionStop::feed(Event ev) {
    if (ev != Event::V2GTP_MESSAGE) {
        return Result::ignored();
    }

    const auto variant = m_ctx.pull_response();
    const auto* res = expect_response<message_din::SessionStopResponse>(m_ctx, *variant);
    if (res != nullptr and m_ctx.stop_is_pause()) {
        m_ctx.pause_session();
    } else {
        m_ctx.stop_session();
    }
    return Result::stopping();
}

} // namespace iso15118::ev::din::state
