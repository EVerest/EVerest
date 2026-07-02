// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#include <iso15118/detail/helper.hpp>
#include <iso15118/ev/d20/state/dc_cable_check.hpp>
#include <iso15118/ev/d20/state/schedule_exchange.hpp>
#include <iso15118/ev/detail/d20/context_helper.hpp>
#include <iso15118/message/schedule_exchange.hpp>

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

message_20::ScheduleExchangeRequest make_request(const SessionId& session) {
    message_20::ScheduleExchangeRequest req;
    setup_header(req.header, session);
    req.max_supporting_points = 12;

    message_20::datatypes::Dynamic_SEReqControlMode control_mode{};
    control_mode.departure_time = 0;
    control_mode.target_energy = message_20::datatypes::RationalNumber{0, 0};
    control_mode.max_energy = message_20::datatypes::RationalNumber{0, 0};
    control_mode.min_energy = message_20::datatypes::RationalNumber{0, 0};
    req.control_mode = control_mode;

    return req;
}

} // namespace

void ScheduleExchange::enter() {
    m_ctx.log.enter_state("ScheduleExchange");
    m_ctx.respond(make_request(m_ctx.get_session()));
}

Result ScheduleExchange::feed(Event ev) {
    if (ev != Event::V2GTP_MESSAGE) {
        return {};
    }

    const auto variant = m_ctx.pull_response();

    const auto res = variant->get_if<message_20::ScheduleExchangeResponse>();
    if (res == nullptr) {
        logf_error("Expected ScheduleExchangeResponse, got code type id: %d", static_cast<int>(variant->get_type()));
        m_ctx.stop_session(true);
        return {};
    }

    if (res->header.session_id != m_ctx.get_session().get_id()) {
        logf_error("ScheduleExchangeResponse session_id does not match current session");
        m_ctx.stop_session(true);
        return {};
    }

    if (not check_response_code(res->response_code)) {
        logf_error("ScheduleExchangeResponse rejected with response_code: %d", static_cast<int>(res->response_code));
        m_ctx.stop_session(true);
        return {};
    }

    if (res->processing == message_20::datatypes::Processing::Finished) {
        m_ctx.feedback.ev_power_ready();
        return m_ctx.create_state<DC_CableCheck>();
    }

    // Processing::Ongoing — re-send the request and stay.
    m_ctx.respond(make_request(m_ctx.get_session()));
    return {};
}

} // namespace iso15118::ev::d20::state
