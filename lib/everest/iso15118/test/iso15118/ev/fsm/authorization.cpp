// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#include <catch2/catch_test_macros.hpp>

#include "helper.hpp"

#include <iso15118/ev/d20/state/authorization.hpp>
#include <iso15118/message/authorization.hpp>
#include <iso15118/message/service_discovery.hpp>
#include <iso15118/message/type.hpp>

using namespace iso15118;

namespace {
constexpr auto SESSION_HEADER =
    message_20::Header{std::array<uint8_t, 8>{0x10, 0x34, 0xAB, 0x7A, 0x01, 0xF3, 0x95, 0x02}, 1691411798};
} // namespace

SCENARIO("ISO15118-20 EV Authorization transitions to ServiceDiscovery on Finished") {
    const ev::d20::session::feedback::Callbacks callbacks{};
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

    const auto request_message = ctx.get_request<message_20::ServiceDiscoveryRequest>();
    REQUIRE(request_message.has_value());
    REQUIRE(request_message->header.session_id == SESSION_HEADER.session_id);
}

SCENARIO("ISO15118-20 EV Authorization stays and resends on Ongoing") {
    const ev::d20::session::feedback::Callbacks callbacks{};
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

    const auto request_message = ctx.get_request<message_20::AuthorizationRequest>();
    REQUIRE(request_message.has_value());
    REQUIRE(request_message->header.session_id == SESSION_HEADER.session_id);
    REQUIRE(request_message->selected_authorization_service == message_20::datatypes::Authorization::EIM);
}

SCENARIO("ISO15118-20 EV Authorization stops session on FAILED response") {
    const ev::d20::session::feedback::Callbacks callbacks{};
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
    const ev::d20::session::feedback::Callbacks callbacks{};
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
    const ev::d20::session::feedback::Callbacks callbacks{};
    auto state_helper = FsmStateHelper(callbacks);
    auto& ctx = state_helper.get_context();
    ctx.get_session().set_id(SESSION_HEADER.session_id);
    ctx.evse_session_info.auth_services = {message_20::datatypes::Authorization::EIM};

    fsm::v2::FSM<ev::d20::StateBase> fsm{ctx.create_state<ev::d20::state::Authorization>()};

    constexpr auto WRONG_HEADER =
        message_20::Header{std::array<uint8_t, 8>{0xDE, 0xAD, 0xBE, 0xEF, 0x00, 0x00, 0x00, 0x00}, 1691411798};
    const auto res = message_20::AuthorizationResponse{WRONG_HEADER, message_20::datatypes::ResponseCode::OK,
                                                       message_20::datatypes::Processing::Finished};
    state_helper.handle_response(res);

    const auto result = fsm.feed(ev::d20::Event::V2GTP_MESSAGE);

    REQUIRE(result.transitioned() == false);
    REQUIRE(fsm.get_current_state_id() == ev::d20::StateID::Authorization);
    REQUIRE(ctx.is_session_stopped() == true);
}
