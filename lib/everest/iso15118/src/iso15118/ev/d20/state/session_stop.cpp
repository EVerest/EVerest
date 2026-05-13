// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#include <iso15118/detail/helper.hpp>
#include <iso15118/ev/d20/context.hpp>
#include <iso15118/ev/d20/state/session_stop.hpp>
#include <iso15118/ev/detail/d20/context_helper.hpp>
#include <iso15118/message/session_stop.hpp>

namespace iso15118::ev::d20::state {

void SessionStop::enter() {
    // TODO(mlitre): Adding logging

    message_20::SessionStopRequest req;
    setup_header(req.header, m_ctx.get_session());
    req.charging_session = message_20::datatypes::ChargingSession::Terminate;
    m_ctx.respond(req);
}

Result SessionStop::feed(Event ev) {
    if (ev != Event::V2GTP_MESSAGE) {
        return {};
    }

    const auto variant = m_ctx.pull_response();

    if (const auto res = variant->get_if<message_20::SessionStopResponse>()) {
        if (res->response_code != message_20::datatypes::ResponseCode::OK) {
            logf_warning("SessionStopResponse with non-OK response_code: %d", static_cast<int>(res->response_code));
        }
    } else {
        logf_error("Expected SessionStopResponse, got code type id: %d", static_cast<int>(variant->get_type()));
    }

    m_ctx.stop_session(true);
    return {};
}

} // namespace iso15118::ev::d20::state
