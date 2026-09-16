// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#include <iso15118/ev/d2/state/session_stop.hpp>

#include <iso15118/detail/helper.hpp>
#include <iso15118/ev/detail/d2/context_helper.hpp>
#include <iso15118/message_2/session_stop.hpp>

namespace iso15118::ev::d2::state {

void SessionStop::enter() {
    logf_debug("Enter state: SessionStop (ISO 15118-2)");
    message_2::SessionStopRequest req{};
    req.charging_session = m_ctx.requested_stop_reason();
    m_ctx.send_request(req);
}

Result SessionStop::feed(Event ev) {
    if (ev != Event::V2GTP_MESSAGE) {
        return Result::ignored();
    }

    const auto variant = m_ctx.pull_response();
    const auto* res = expect_response<message_2::SessionStopResponse>(m_ctx, *variant);
    if (res != nullptr and m_ctx.requested_stop_reason() == message_2::datatypes::ChargingSession::Pause) {
        m_ctx.pause_session();
    } else {
        m_ctx.stop_session();
    }
    return Result::stopping();
}

} // namespace iso15118::ev::d2::state
