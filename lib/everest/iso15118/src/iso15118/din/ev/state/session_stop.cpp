// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Pionix GmbH and Contributors to EVerest
#include <iso15118/din/ev/state/session_stop.hpp>

#include <iso15118/din/ev/timeouts.hpp>

#include <iso15118/detail/din/ev/state/session_stop.hpp>
#include <iso15118/detail/din/ev/state/state_helper.hpp>
#include <iso15118/detail/helper.hpp>

namespace iso15118::din::ev::state {

namespace session_stop {

message_din::SessionStopRequest create_request() {
    return {}; // DIN SessionStopReq has an empty body
}

Result handle_response(const message_din::SessionStopResponse& res) {
    return {res.response_code == dt::ResponseCode::OK};
}

} // namespace session_stop

using namespace session_stop;

void SessionStop::enter() {
    m_ctx.log.enter_state("SessionStop");
}

din::ev::Result SessionStop::feed(Event ev) {
    if (ev == Event::SEND_REQUEST) {
        auto req = create_request();
        m_ctx.setup_header(req.header);
        m_ctx.send_request(req);
        m_ctx.start_timeout(d20::TimeoutType::SEQUENCE, MESSAGE_TIMEOUT_MS);
        return {};
    }

    if (ev == Event::TIMEOUT) {
        m_ctx.log("SessionStop message timeout reached, terminating session");
        m_ctx.session_stopped = true;
        return {};
    }

    if (ev != Event::V2GTP_MESSAGE) {
        return {};
    }

    m_ctx.stop_timeout(d20::TimeoutType::SEQUENCE);

    const auto variant = m_ctx.pull_response();

    if (const auto res = variant->get_if<message_din::SessionStopResponse>()) {
        [[maybe_unused]] const auto result = handle_response(*res);

        // DIN SPEC 70121 has no charging pause/resume [V2G-DC-241]: a SessionStop always terminates the
        // session, and the next session must start with SessionID=0. A pause request from upstream is not
        // representable in DIN and is treated as a terminate.
        if (m_ctx.pending_stop.value_or(ChargingSession::Terminate) == ChargingSession::Pause) {
            m_ctx.log("DIN does not support pause/resume; terminating the session instead [V2G-DC-241]");
        }
        m_ctx.session_stopped = true;
        return {};
    }

    m_ctx.log("expected SessionStopRes! But code type id: %d", variant->get_type());
    m_ctx.session_stopped = true;
    return {};
}

} // namespace iso15118::din::ev::state
