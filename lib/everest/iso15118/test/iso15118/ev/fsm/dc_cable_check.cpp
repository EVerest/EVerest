// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#include <catch2/catch_test_macros.hpp>

#include "helper.hpp"

#include <iso15118/ev/d20/state/dc_cable_check.hpp>
#include <iso15118/ev/d20/state/dc_pre_charge.hpp>
#include <iso15118/message/authorization.hpp>
#include <iso15118/message/dc_cable_check.hpp>
#include <iso15118/message/type.hpp>

using namespace iso15118;

SCENARIO("ISO15118-20 EV DC_CableCheck sends initial request on enter") {
    const ev::feedback::Callbacks callbacks{};
    auto state_helper = FsmStateHelper(callbacks);
    auto& ctx = state_helper.get_context();
    ctx.get_session().set_id(SESSION_HEADER.session_id);

    fsm::v2::FSM<ev::d20::StateBase> fsm{ctx.create_state<ev::d20::state::DC_CableCheck>()};

    const auto requests = drain_requests(state_helper.get_message_exchange());
    const auto request_message = requests.get<message_20::DC_CableCheckRequest>();
    REQUIRE(request_message.has_value());
    REQUIRE(request_message->header.session_id == SESSION_HEADER.session_id);
}

SCENARIO("ISO15118-20 EV DC_CableCheck stays and resends on Ongoing") {
    const ev::feedback::Callbacks callbacks{};
    auto state_helper = FsmStateHelper(callbacks);
    auto& ctx = state_helper.get_context();
    ctx.get_session().set_id(SESSION_HEADER.session_id);

    fsm::v2::FSM<ev::d20::StateBase> fsm{ctx.create_state<ev::d20::state::DC_CableCheck>()};

    // Consume the request emitted on enter() the way the reactor transmits it before the
    // response arrives, so the post-feed assertion proves feed() emitted a *fresh*
    // DC_CableCheckRequest rather than observing the initial one.
    REQUIRE(state_helper.get_message_exchange().take_request().has_value());
    REQUIRE_FALSE(state_helper.get_message_exchange().has_request());

    const auto res = message_20::DC_CableCheckResponse{SESSION_HEADER, message_20::datatypes::ResponseCode::OK,
                                                       message_20::datatypes::Processing::Ongoing};
    state_helper.handle_response(res);

    const auto result = fsm.feed(ev::d20::Event::V2GTP_MESSAGE);

    REQUIRE(result.transitioned() == false);
    REQUIRE(fsm.get_current_state_id() == ev::d20::StateID::DC_CableCheck);
    REQUIRE(ctx.is_session_stopped() == false);

    const auto requests = drain_requests(state_helper.get_message_exchange());
    const auto request_message = requests.get<message_20::DC_CableCheckRequest>();
    REQUIRE(request_message.has_value());
    REQUIRE(request_message->header.session_id == SESSION_HEADER.session_id);
}

SCENARIO("ISO15118-20 EV DC_CableCheck transitions to DC_PreCharge on Finished") {
    const ev::feedback::Callbacks callbacks{};
    auto state_helper = FsmStateHelper(callbacks);
    auto& ctx = state_helper.get_context();
    ctx.get_session().set_id(SESSION_HEADER.session_id);

    fsm::v2::FSM<ev::d20::StateBase> fsm{ctx.create_state<ev::d20::state::DC_CableCheck>()};

    const auto res = message_20::DC_CableCheckResponse{SESSION_HEADER, message_20::datatypes::ResponseCode::OK,
                                                       message_20::datatypes::Processing::Finished};
    state_helper.handle_response(res);

    const auto result = fsm.feed(ev::d20::Event::V2GTP_MESSAGE);

    REQUIRE(result.transitioned() == true);
    REQUIRE(fsm.get_current_state_id() == ev::d20::StateID::DC_PreCharge);
    REQUIRE(ctx.is_session_stopped() == false);
}

