// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#include <cstdint>
#include <variant>

#include <iso15118/detail/helper.hpp>
#include <iso15118/ev/d20/context.hpp>
#include <iso15118/ev/d20/state/dc_cable_check.hpp>
#include <iso15118/ev/d20/state/power_delivery.hpp>
#include <iso15118/ev/d20/state/schedule_exchange.hpp>
#include <iso15118/ev/d20/state/stop_before_start.hpp>
#include <iso15118/ev/detail/d20/context_helper.hpp>
#include <iso15118/message/schedule_exchange.hpp>

namespace iso15118::ev::d20::state {

namespace {

namespace dt = message_20::datatypes;

constexpr uint16_t MAX_SUPPORTING_POINTS = 12;

constexpr dt::NumericId MIN_SCHEDULE_TUPLE_ID = 1;
constexpr dt::NumericId MAX_SCHEDULE_TUPLE_ID = 255;

message_20::ScheduleExchangeRequest make_request(const SessionId& session, dt::ControlMode control_mode) {
    message_20::ScheduleExchangeRequest req;
    setup_header(req.header, session);
    req.max_supporting_points = MAX_SUPPORTING_POINTS;

    if (control_mode == dt::ControlMode::Scheduled) {
        // Every Scheduled_SEReqControlMode field is optional: the EV states no energy window,
        // no departure time and no EVEnergyOffer, leaving the schedule to the SECC.
        req.control_mode = dt::Scheduled_SEReqControlMode{};
        return req;
    }

    dt::Dynamic_SEReqControlMode dynamic_mode{};
    dynamic_mode.departure_time = 0;
    dynamic_mode.target_energy = dt::RationalNumber{0, 0};
    dynamic_mode.max_energy = dt::RationalNumber{0, 0};
    dynamic_mode.min_energy = dt::RationalNumber{0, 0};
    req.control_mode = dynamic_mode;

    return req;
}

} // namespace

void ScheduleExchange::enter() {
    logf_debug("Enter state: ScheduleExchange");
    m_ctx.send_request(make_request(m_ctx.get_session(), m_ctx.selected_control_mode()));
}

Result ScheduleExchange::feed(Event ev) {
    if (ev != Event::V2GTP_MESSAGE) {
        return Result::ignored();
    }

    const auto variant = m_ctx.pull_response();

    const auto* res = expect_response<message_20::ScheduleExchangeResponse>(m_ctx, *variant);
    if (res == nullptr) {
        return Result::stopping();
    }

    if (auto stop = stop_before_start(m_ctx)) {
        return std::move(*stop);
    }

    if (res->processing == dt::Processing::Finished) {
        if (m_ctx.selected_control_mode() == dt::ControlMode::Scheduled) {
            const auto* mode = std::get_if<dt::Scheduled_SEResControlMode>(&res->control_mode);
            if (mode == nullptr) {
                logf_error("ScheduleExchangeResponse offers a control mode the EV did not request");
                m_ctx.stop_session();
                return Result::stopping();
            }
            if (mode->schedule_tuple.empty()) {
                logf_error("Scheduled ScheduleExchangeResponse carries no schedule tuple");
                m_ctx.stop_session();
                return Result::stopping();
            }
            // PowerDeliveryReq(Start) has to name one of the offered tuples; the first is taken.
            const auto tuple_id = mode->schedule_tuple[0].schedule_tuple_id;
            if (tuple_id < MIN_SCHEDULE_TUPLE_ID or tuple_id > MAX_SCHEDULE_TUPLE_ID) {
                logf_error("Scheduled ScheduleExchangeResponse offers ScheduleTupleID %u outside 1..255",
                           static_cast<unsigned>(tuple_id));
                m_ctx.stop_session();
                return Result::stopping();
            }
            m_ctx.set_selected_schedule_tuple_id(static_cast<uint8_t>(tuple_id));
        }

        m_ctx.feedback.ev_power_ready();
        if (m_ctx.is_ac_family()) {
            return m_ctx.create_state<PowerDelivery>(dt::Progress::Start);
        }
        return m_ctx.create_state<DC_CableCheck>();
    }

    // Processing::Ongoing: re-send the request and stay.
    m_ctx.send_request(make_request(m_ctx.get_session(), m_ctx.selected_control_mode()));
    return Result::awaiting();
}

} // namespace iso15118::ev::d20::state
