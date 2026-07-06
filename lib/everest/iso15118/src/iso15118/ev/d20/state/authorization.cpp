// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Pionix GmbH and Contributors to EVerest
#include <iso15118/detail/helper.hpp>
#include <iso15118/ev/d20/context.hpp>
#include <iso15118/ev/d20/state/authorization.hpp>
#include <iso15118/ev/d20/state/service_discovery.hpp>
#include <iso15118/ev/detail/d20/context_helper.hpp>
#include <iso15118/message/authorization.hpp>

namespace iso15118::ev::d20::state {

namespace {

// Build an AuthorizationRequest from the service the EVSE offered (chosen in
// AuthorizationSetup). The EV serializer only emits an EIM mode today, but the
// selected service is preserved so the SECC sees the intended choice.
message_20::AuthorizationRequest make_request(Context& ctx) {
    message_20::AuthorizationRequest req;
    setup_header(req.header, ctx.get_session());
    req.selected_authorization_service = ctx.get_evse_session_info().auth_services.front();
    if (req.selected_authorization_service == message_20::datatypes::Authorization::PnC) {
        // TODO(mlitre): Fill in the PnC authorization mode data.
        req.authorization_mode = message_20::datatypes::PnC_ASReqAuthorizationMode{};
    } else {
        req.authorization_mode = message_20::datatypes::EIM_ASReqAuthorizationMode{};
    }
    return req;
}

} // namespace

void Authorization::enter() {
    m_ctx.log.enter_state("Authorization");

    if (m_ctx.get_evse_session_info().auth_services.empty()) {
        logf_error("No authorization services available to send AuthorizationRequest. Abort the session.");
        m_ctx.stop_session();
        return;
    }

    m_ctx.respond(make_request(m_ctx));
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
        m_ctx.respond(make_request(m_ctx));
        return {};
    }

    return m_ctx.create_state<ServiceDiscovery>();
}

} // namespace iso15118::ev::d20::state
