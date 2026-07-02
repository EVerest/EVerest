// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#include <catch2/catch_test_macros.hpp>

#include "helper.hpp"

#include <iso15118/ev/d20/state/authorization.hpp>
#include <iso15118/message/authorization.hpp>
#include <iso15118/message/service_discovery.hpp>
#include <iso15118/message/type.hpp>

using namespace iso15118;

SCENARIO("ISO15118-20 EV Authorization transitions to ServiceDiscovery on Finished") {
    const ev::feedback::Callbacks callbacks{};
    auto state_helper = FsmStateHelper(callbacks);
    auto& ctx = state_helper.get_context();
    ctx.get_session().set_id(SESSION_HEADER.session_id);
    ctx.evse_session_info.auth_services = {message_20::datatypes::Authorization::EIM};

    fsm::v2::FSM<ev::d20::StateBase> fsm{ctx.create_state<ev::d20::state::Authorization>()};

    const auto res = message_20::AuthorizationResponse{SESSION_HEADER, message_20::datatypes::ResponseCode::OK,
                                                       message_20::datatypes::Processing::Finished};
    state_helper.handle_response(res);

    const auto result = fsm.feed(ev::d20::Event::V2GTP_MESSAGE);

    REQUIRE(result.transitioned() == true);
    REQUIRE(fsm.get_current_state_id() == ev::d20::StateID::ServiceDiscovery);
    REQUIRE(ctx.is_session_stopped() == false);

    const auto requests = drain_requests(state_helper.get_message_exchange());
    const auto request_message = requests.get<message_20::ServiceDiscoveryRequest>();
    REQUIRE(request_message.has_value());
    REQUIRE(request_message->header.session_id == SESSION_HEADER.session_id);
}

SCENARIO("ISO15118-20 EV Authorization stays and resends on Ongoing") {
    const ev::feedback::Callbacks callbacks{};
    auto state_helper = FsmStateHelper(callbacks);
    auto& ctx = state_helper.get_context();
    ctx.get_session().set_id(SESSION_HEADER.session_id);
    ctx.evse_session_info.auth_services = {message_20::datatypes::Authorization::EIM};

    fsm::v2::FSM<ev::d20::StateBase> fsm{ctx.create_state<ev::d20::state::Authorization>()};

    const auto res = message_20::AuthorizationResponse{SESSION_HEADER, message_20::datatypes::ResponseCode::OK,
                                                       message_20::datatypes::Processing::Ongoing};
    state_helper.handle_response(res);

    const auto result = fsm.feed(ev::d20::Event::V2GTP_MESSAGE);

    REQUIRE(result.transitioned() == false);
    REQUIRE(fsm.get_current_state_id() == ev::d20::StateID::Authorization);
    REQUIRE(ctx.is_session_stopped() == false);

    const auto requests = drain_requests(state_helper.get_message_exchange());
    const auto request_message = requests.get<message_20::AuthorizationRequest>();
    REQUIRE(request_message.has_value());
    REQUIRE(request_message->header.session_id == SESSION_HEADER.session_id);
    REQUIRE(request_message->selected_authorization_service == message_20::datatypes::Authorization::EIM);
}

SCENARIO("ISO15118-20 EV Authorization stops session on FAILED response") {
    const ev::feedback::Callbacks callbacks{};
    auto state_helper = FsmStateHelper(callbacks);
    auto& ctx = state_helper.get_context();
    ctx.get_session().set_id(SESSION_HEADER.session_id);
    ctx.evse_session_info.auth_services = {message_20::datatypes::Authorization::EIM};

    fsm::v2::FSM<ev::d20::StateBase> fsm{ctx.create_state<ev::d20::state::Authorization>()};

    const auto res = message_20::AuthorizationResponse{SESSION_HEADER, message_20::datatypes::ResponseCode::FAILED,
                                                       message_20::datatypes::Processing::Finished};
    state_helper.handle_response(res);

    const auto result = fsm.feed(ev::d20::Event::V2GTP_MESSAGE);

    REQUIRE(result.transitioned() == false);
    REQUIRE(fsm.get_current_state_id() == ev::d20::StateID::Authorization);
    REQUIRE(ctx.is_session_stopped() == true);
}

SCENARIO("ISO15118-20 EV Authorization stops session on wrong variant") {
    const ev::feedback::Callbacks callbacks{};
    auto state_helper = FsmStateHelper(callbacks);
    auto& ctx = state_helper.get_context();
    ctx.get_session().set_id(SESSION_HEADER.session_id);
    ctx.evse_session_info.auth_services = {message_20::datatypes::Authorization::EIM};

    fsm::v2::FSM<ev::d20::StateBase> fsm{ctx.create_state<ev::d20::state::Authorization>()};

    const auto wrong = message_20::ServiceDiscoveryResponse{};
    state_helper.handle_response(wrong);

    const auto result = fsm.feed(ev::d20::Event::V2GTP_MESSAGE);

    REQUIRE(result.transitioned() == false);
    REQUIRE(fsm.get_current_state_id() == ev::d20::StateID::Authorization);
    REQUIRE(ctx.is_session_stopped() == true);
}

SCENARIO("ISO15118-20 EV Authorization stops session on mismatched response session_id") {
    const ev::feedback::Callbacks callbacks{};
    auto state_helper = FsmStateHelper(callbacks);
    auto& ctx = state_helper.get_context();
    ctx.get_session().set_id(SESSION_HEADER.session_id);
    ctx.evse_session_info.auth_services = {message_20::datatypes::Authorization::EIM};

    fsm::v2::FSM<ev::d20::StateBase> fsm{ctx.create_state<ev::d20::state::Authorization>()};

    const auto res = message_20::AuthorizationResponse{WRONG_HEADER, message_20::datatypes::ResponseCode::OK,
                                                       message_20::datatypes::Processing::Finished};
    state_helper.handle_response(res);

    const auto result = fsm.feed(ev::d20::Event::V2GTP_MESSAGE);

    REQUIRE(result.transitioned() == false);
    REQUIRE(fsm.get_current_state_id() == ev::d20::StateID::Authorization);
    REQUIRE(ctx.is_session_stopped() == true);
}
