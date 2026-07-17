// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Pionix GmbH and Contributors to EVerest
#include <iso15118/d2/ev/state/session_stop.hpp>

#include <iso15118/d2/ev/timeouts.hpp>

#include <iso15118/detail/d2/ev/state/session_stop.hpp>
#include <iso15118/detail/helper.hpp>

namespace iso15118::d2::ev::state {

namespace session_stop {

message_2::SessionStopRequest create_request(dt::ChargingSession charging_session) {
    message_2::SessionStopRequest req;
    req.charging_session = charging_session;
    return req;
}

Result handle_response(const message_2::SessionStopResponse& res) {
    return {res.response_code == dt::ResponseCode::OK};
}

} // namespace session_stop

using namespace session_stop;

void SessionStop::enter() {
    m_ctx.log.enter_state("SessionStop");
}

d2::ev::Result SessionStop::feed(Event ev) {
    if (ev == Event::SEND_REQUEST) {
        const auto charging_session = m_ctx.pending_stop.value_or(dt::ChargingSession::Terminate);
        auto req = create_request(charging_session);
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

    if (const auto res = variant->get_if<message_2::SessionStopResponse>()) {
        [[maybe_unused]] const auto result = handle_response(*res);

        const auto charging_session = m_ctx.pending_stop.value_or(dt::ChargingSession::Terminate);
        if (charging_session == dt::ChargingSession::Pause) {
            m_ctx.session_paused = true;
        } else {
            m_ctx.session_stopped = true;
        }

        // v2g_session_finished is published exactly once by the session driver at teardown.
        return {};
    }

    m_ctx.log("expected SessionStopRes! But code type id: %d", variant->get_type());
    m_ctx.session_stopped = true;
    return {};
}

} // namespace iso15118::d2::ev::state
