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
    // Re-joining a paused session: its id goes into the header, otherwise all zeros.
    if (const auto resumed = m_ctx.options().resumed_session_id) {
        m_ctx.get_session().set_id(resumed.value());
    }
    setup_header(req.header, m_ctx.get_session());
    req.evccid = m_ctx.get_evcc_id();
    m_ctx.send_request(req);
}

Result SessionSetup::feed(Event ev) {
    if (ev != Event::V2GTP_MESSAGE) {
        return Result::ignored();
    }

    const auto variant = m_ctx.pull_response();

    // SessionSetup establishes the session id (it arrives IN the response), so the
    // generic session-id check does not apply; validate the returned id here instead.
    const auto res = variant->get_if<message_20::SessionSetupResponse>();
    if (res == nullptr) {
        logf_error("expected SessionSetupResponse! But code type id: %d", static_cast<int>(variant->get_type()));
        m_ctx.stop_session();
        return Result::stopping();
    }

    using message_20::datatypes::ResponseCode;

    const bool resuming = m_ctx.options().resumed_session_id.has_value();

    // OK_OldSessionJoined is only valid when this EV asked to resume.
    if (res->response_code == ResponseCode::OK_OldSessionJoined and not resuming) {
        logf_error("EVSE joined an old session although this EV requested a new one; aborting");
        m_ctx.stop_session();
        return Result::stopping();
    }

    // Strict on purpose: this EV exists to surface SECC deviations, so plain OK and every
    // WARNING_* are rejected here even though the generic table accepts them elsewhere.
    if (res->response_code != ResponseCode::OK_NewSessionEstablished and
        res->response_code != ResponseCode::OK_OldSessionJoined) {
        logf_error("SessionSetupResponse rejected with response_code %d: a new session requires "
                   "OK_NewSessionEstablished",
                   static_cast<int>(res->response_code));
        m_ctx.stop_session();
        return Result::stopping();
    }

    if (res->evseid.empty()) {
        logf_error("EVSEID is empty. Abort the session.");
        m_ctx.stop_session();
        return Result::stopping();
    }

    if (res->response_code == ResponseCode::OK_OldSessionJoined) {
        logf_info("Paused session re-joined by EVSE.");
        if (res->header.session_id != m_ctx.get_session().get_id()) {
            logf_error("OK_OldSessionJoined with a different session id. Abort the session.");
            m_ctx.stop_session();
            return Result::stopping();
        }
    } else {
        logf_info("New session established by EVSE.");

        if (session_is_zero(res->header.session_id)) {
            logf_error("Returned SessionID is zero although a new session was requested. Abort the session.");
            m_ctx.stop_session();
            return Result::stopping();
        }

        m_ctx.get_session().set_id(res->header.session_id);
    }

    m_ctx.feedback.evse_id(res->evseid);

    return {m_ctx.create_state<AuthorizationSetup>()};
}

} // namespace iso15118::ev::d20::state
