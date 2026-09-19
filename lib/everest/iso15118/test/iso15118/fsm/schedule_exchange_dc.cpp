// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#include <catch2/catch_test_macros.hpp>

#include "helper.hpp"

#include <iso15118/d20/state/schedule_exchange.hpp>

using namespace iso15118;

SCENARIO("ISO15118-20 scheduled DC schedule exchange state transitions") {
    auto evse_setup = create_default_evse_setup();
    evse_setup.dc_limits.charge_limits.power.max = {22, 3};
    std::optional<d20::PauseContext> pause_ctx;
    const session::feedback::Callbacks callbacks{};
    const decltype(d20::Context::session_config) config(evse_setup);
    auto state_helper = FsmStateHelper(config, pause_ctx, callbacks);
    auto& ctx = state_helper.get_context();
    ctx.session = d20::Session(
        d20::SelectedServiceParameters(dt::ServiceCategory::DC, dt::DcConnector::Extended, dt::ControlMode::Scheduled,
                                       dt::MobilityNeedsMode::ProvidedByEvcc, dt::Pricing::NoPricing));

    fsm::v2::FSM<d20::StateBase> fsm{ctx.create_state<d20::state::ScheduleExchange>()};
    message_20::ScheduleExchangeRequest req{};
    req.header.session_id = ctx.session.get_id();
    req.header.timestamp = 1691411798;
    req.max_supporting_points = 12;
    req.control_mode.emplace<dt::Scheduled_SEReqControlMode>();

    state_helper.handle_request(req);
    const auto result = fsm.feed(d20::Event::V2GTP_MESSAGE);

    REQUIRE(result.transitioned());
    REQUIRE(fsm.get_current_state_id() == d20::StateID::DC_CableCheck);
    REQUIRE_FALSE(ctx.session_stopped);
    const auto res = ctx.get_response<message_20::ScheduleExchangeResponse>();
    REQUIRE(res.has_value());
    REQUIRE(res->response_code == dt::ResponseCode::OK);
    REQUIRE(res->processing == dt::Processing::Finished);
    REQUIRE(std::holds_alternative<dt::Scheduled_SEResControlMode>(res->control_mode));
    const auto& schedules = std::get<dt::Scheduled_SEResControlMode>(res->control_mode).schedule_tuple;
    REQUIRE(schedules.size() == 1);
    REQUIRE(schedules.front().schedule_tuple_id == 1);
    REQUIRE_FALSE(schedules.front().discharging_schedule.has_value());
    const auto& charging_schedule = schedules.front().charging_schedule;
    REQUIRE(std::holds_alternative<std::monostate>(charging_schedule.price_schedule));
    REQUIRE(charging_schedule.power_schedule.entries.size() == 1);
    const auto& entry = charging_schedule.power_schedule.entries.front();
    REQUIRE(dt::from_RationalNumber(entry.power) == 22000);
    REQUIRE(entry.duration == dt::SCHEDULED_POWER_DURATION_S);
}