SCENARIO("ISO15118-20 EV DC_CableCheck stops session on FAILED response") {
    const ev::feedback::Callbacks callbacks{};
    auto state_helper = FsmStateHelper(callbacks);
    auto& ctx = state_helper.get_context();
    ctx.get_session().set_id(SESSION_HEADER.session_id);

    fsm::v2::FSM<ev::d20::StateBase> fsm{ctx.create_state<ev::d20::state::DC_CableCheck>()};

    const auto res = message_20::DC_CableCheckResponse{SESSION_HEADER, message_20::datatypes::ResponseCode::FAILED,
                                                       message_20::datatypes::Processing::Ongoing};
    state_helper.handle_response(res);

    const auto result = fsm.feed(ev::d20::Event::V2GTP_MESSAGE);

    REQUIRE(result.transitioned() == false);
    REQUIRE(fsm.get_current_state_id() == ev::d20::StateID::DC_CableCheck);
    REQUIRE(ctx.is_session_stopped() == true);
}

SCENARIO("ISO15118-20 EV DC_CableCheck stops session on wrong variant") {
    const ev::feedback::Callbacks callbacks{};
    auto state_helper = FsmStateHelper(callbacks);
    auto& ctx = state_helper.get_context();
    ctx.get_session().set_id(SESSION_HEADER.session_id);

    fsm::v2::FSM<ev::d20::StateBase> fsm{ctx.create_state<ev::d20::state::DC_CableCheck>()};

    const auto wrong = message_20::AuthorizationResponse{SESSION_HEADER, message_20::datatypes::ResponseCode::OK,
                                                         message_20::datatypes::Processing::Finished};
    state_helper.handle_response(wrong);

    const auto result = fsm.feed(ev::d20::Event::V2GTP_MESSAGE);

    REQUIRE(result.transitioned() == false);
    REQUIRE(fsm.get_current_state_id() == ev::d20::StateID::DC_CableCheck);
    REQUIRE(ctx.is_session_stopped() == true);
}

SCENARIO("ISO15118-20 EV DC_CableCheck stops session on mismatched response session_id") {
    const ev::feedback::Callbacks callbacks{};
    auto state_helper = FsmStateHelper(callbacks);
    auto& ctx = state_helper.get_context();
    ctx.get_session().set_id(SESSION_HEADER.session_id);

    fsm::v2::FSM<ev::d20::StateBase> fsm{ctx.create_state<ev::d20::state::DC_CableCheck>()};

    const auto res = message_20::DC_CableCheckResponse{WRONG_HEADER, message_20::datatypes::ResponseCode::OK,
                                                       message_20::datatypes::Processing::Finished};
    state_helper.handle_response(res);

    const auto result = fsm.feed(ev::d20::Event::V2GTP_MESSAGE);

    REQUIRE(result.transitioned() == false);
    REQUIRE(fsm.get_current_state_id() == ev::d20::StateID::DC_CableCheck);
    REQUIRE(ctx.is_session_stopped() == true);
}

SCENARIO("ISO15118-20 EV DC_CableCheck stops session on FAILED_UnknownSession") {
    const ev::feedback::Callbacks callbacks{};
    auto state_helper = FsmStateHelper(callbacks);
    auto& ctx = state_helper.get_context();
    ctx.get_session().set_id(SESSION_HEADER.session_id);

    fsm::v2::FSM<ev::d20::StateBase> fsm{ctx.create_state<ev::d20::state::DC_CableCheck>()};

    const auto res =
        message_20::DC_CableCheckResponse{SESSION_HEADER, message_20::datatypes::ResponseCode::FAILED_UnknownSession,
                                          message_20::datatypes::Processing::Finished};
    state_helper.handle_response(res);

    const auto result = fsm.feed(ev::d20::Event::V2GTP_MESSAGE);

    REQUIRE(result.transitioned() == false);
    REQUIRE(fsm.get_current_state_id() == ev::d20::StateID::DC_CableCheck);
    REQUIRE(ctx.is_session_stopped() == true);
}
