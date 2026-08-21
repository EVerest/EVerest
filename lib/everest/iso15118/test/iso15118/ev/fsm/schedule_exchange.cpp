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
using message_20::datatypes::Processing;
using message_20::datatypes::ResponseCode;

message_20::ScheduleExchangeResponse make_response(const message_20::Header& header, ResponseCode response_code,
                                                   Processing processing) {
    message_20::ScheduleExchangeResponse res{};
    res.header = header;
    res.response_code = response_code;
    res.processing = processing;
    return res;
}
} // namespace

SCENARIO("ISO15118-20 EV ScheduleExchange sends initial Dynamic request on enter") {
    const ev::feedback::Callbacks callbacks{};
    PrimedState<ev::d20::state::ScheduleExchange> primed{callbacks, no_seed};

    const auto requests = primed.take_requests();
    const auto request_message = requests.get<message_20::ScheduleExchangeRequest>();
    REQUIRE(request_message.has_value());
    REQUIRE(request_message->header.session_id == SESSION_HEADER.session_id);
    REQUIRE(std::holds_alternative<message_20::datatypes::Dynamic_SEReqControlMode>(request_message->control_mode));
}

SCENARIO("ISO15118-20 EV ScheduleExchange fires ev_power_ready and transitions to DC_CableCheck on Finished") {
    bool ev_power_ready_fired = false;
    ev::feedback::Callbacks callbacks{};
    callbacks.ev_power_ready = [&ev_power_ready_fired]() { ev_power_ready_fired = true; };
    PrimedState<ev::d20::state::ScheduleExchange> primed{callbacks, no_seed};

    primed.handle_response(make_response(SESSION_HEADER, ResponseCode::OK, Processing::Finished));
    const auto result = primed.feed(ev::d20::Event::V2GTP_MESSAGE);

    REQUIRE(ev_power_ready_fired == true);
    REQUIRE(result.transitioned() == true);
    REQUIRE(primed.fsm.get_current_state_id() == ev::d20::StateID::DC_CableCheck);
    REQUIRE(primed.ctx.is_session_stopped() == false);
}

SCENARIO(
    "ISO15118-20 EV ScheduleExchange fires ev_power_ready and transitions to DC_CableCheck on Finished for DC_BPT") {
    bool ev_power_ready_fired = false;
    ev::feedback::Callbacks callbacks{};
    callbacks.ev_power_ready = [&ev_power_ready_fired]() { ev_power_ready_fired = true; };
    const auto seed_dc_bpt = [](FsmStateHelper& helper) {
        helper.get_context().set_selected_service(message_20::datatypes::ServiceCategory::DC_BPT);
    };
    PrimedState<ev::d20::state::ScheduleExchange> primed{callbacks, seed_dc_bpt};

    primed.handle_response(make_response(SESSION_HEADER, ResponseCode::OK, Processing::Finished));
    const auto result = primed.feed(ev::d20::Event::V2GTP_MESSAGE);

    REQUIRE(ev_power_ready_fired == true);
    REQUIRE(result.transitioned() == true);
    REQUIRE(primed.fsm.get_current_state_id() == ev::d20::StateID::DC_CableCheck);
    REQUIRE(primed.ctx.is_session_stopped() == false);
}

SCENARIO("ISO15118-20 EV ScheduleExchange fires ev_power_ready and transitions to PowerDelivery on Finished for AC") {
    bool ev_power_ready_fired = false;
    ev::feedback::Callbacks callbacks{};
    callbacks.ev_power_ready = [&ev_power_ready_fired]() { ev_power_ready_fired = true; };
    const auto seed_ac = [](FsmStateHelper& helper) {
        helper.get_context().set_selected_service(message_20::datatypes::ServiceCategory::AC);
    };
    PrimedState<ev::d20::state::ScheduleExchange> primed{callbacks, seed_ac};

    primed.handle_response(make_response(SESSION_HEADER, ResponseCode::OK, Processing::Finished));
    const auto result = primed.feed(ev::d20::Event::V2GTP_MESSAGE);

    REQUIRE(ev_power_ready_fired == true);
    REQUIRE(result.transitioned() == true);
    REQUIRE(primed.fsm.get_current_state_id() == ev::d20::StateID::PowerDelivery);
    REQUIRE(primed.ctx.is_session_stopped() == false);
}

