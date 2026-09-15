// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH, Roger Bedell, and Contributors to EVerest
#include <algorithm>

#include <iso15118/detail/helper.hpp>
#include <iso15118/ev/d20/context.hpp>
#include <iso15118/ev/d20/state/authorization_setup.hpp>
#include <iso15118/ev/d20/state/session_setup.hpp>
#include <iso15118/ev/detail/d20/context_helper.hpp>
#include <iso15118/message/session_setup.hpp>

namespace iso15118::ev::d20::state {

namespace {

bool session_is_zero(const message_20::datatypes::SessionId& session_id) {
    return std::all_of(session_id.begin(), session_id.end(), [](int i) { return i == 0; });
}

} // namespace

void SessionSetup::enter() {
    logf_debug("Enter state: SessionSetup");

    message_20::SessionSetupRequest req{};
    setup_header(req.header, m_ctx.get_session());
    req.evccid = m_ctx.get_evcc_id();
    m_ctx.send_request(req);
}

Result SessionSetup::feed(Event ev) {
    if (ev != Event::V2GTP_MESSAGE) {
        return {};
    }

    const auto variant = m_ctx.pull_response();

    // SessionSetup establishes the session id (it arrives IN the response), so the
    // generic session-id check does not apply; validate the returned id here instead.
    const auto res = variant->get_if<message_20::SessionSetupResponse>();
    if (res == nullptr) {
        logf_error("expected SessionSetupResponse! But code type id: %d", variant->get_type());
        m_ctx.stop_session();
        return {};
    }

    using message_20::datatypes::ResponseCode;

    // SessionSetupRequest always carries a zero session id, so the EV never asks to
    // resume and OK_OldSessionJoined is a protocol violation.
    if (res->response_code == ResponseCode::OK_OldSessionJoined) {
        logf_error("EVSE joined an old session although this EV requested a new one; aborting");
        m_ctx.stop_session();
        return {};
    }

    // Strict on purpose: this EV exists to surface SECC deviations, so plain OK and every
    // WARNING_* are rejected here even though the generic table accepts them elsewhere.
    if (res->response_code != ResponseCode::OK_NewSessionEstablished) {
        logf_error("SessionSetupResponse rejected with response_code %d: a new session requires "
                   "OK_NewSessionEstablished",
                   static_cast<int>(res->response_code));
        m_ctx.stop_session();
        return {};
    }

    if (res->evseid.size() <= 0) {
        logf_error("EVSEID is empty. Abort the session.");
        m_ctx.stop_session();
        return {};
    }

    logf_info("New session established by EVSE.");

    if (session_is_zero(res->header.session_id)) {
        logf_error("Returned SessionID is zero although a new session was requested. Abort the session.");
        m_ctx.stop_session();
        return {};
    }

    m_ctx.get_session().set_id(res->header.session_id);

    return {m_ctx.create_state<AuthorizationSetup>()};
}

} // namespace iso15118::ev::d20::state
