// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Pionix GmbH and Contributors to EVerest
#include <iso15118/d20/ev/state/session_setup.hpp>

#include <iso15118/d20/ev/state/authorization_setup.hpp>
#include <iso15118/d20/ev/timeouts.hpp>

#include <iso15118/detail/d20/ev/state/session_setup.hpp>
#include <iso15118/detail/d20/ev/state/state_helper.hpp>
#include <iso15118/detail/helper.hpp>

namespace iso15118::d20::ev::state {

namespace session_setup {

message_20::SessionSetupRequest create_request(const std::string& evcc_id) {
    message_20::SessionSetupRequest req;
    // header (incl. the all-zero session id) is filled in by the state via Context::setup_header
    req.evccid = evcc_id;
    return req;
}

Result handle_response(const message_20::SessionSetupResponse& res) {
    Result result;
    result.valid = (res.response_code == dt::ResponseCode::OK_NewSessionEstablished) or
                   (res.response_code == dt::ResponseCode::OK_OldSessionJoined);
    result.new_session = (res.response_code == dt::ResponseCode::OK_NewSessionEstablished);
    result.session_id = res.header.session_id;
    result.evse_id = res.evseid;
    return result;
}

} // namespace session_setup

using namespace session_setup;

void SessionSetup::enter() {
    m_ctx.log.enter_state("SessionSetup");
}

d20::ev::Result SessionSetup::feed(Event ev) {
    if (ev == Event::SEND_REQUEST) {
        auto req = create_request(m_ctx.session_config.evcc_id);
        m_ctx.setup_header(req.header); // session id is still all-zero here [V2G20 SessionSetupReq]
        m_ctx.send_request(req);
        m_ctx.start_timeout(d20::TimeoutType::SEQUENCE, MESSAGE_TIMEOUT_MS);
        return {};
    }

    if (ev == Event::CONTROL_MESSAGE) {
        handle_stop_control_event(m_ctx);
        return {};
    }

    if (ev == Event::TIMEOUT) {
        m_ctx.log("SessionSetup message timeout reached, terminating session");
        m_ctx.session_stopped = true;
        return {};
    }

    if (ev != Event::V2GTP_MESSAGE) {
        return {};
    }

    m_ctx.stop_timeout(d20::TimeoutType::SEQUENCE);

    const auto variant = m_ctx.pull_response();

    if (const auto res = variant->get_if<message_20::SessionSetupResponse>()) {
        const auto result = handle_response(*res);

        if (not result.valid) {
            m_ctx.log("SessionSetup failed, terminating session");
            m_ctx.session_stopped = true;
            return {};
        }

        // Adopt the SECC-assigned session id for all following messages.
        m_ctx.set_session_id(result.session_id);
        m_ctx.evse_info.evse_id = result.evse_id;
        m_ctx.feedback.evse_id(result.evse_id);

        logf_info("Session established with evseid: %s", result.evse_id.c_str());

        if (auto stop = stop_if_pending(m_ctx)) {
            return stop;
        }
        return m_ctx.create_state<AuthorizationSetup>();
    }

    m_ctx.log("expected SessionSetupRes! But code type id: %d", variant->get_type());
    m_ctx.session_stopped = true;
    return {};
}

} // namespace iso15118::d20::ev::state