SCENARIO(
    "ISO15118-20 EV ScheduleExchange fires ev_power_ready and transitions to PowerDelivery on Finished for AC_BPT") {
    bool ev_power_ready_fired = false;
    ev::feedback::Callbacks callbacks{};
    callbacks.ev_power_ready = [&ev_power_ready_fired]() { ev_power_ready_fired = true; };
    const auto seed_ac_bpt = [](FsmStateHelper& helper) {
        helper.get_context().set_selected_service(message_20::datatypes::ServiceCategory::AC_BPT);
    };
    PrimedState<ev::d20::state::ScheduleExchange> primed{callbacks, seed_ac_bpt};

    primed.handle_response(make_response(SESSION_HEADER, ResponseCode::OK, Processing::Finished));
    const auto result = primed.feed(ev::d20::Event::V2GTP_MESSAGE);

    REQUIRE(ev_power_ready_fired == true);
    REQUIRE(result.transitioned() == true);
    REQUIRE(primed.fsm.get_current_state_id() == ev::d20::StateID::PowerDelivery);
    REQUIRE(primed.ctx.is_session_stopped() == false);
}

SCENARIO("ISO15118-20 EV ScheduleExchange stays and resends on Ongoing without firing ev_power_ready") {
    bool ev_power_ready_fired = false;
    ev::feedback::Callbacks callbacks{};
    callbacks.ev_power_ready = [&ev_power_ready_fired]() { ev_power_ready_fired = true; };
    PrimedState<ev::d20::state::ScheduleExchange> primed{callbacks, no_seed};

    // Take the request emitted on enter() so the post-feed assertion proves feed()
    // emitted a *fresh* ScheduleExchangeRequest rather than observing the initial one.
    REQUIRE(primed.helper.get_message_exchange().take_request().has_value());
    REQUIRE_FALSE(primed.helper.get_message_exchange().has_request());

    primed.handle_response(make_response(SESSION_HEADER, ResponseCode::OK, Processing::Ongoing));
    const auto result = primed.feed(ev::d20::Event::V2GTP_MESSAGE);

    REQUIRE(ev_power_ready_fired == false);
    REQUIRE(result.transitioned() == false);
    REQUIRE(primed.fsm.get_current_state_id() == ev::d20::StateID::ScheduleExchange);
    REQUIRE(primed.ctx.is_session_stopped() == false);

    const auto requests = primed.take_requests();
    const auto request_message = requests.get<message_20::ScheduleExchangeRequest>();
    REQUIRE(request_message.has_value());
    REQUIRE(request_message->header.session_id == SESSION_HEADER.session_id);
}

SCENARIO("ISO15118-20 EV ScheduleExchange rejects malformed responses") {
    const ev::feedback::Callbacks callbacks{};
    const auto make_fsm = [](FsmStateHelper& helper) {
        auto& ctx = helper.get_context();
        ctx.get_session().set_id(SESSION_HEADER.session_id);
        return fsm::v2::FSM<ev::d20::StateBase>{ctx.create_state<ev::d20::state::ScheduleExchange>()};
    };
    const auto make_ok = [](const message_20::Header& header) {
        return make_response(header, ResponseCode::OK, Processing::Finished);
    };
    const auto wrong = message_20::AuthorizationResponse{SESSION_HEADER, ResponseCode::OK, Processing::Finished};
    check_rejection_paths(callbacks, ev::d20::StateID::ScheduleExchange, make_fsm, make_ok, wrong);
}
