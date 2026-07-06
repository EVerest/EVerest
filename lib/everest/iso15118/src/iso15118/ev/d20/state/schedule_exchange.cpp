// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#include <iso15118/detail/helper.hpp>
#include <iso15118/ev/d20/state/dc_cable_check.hpp>
#include <iso15118/ev/d20/state/power_delivery.hpp>
#include <iso15118/ev/d20/state/schedule_exchange.hpp>
#include <iso15118/ev/detail/d20/context_helper.hpp>
#include <iso15118/message/schedule_exchange.hpp>

namespace iso15118::ev::d20::state {

namespace {

constexpr uint16_t MAX_SUPPORTING_POINTS = 12;

message_20::ScheduleExchangeRequest make_request(const SessionId& session) {
    message_20::ScheduleExchangeRequest req;
    setup_header(req.header, session);
    req.max_supporting_points = MAX_SUPPORTING_POINTS;

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
    m_ctx.send_request(make_request(m_ctx.get_session()));
}

Result ScheduleExchange::feed(Event ev) {
    if (ev != Event::V2GTP_MESSAGE) {
        return {};
    }

    const auto variant = m_ctx.pull_response();

    const auto* res = expect_response<message_20::ScheduleExchangeResponse>(m_ctx, *variant);
    if (res == nullptr) {
        return {};
    }

    if (res->processing == message_20::datatypes::Processing::Finished) {
        m_ctx.feedback.ev_power_ready();
        const auto service = m_ctx.selected_service();
        if (service == message_20::datatypes::ServiceCategory::AC or
            service == message_20::datatypes::ServiceCategory::AC_BPT or
            service == message_20::datatypes::ServiceCategory::AC_DER_IEC) {
            return m_ctx.create_state<PowerDelivery>(message_20::datatypes::Progress::Start);
        }
        return m_ctx.create_state<DC_CableCheck>();
    }

    // Processing::Ongoing: re-send the request and stay.
    m_ctx.send_request(make_request(m_ctx.get_session()));
    return {};
}

} // namespace iso15118::ev::d20::state
