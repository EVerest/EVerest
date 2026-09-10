// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#include <iso15118/ev/d2/state/session_setup.hpp>

#include <iso15118/detail/helper.hpp>
#include <iso15118/ev/d2/state/service_discovery.hpp>
#include <iso15118/ev/detail/d2/context_helper.hpp>
#include <iso15118/message_2/session_setup.hpp>

namespace iso15118::ev::d2::state {

namespace dt = message_2::datatypes;

void SessionSetup::enter() {
    logf_debug("Enter state: SessionSetup (ISO 15118-2)");
    // Header carries the resumed id or all zeros (set in the Context ctor).
    message_2::SessionSetupRequest req{};
    req.evcc_id = m_ctx.params().evcc_mac;
    m_ctx.send_request(req);
}

Result SessionSetup::feed(Event ev) {
    if (ev != Event::V2GTP_MESSAGE) {
        return Result::ignored();
    }

    const auto variant = m_ctx.pull_response();

    // The session id arrives in this response, so the generic session-id check does not apply.
    const auto* res = variant->get_if<message_2::SessionSetupResponse>();
    if (res == nullptr) {
        logf_error("expected SessionSetupRes, got message type id: %d", static_cast<int>(variant->get_type()));
        m_ctx.stop_session();
        return Result::stopping();
    }

    const bool resuming = m_ctx.resumed_session_id().has_value();
    if (res->response_code == dt::ResponseCode::OK_OldSessionJoined and not resuming) {
        logf_error("EVSE joined an old session although a new one was requested");
        m_ctx.stop_session();
        return Result::stopping();
    }
    if (res->response_code != dt::ResponseCode::OK_NewSessionEstablished and
        res->response_code != dt::ResponseCode::OK_OldSessionJoined) {
        logf_error("SessionSetupRes rejected with response_code %d", static_cast<int>(res->response_code));
        m_ctx.stop_session();
        return Result::stopping();
    }
    if (res->header.session_id == dt::SessionId{}) {
        logf_error("SessionSetupRes carries an all-zero session id");
        m_ctx.stop_session();
        return Result::stopping();
    }

    m_ctx.set_session_id(res->header.session_id);
    m_ctx.evse_info.evse_id = res->evse_id;
    m_ctx.feedback.evse_id(m_ctx.evse_info.evse_id);
    logf_info("ISO 15118-2 session established, EVSEID %s", m_ctx.evse_info.evse_id.c_str());

    if (auto stop = stop_before_start(m_ctx)) {
        return std::move(*stop);
    }
    return m_ctx.create_state<ServiceDiscovery>();
}

} // namespace iso15118::ev::d2::state
