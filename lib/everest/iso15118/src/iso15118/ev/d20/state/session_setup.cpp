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

    if (not check_response_code(res->response_code)) {
        // SessionSetup hand-rolls this check instead of using expect_response, so it has to
        // log the rejection itself.
        logf_error("SessionSetupResponse rejected with response_code: %d", static_cast<int>(res->response_code));
        m_ctx.stop_session();
        return {};
    }

    if (res->evseid.size() <= 0) {
        logf_error("EVSEID is empty. Abort the session.");
        m_ctx.stop_session();
        return {};
    }

    // The EV never requests a resume: see plans/2026-08-04-ev-session-resume-and-pnc.md.
    if (res->response_code == message_20::datatypes::ResponseCode::OK_OldSessionJoined) {
        logf_error("EVSE joined an old session although the EV requested a new one. Abort the session.");
        m_ctx.stop_session();
        return {};
    }

    // A plain OK carries the same authoritative session id as OK_NewSessionEstablished;
    // both must be adopted, and a zero id is unusable either way (every later request
    // would echo zeros).
    if (res->response_code == message_20::datatypes::ResponseCode::OK or
        res->response_code == message_20::datatypes::ResponseCode::OK_NewSessionEstablished) {
        logf_info("New session established by EVSE.");

        if (session_is_zero(res->header.session_id)) {
            logf_error("Returned SessionID is zero although a new session was requested. Abort the session.");
            m_ctx.stop_session();
            return {};
        }

        m_ctx.get_session().set_id(res->header.session_id);
    }

    return {m_ctx.create_state<AuthorizationSetup>()};
}

} // namespace iso15118::ev::d20::state
