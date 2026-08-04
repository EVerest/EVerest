// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#include <algorithm>

#include <iso15118/detail/helper.hpp>
#include <iso15118/ev/d20/context.hpp>
#include <iso15118/ev/d20/state/authorization.hpp>
#include <iso15118/ev/d20/state/service_discovery.hpp>
#include <iso15118/ev/detail/d20/context_helper.hpp>
#include <iso15118/message/authorization.hpp>

namespace iso15118::ev::d20::state {

namespace {

// EIM only: the serializer emits EIM_AReqAuthorizationMode unconditionally
// (message/authorization.cpp), so a PnC selection would go on the wire as EIM.
message_20::AuthorizationRequest make_request(Context& ctx) {
    message_20::AuthorizationRequest req;
    setup_header(req.header, ctx.get_session());
    req.selected_authorization_service = message_20::datatypes::Authorization::EIM;
    req.authorization_mode = message_20::datatypes::EIM_ASReqAuthorizationMode{};
    return req;
}

} // namespace

void Authorization::enter() {
    logf_debug("Enter state: Authorization");

    const auto& auth_services = m_ctx.get_evse_session_info().auth_services;

    if (auth_services.empty()) {
        logf_error("No authorization services available to send AuthorizationRequest. Abort the session.");
        m_ctx.stop_session();
        return;
    }

    const auto offers_eim = std::find(auth_services.begin(), auth_services.end(),
                                      message_20::datatypes::Authorization::EIM) != auth_services.end();

    if (not offers_eim) {
        logf_error("EVSE does not offer EIM authorization and the EV supports no other mode. Abort the session.");
        m_ctx.stop_session();
        return;
    }

    m_ctx.send_request(make_request(m_ctx));
}

Result Authorization::feed(Event ev) {
    if (ev != Event::V2GTP_MESSAGE) {
        return {};
    }

    const auto variant = m_ctx.pull_response();

    const auto* res = expect_response<message_20::AuthorizationResponse>(m_ctx, *variant);
    if (res == nullptr) {
        return {};
    }

    if (res->evse_processing == message_20::datatypes::Processing::Ongoing) {
        m_ctx.send_request(make_request(m_ctx));
        return {};
    }

    return m_ctx.create_state<ServiceDiscovery>();
}

} // namespace iso15118::ev::d20::state
