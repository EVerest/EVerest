// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#include <catch2/catch_test_macros.hpp>

#include "helper.hpp"

#include <iso15118/d20/state/schedule_exchange.hpp>

#include <iso15118/message/schedule_exchange.hpp>

using namespace iso15118;

namespace dt = message_20::datatypes;

SCENARIO("ISO15118-20 scheduled DC schedule exchange state transitions") {

    auto evse_setup = create_default_evse_setup();
    evse_setup.dc_limits.charge_limits.power.max = {22, 3};

    std::optional<d20::PauseContext> pause_ctx{std::nullopt};
    session::feedback::Callbacks callbacks{};

    auto state_helper = FsmStateHelper(d20::SessionConfig(evse_setup), pause_ctx, callbacks);
    auto& ctx = state_helper.get_context();

    GIVEN("Good case - Scheduled control mode") {
        ctx.session = d20::Session(d20::SelectedServiceParameters(
            dt::ServiceCategory::DC, dt::DcConnector::Extended, dt::ControlMode::Scheduled,
            dt::MobilityNeedsMode::ProvidedByEvcc, dt::Pricing::NoPricing));

        // The FSM constructor enters the initial state, so the session has to be set up before it.
        fsm::v2::FSM<d20::StateBase> fsm{ctx.create_state<d20::state::ScheduleExchange>()};

        message_20::ScheduleExchangeRequest req;
        req.header.session_id = ctx.session.get_id();
        req.header.timestamp = 1691411798;
        req.max_supporting_points = 12;
        req.control_mode.emplace<dt::Scheduled_SEReqControlMode>();

        state_helper.handle_request(req);
        const auto result = fsm.feed(d20::Event::V2GTP_MESSAGE);

        THEN("Check state transition and response") {
            REQUIRE(result.transitioned() == true);
            REQUIRE(fsm.get_current_state_id() == d20::StateID::DC_CableCheck);
            REQUIRE(ctx.session_stopped == false);

            const auto response_message = ctx.get_response<message_20::ScheduleExchangeResponse>();
            REQUIRE(response_message.has_value());

            const auto& res = response_message.value();
            REQUIRE(res.response_code == dt::ResponseCode::OK);
            REQUIRE(res.processing == dt::Processing::Finished);

            REQUIRE(std::holds_alternative<dt::Scheduled_SEResControlMode>(res.control_mode));
            const auto& schedules = std::get<dt::Scheduled_SEResControlMode>(res.control_mode).schedule_tuple;
            REQUIRE(schedules.size() == 1);

            const auto& schedule = schedules.front();
            REQUIRE(schedule.schedule_tuple_id == 1);
            REQUIRE_FALSE(schedule.discharging_schedule.has_value());

            const auto& charging_schedule = schedule.charging_schedule;
            REQUIRE(std::holds_alternative<std::monostate>(charging_schedule.price_schedule));
            REQUIRE(charging_schedule.power_schedule.entries.size() == 1);

            const auto& entry = charging_schedule.power_schedule.entries.front();
            REQUIRE(dt::from_RationalNumber(entry.power) == 22000);
            REQUIRE(entry.duration == dt::SCHEDULED_POWER_DURATION_S);
        }
    }
}
