// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#include <iso15118/detail/helper.hpp>
#include <iso15118/ev/d20/context.hpp>
#include <iso15118/ev/d20/state/dc_cable_check.hpp>
#include <iso15118/ev/d20/state/dc_pre_charge.hpp>
#include <iso15118/ev/detail/d20/context_helper.hpp>
#include <iso15118/message/dc_cable_check.hpp>

namespace iso15118::ev::d20::state {

namespace {

using ResponseCode = message_20::datatypes::ResponseCode;

bool check_response_code(ResponseCode response_code) {
    switch (response_code) {
    case ResponseCode::OK:
        return true;
    case ResponseCode::FAILED_SequenceError:
    case ResponseCode::FAILED_UnknownSession:
        return false;
    default:
        logf_warning("Unexpected response code received: %d", static_cast<int>(response_code));
        return iso15118::ev::d20::check_response_code(response_code);
    }
}

message_20::DC_CableCheckRequest make_request(const Session& session) {
    message_20::DC_CableCheckRequest req;
    setup_header(req.header, session);
    return req;
}

} // namespace

void DC_CableCheck::enter() {
    m_ctx.respond(make_request(m_ctx.get_session()));
}

Result DC_CableCheck::feed(Event ev) {
    if (ev != Event::V2GTP_MESSAGE) {
        return {};
    }

    const auto variant = m_ctx.pull_response();

    if (const auto res = variant->get_if<message_20::DC_CableCheckResponse>()) {
        if (res->header.session_id != m_ctx.get_session().get_id()) {
            logf_error("DC_CableCheckResponse session_id does not match current session");
            m_ctx.stop_session(true);
            return {};
        }

        if (not check_response_code(res->response_code)) {
            logf_error("DC_CableCheckResponse rejected with response_code: %d", static_cast<int>(res->response_code));
            m_ctx.stop_session(true);
            return {};
        }

        if (res->processing == message_20::datatypes::Processing::Finished) {
            return m_ctx.create_state<DC_PreCharge>();
        }

        // Processing::Ongoing — re-poll
        m_ctx.respond(make_request(m_ctx.get_session()));
        return {};
    }

    logf_error("Expected DC_CableCheckResponse, got code type id: %d", static_cast<int>(variant->get_type()));
    m_ctx.stop_session(true);
    return {};
}

} // namespace iso15118::ev::d20::state
