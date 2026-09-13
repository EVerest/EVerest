// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#include <catch2/catch_test_macros.hpp>

#include "helper.hpp"

#include <iso15118/ev/d20/state/schedule_exchange.hpp>
#include <iso15118/message/authorization.hpp>
#include <iso15118/message/schedule_exchange.hpp>
#include <iso15118/message/type.hpp>

using namespace iso15118;

namespace {
message_20::ScheduleExchangeResponse make_response(message_20::datatypes::ResponseCode response_code,
                                                   message_20::datatypes::Processing processing) {
    message_20::ScheduleExchangeResponse res{};
    res.header.session_id = SESSION_HEADER.session_id;
    res.response_code = response_code;
    res.processing = processing;
    return res;
}
} // namespace

SCENARIO("ISO15118-20 EV ScheduleExchange sends initial Dynamic request on enter") {
    const ev::feedback::Callbacks callbacks{};
    auto state_helper = FsmStateHelper(callbacks);
    auto& ctx = state_helper.get_context();
    ctx.get_session().set_id(SESSION_HEADER.session_id);

    fsm::v2::FSM<ev::d20::StateBase> fsm{ctx.create_state<ev::d20::state::ScheduleExchange>()};

    const auto requests = drain_requests(state_helper.get_message_exchange());
    const auto request_message = requests.get<message_20::ScheduleExchangeRequest>();
    REQUIRE(request_message.has_value());
    REQUIRE(request_message->header.session_id == SESSION_HEADER.session_id);
    REQUIRE(std::holds_alternative<message_20::datatypes::Dynamic_SEReqControlMode>(request_message->control_mode));
}

SCENARIO("ISO15118-20 EV ScheduleExchange fires ev_power_ready and transitions to DC_CableCheck on Finished") {
    bool ev_power_ready_fired = false;
    ev::feedback::Callbacks callbacks{};
    callbacks.ev_power_ready = [&ev_power_ready_fired]() { ev_power_ready_fired = true; };

    auto state_helper = FsmStateHelper(callbacks);
    auto& ctx = state_helper.get_context();
    ctx.get_session().set_id(SESSION_HEADER.session_id);

    fsm::v2::FSM<ev::d20::StateBase> fsm{ctx.create_state<ev::d20::state::ScheduleExchange>()};

    state_helper.handle_response(
        make_response(message_20::datatypes::ResponseCode::OK, message_20::datatypes::Processing::Finished));

    const auto result = fsm.feed(ev::d20::Event::V2GTP_MESSAGE);

    REQUIRE(ev_power_ready_fired == true);
    REQUIRE(result.transitioned() == true);
    REQUIRE(fsm.get_current_state_id() == ev::d20::StateID::DC_CableCheck);
    REQUIRE(ctx.is_session_stopped() == false);
}

SCENARIO("ISO15118-20 EV ScheduleExchange stays and resends on Ongoing without firing ev_power_ready") {
    bool ev_power_ready_fired = false;
    ev::feedback::Callbacks callbacks{};
    callbacks.ev_power_ready = [&ev_power_ready_fired]() { ev_power_ready_fired = true; };

    auto state_helper = FsmStateHelper(callbacks);
    auto& ctx = state_helper.get_context();
    ctx.get_session().set_id(SESSION_HEADER.session_id);

    fsm::v2::FSM<ev::d20::StateBase> fsm{ctx.create_state<ev::d20::state::ScheduleExchange>()};

    // Drain the request emitted on enter() so the post-feed assertion proves feed()
    // emitted a *fresh* ScheduleExchangeRequest rather than observing the initial one.
    REQUIRE(state_helper.get_message_exchange().take_request().has_value());
    REQUIRE_FALSE(state_helper.get_message_exchange().has_request());

    state_helper.handle_response(
        make_response(message_20::datatypes::ResponseCode::OK, message_20::datatypes::Processing::Ongoing));

    const auto result = fsm.feed(ev::d20::Event::V2GTP_MESSAGE);

    REQUIRE(ev_power_ready_fired == false);
    REQUIRE(result.transitioned() == false);
    REQUIRE(fsm.get_current_state_id() == ev::d20::StateID::ScheduleExchange);
    REQUIRE(ctx.is_session_stopped() == false);

    const auto requests = drain_requests(state_helper.get_message_exchange());
    const auto request_message = requests.get<message_20::ScheduleExchangeRequest>();
    REQUIRE(request_message.has_value());
    REQUIRE(request_message->header.session_id == SESSION_HEADER.session_id);
}

SCENARIO("ISO15118-20 EV ScheduleExchange stops session on FAILED response") {
    const ev::feedback::Callbacks callbacks{};
    auto state_helper = FsmStateHelper(callbacks);
    auto& ctx = state_helper.get_context();
    ctx.get_session().set_id(SESSION_HEADER.session_id);

    fsm::v2::FSM<ev::d20::StateBase> fsm{ctx.create_state<ev::d20::state::ScheduleExchange>()};

    state_helper.handle_response(
        make_response(message_20::datatypes::ResponseCode::FAILED, message_20::datatypes::Processing::Finished));

    const auto result = fsm.feed(ev::d20::Event::V2GTP_MESSAGE);

    REQUIRE(result.transitioned() == false);
    REQUIRE(fsm.get_current_state_id() == ev::d20::StateID::ScheduleExchange);
    REQUIRE(ctx.is_session_stopped() == true);
}

SCENARIO("ISO15118-20 EV ScheduleExchange stops session on wrong variant") {
    const ev::feedback::Callbacks callbacks{};
    auto state_helper = FsmStateHelper(callbacks);
    auto& ctx = state_helper.get_context();
    ctx.get_session().set_id(SESSION_HEADER.session_id);

    fsm::v2::FSM<ev::d20::StateBase> fsm{ctx.create_state<ev::d20::state::ScheduleExchange>()};

    const auto wrong = message_20::AuthorizationResponse{SESSION_HEADER, message_20::datatypes::ResponseCode::OK,
                                                         message_20::datatypes::Processing::Finished};
    state_helper.handle_response(wrong);

    const auto result = fsm.feed(ev::d20::Event::V2GTP_MESSAGE);

    REQUIRE(result.transitioned() == false);
    REQUIRE(fsm.get_current_state_id() == ev::d20::StateID::ScheduleExchange);
    REQUIRE(ctx.is_session_stopped() == true);
}

SCENARIO("ISO15118-20 EV ScheduleExchange stops session on mismatched response session_id") {
    const ev::feedback::Callbacks callbacks{};
    auto state_helper = FsmStateHelper(callbacks);
    auto& ctx = state_helper.get_context();
    ctx.get_session().set_id(SESSION_HEADER.session_id);

    fsm::v2::FSM<ev::d20::StateBase> fsm{ctx.create_state<ev::d20::state::ScheduleExchange>()};

    message_20::ScheduleExchangeResponse res{};
    res.header.session_id = WRONG_HEADER.session_id;
    res.response_code = message_20::datatypes::ResponseCode::OK;
    res.processing = message_20::datatypes::Processing::Finished;
    state_helper.handle_response(res);

    const auto result = fsm.feed(ev::d20::Event::V2GTP_MESSAGE);

    REQUIRE(result.transitioned() == false);
    REQUIRE(fsm.get_current_state_id() == ev::d20::StateID::ScheduleExchange);
    REQUIRE(ctx.is_session_stopped() == true);
}
