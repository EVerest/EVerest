// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Pionix GmbH, Roger Bedell and Contributors to EVerest
#include <iso15118/detail/helper.hpp>
#include <iso15118/ev/d20/context.hpp>
#include <iso15118/ev/d20/state/authorization.hpp>
#include <iso15118/ev/d20/state/authorization_setup.hpp>
#include <iso15118/ev/detail/d20/context_helper.hpp>
#include <iso15118/ev/session/feedback.hpp>
#include <iso15118/message/authorization.hpp>
#include <iso15118/message/authorization_setup.hpp>

namespace iso15118::ev::d20::state {

void AuthorizationSetup::enter() {
    m_ctx.log.enter_state("AuthorizationSetup");

    message_20::AuthorizationSetupRequest req;
    setup_header(req.header, m_ctx.get_session());
    m_ctx.send_request(req);
}

Result AuthorizationSetup::feed(Event ev) {
    if (ev != Event::V2GTP_MESSAGE) {
        return {};
    }

    const auto variant = m_ctx.pull_response();

    const auto* res = expect_response<message_20::AuthorizationSetupResponse>(m_ctx, *variant);
    if (res == nullptr) {
        return {};
    }

    auto& info = m_ctx.get_evse_session_info();

    if (res->certificate_installation_service) {
        // TODO(mlitre): Implement certificate installation service
        logf_warning("EVSE supports certificate installation service, but this is not implemented in the EV yet.");
        info.certificate_installation_service = true;
    }

    // Save the offered authorization services so they can be reported to the EV and
    // used to build the AuthorizationRequest.
    info.auth_services = res->authorization_services;

    if (std::holds_alternative<message_20::datatypes::PnC_ASResAuthorizationMode>(res->authorization_mode)) {
        const auto& pnc_auth_mode =
            std::get<message_20::datatypes::PnC_ASResAuthorizationMode>(res->authorization_mode);
        info.supported_providers = pnc_auth_mode.supported_providers;
        info.gen_challenge = pnc_auth_mode.gen_challenge;
    }

    m_ctx.feedback.evse_session_info(info);

    if (info.auth_services.empty()) {
        logf_error("No authorization services offered by the EVSE. Abort the session.");
        m_ctx.stop_session();
        return {};
    }

    return m_ctx.create_state<Authorization>();
}

} // namespace iso15118::ev::d20::state
