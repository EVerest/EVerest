// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#include <algorithm>

#include <iso15118/detail/helper.hpp>
#include <iso15118/ev/d20/context.hpp>
#include <iso15118/ev/d20/state/authorization.hpp>
#include <iso15118/ev/d20/state/service_discovery.hpp>
#include <iso15118/ev/d20/state/stop_before_start.hpp>
#include <iso15118/ev/detail/d20/context_helper.hpp>
#include <iso15118/message/authorization.hpp>

namespace iso15118::ev::d20::state {

namespace {

// TODO(mlitre): offer PnC once the EV has a contract certificate.
message_20::AuthorizationRequest make_request() {
    message_20::AuthorizationRequest req;
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

    m_ctx.send_request(make_request());
}

Result Authorization::feed(Event ev) {
    if (ev != Event::V2GTP_MESSAGE) {
        return Result::ignored();
    }

    const auto variant = m_ctx.pull_response();

    const auto* res = expect_response<message_20::AuthorizationResponse>(m_ctx, *variant);
    if (res == nullptr) {
        return Result::stopping();
    }

    if (auto stop = stop_before_start(m_ctx)) {
        return std::move(*stop);
    }

    // A declined attempt is not the end of the session: the driver may authorize again at the
    // EVSE, so the EV keeps asking. The state's ongoing guard bounds the loop and stops the
    // session when it expires.
    const bool declined =
        res->response_code == message_20::datatypes::ResponseCode::WARNING_EIMAuthorizationFailure or
        res->response_code == message_20::datatypes::ResponseCode::WARNING_AuthorizationSelectionInvalid;
    if (declined) {
        logf_warning("EVSE declined authorization with response_code %d; retrying until the authorization timeout",
                     static_cast<int>(res->response_code));
    }

    if (declined or res->evse_processing == message_20::datatypes::Processing::Ongoing) {
        m_ctx.send_request(make_request());
        return Result::awaiting();
    }

    return m_ctx.create_state<ServiceDiscovery>();
}

} // namespace iso15118::ev::d20::state
