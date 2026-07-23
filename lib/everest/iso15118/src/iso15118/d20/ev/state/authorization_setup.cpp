// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Pionix GmbH and Contributors to EVerest
#include <iso15118/d20/ev/state/authorization_setup.hpp>

#include <algorithm>

#include <iso15118/d20/ev/state/authorization.hpp>
#include <iso15118/d20/ev/timeouts.hpp>

#include <iso15118/detail/d20/ev/state/authorization_setup.hpp>
#include <iso15118/detail/d20/ev/state/state_helper.hpp>
#include <iso15118/detail/helper.hpp>

namespace iso15118::d20::ev::state {

namespace authorization_setup {

message_20::AuthorizationSetupRequest create_request() {
    return {}; // header-only request, header filled in by the state
}

Result handle_response(const message_20::AuthorizationSetupResponse& res) {
    Result result;
    result.valid = (res.response_code < dt::ResponseCode::FAILED);
    result.certificate_installation_service = res.certificate_installation_service;
    result.offered_auth_services =
        std::vector<dt::Authorization>(res.authorization_services.begin(), res.authorization_services.end());
    return result;
}

} // namespace authorization_setup

using namespace authorization_setup;

void AuthorizationSetup::enter() {
    m_ctx.log.enter_state("AuthorizationSetup");
}

d20::ev::Result AuthorizationSetup::feed(Event ev) {
    if (ev == Event::SEND_REQUEST) {
        auto req = create_request();
        m_ctx.setup_header(req.header);
        m_ctx.send_request(req);
        m_ctx.start_timeout(d20::TimeoutType::SEQUENCE, MESSAGE_TIMEOUT_MS);
        return {};
    }

    if (ev == Event::CONTROL_MESSAGE) {
        handle_stop_control_event(m_ctx);
        return {};
    }

    if (ev == Event::TIMEOUT) {
        m_ctx.log("AuthorizationSetup message timeout reached, terminating session");
        m_ctx.session_stopped = true;
        return {};
    }

    if (ev != Event::V2GTP_MESSAGE) {
        return {};
    }

    m_ctx.stop_timeout(d20::TimeoutType::SEQUENCE);

    const auto variant = m_ctx.pull_response();

    if (const auto res = variant->get_if<message_20::AuthorizationSetupResponse>()) {
        const auto result = handle_response(*res);

        if (not result.valid) {
            m_ctx.log("AuthorizationSetup failed, terminating session");
            m_ctx.session_stopped = true;
            return {};
        }

        if (auto stop = stop_if_pending(m_ctx)) {
            return stop;
        }
        return m_ctx.create_state<Authorization>();
    }

    m_ctx.log("expected AuthorizationSetupRes! But code type id: %d", variant->get_type());
    m_ctx.session_stopped = true;
    return {};
}

} // namespace iso15118::d20::ev::state
