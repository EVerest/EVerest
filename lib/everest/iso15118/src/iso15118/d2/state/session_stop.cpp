// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#include <iso15118/d2/state/session_stop.hpp>
#include <iso15118/message/d2/session_stop.hpp>
#include <iso15118/detail/d2/context_helper.hpp>
#include <iso15118/detail/helper.hpp>
#include <iso15118/session/feedback.hpp>

namespace iso15118::d2::state {

namespace dt = msg::data_types;

void SessionStop::enter() {
    m_ctx.feedback.signal(session::feedback::Signal::DLINK_TERMINATE);
}

Result SessionStop::feed(Event ev) {
    if (ev != Event::V2GTP_MESSAGE) {
        return {};
    }

    const auto variant = m_ctx.pull_request();

    if (const auto req = variant->get_if<msg::SessionStopRequest>()) {
        msg::SessionStopResponse res;
        setup_header(res.header, m_ctx.session);
        response_with_code(res, dt::ResponseCode::OK);
        m_ctx.respond(res);
        m_ctx.session_stopped = true;
        return {};
    }

    const msg::Type req_type = variant->get_type();
    send_sequence_error(req_type, m_ctx);
    m_ctx.session_stopped = true;
    return {};
}

} // namespace iso15118::d2::state
