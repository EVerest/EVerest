// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Pionix GmbH and Contributors to EVerest
#include <iso15118/d20/ev/state/authorization.hpp>

#include <algorithm>

#include <iso15118/d20/ev/state/service_discovery.hpp>
#include <iso15118/d20/ev/timeouts.hpp>

#include <iso15118/detail/d20/ev/state/authorization.hpp>
#include <iso15118/detail/d20/ev/state/state_helper.hpp>
#include <iso15118/detail/helper.hpp>

namespace iso15118::d20::ev::state {

namespace authorization {

message_20::AuthorizationRequest create_request(dt::Authorization selected_auth_service) {
    message_20::AuthorizationRequest req;
    req.selected_authorization_service = selected_auth_service;
    if (selected_auth_service == dt::Authorization::EIM) {
        req.authorization_mode.emplace<dt::EIM_ASReqAuthorizationMode>();
    }
    return req;
}

Result handle_response(const message_20::AuthorizationResponse& res) {
    // Only ResponseCode OK authorizes the EV. Warnings such as WARNING_EIMAuthorizationFailure or
    // WARNING_AuthorizationSelectionInvalid (sent with EVSEProcessing=Finished) are rejections and
    // must terminate the session, not be treated as a successful authorization.
    if (res.response_code != dt::ResponseCode::OK) {
        return {Action::Failed};
    }
    if (res.evse_processing == dt::Processing::Finished) {
        return {Action::Done};
    }
    return {Action::Retry};
}

} // namespace authorization

using namespace authorization;

void Authorization::enter() {
    m_ctx.log.enter_state("Authorization");
}

void Authorization::send(Event) {
    if (first_request) {
        const auto& options = m_ctx.session_config.supported_auth_options;
        auto selected = dt::Authorization::EIM;
        if (std::find(options.begin(), options.end(), dt::Authorization::EIM) == options.end() and
            not options.empty()) {
            selected = options.front();
        }
        cached_request = create_request(selected);
        // guard the whole Ongoing loop
        m_ctx.start_timeout(d20::TimeoutType::ONGOING, TIMEOUT_AUTHORIZATION_ONGOING_MS);
        first_request = false;
    }

    // Resend unaltered; only the header timestamp changes [flow spec §3 Authorization].
    m_ctx.setup_header(cached_request.header);
    m_ctx.send_request(cached_request);
    m_ctx.start_timeout(d20::TimeoutType::SEQUENCE, MESSAGE_TIMEOUT_MS);
}

d20::ev::Result Authorization::feed(Event ev) {
    if (ev == Event::SEND_REQUEST) {
        send(ev);
        return {};
    }

    if (ev == Event::CONTROL_MESSAGE) {
        handle_stop_control_event(m_ctx);
        return {};
    }

    if (ev == Event::TIMEOUT) {
        const auto* timeout = m_ctx.get_active_timeout();
        if (timeout and *timeout == d20::TimeoutType::ONGOING) {
            m_ctx.log("Authorization ongoing timeout reached, terminating session");
            ongoing_timeout_reached = true;
        } else {
            m_ctx.log("Authorization message timeout reached, terminating session");
        }
        m_ctx.session_stopped = true;
        return {};
    }

    if (ev != Event::V2GTP_MESSAGE) {
        return {};
    }

    m_ctx.stop_timeout(d20::TimeoutType::SEQUENCE);

    const auto variant = m_ctx.pull_response();

    if (const auto res = variant->get_if<message_20::AuthorizationResponse>()) {
        const auto result = handle_response(*res);

        switch (result.action) {
        case Action::Failed:
            m_ctx.log("Authorization failed, terminating session");
            m_ctx.session_stopped = true;
            return {};
        case Action::Done:
            m_ctx.stop_timeout(d20::TimeoutType::ONGOING);
            if (auto stop = stop_if_pending(m_ctx)) {
                return stop;
            }
            return m_ctx.create_state<ServiceDiscovery>();
        case Action::Retry:
        default:
            if (ongoing_timeout_reached) {
                m_ctx.session_stopped = true;
                return {};
            }
            // resend the cached request (paced by the session driver)
            send(ev);
            return {};
        }
    }

    m_ctx.log("expected AuthorizationRes! But code type id: %d", variant->get_type());
    m_ctx.session_stopped = true;
    return {};
}

} // namespace iso15118::d20::ev::state
