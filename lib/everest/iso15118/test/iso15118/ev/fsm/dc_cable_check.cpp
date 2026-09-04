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

namespace {
constexpr auto SESSION_HEADER =
    message_20::Header{std::array<uint8_t, 8>{0x10, 0x34, 0xAB, 0x7A, 0x01, 0xF3, 0x95, 0x02}, 1691411798};
} // namespace

SCENARIO("ISO15118-20 EV DC_CableCheck sends initial request on enter") {
    const ev::d20::session::feedback::Callbacks callbacks{};
    auto state_helper = FsmStateHelper(callbacks);
    auto& ctx = state_helper.get_context();
    ctx.get_session().set_id(SESSION_HEADER.session_id);

    fsm::v2::FSM<ev::d20::StateBase> fsm{ctx.create_state<ev::d20::state::DC_CableCheck>()};

    const auto request_message = ctx.get_request<message_20::DC_CableCheckRequest>();
    REQUIRE(request_message.has_value());
    REQUIRE(request_message->header.session_id == SESSION_HEADER.session_id);
}

SCENARIO("ISO15118-20 EV DC_CableCheck stays and resends on Ongoing") {
    const ev::d20::session::feedback::Callbacks callbacks{};
    auto state_helper = FsmStateHelper(callbacks);
    auto& ctx = state_helper.get_context();
    ctx.get_session().set_id(SESSION_HEADER.session_id);

    fsm::v2::FSM<ev::d20::StateBase> fsm{ctx.create_state<ev::d20::state::DC_CableCheck>()};

    // Overwrite the post-enter request with a sentinel of a different type so the post-feed assertion
    // proves a *fresh* DC_CableCheckRequest was emitted by feed(), not just the initial one.
    const auto sentinel = message_20::AuthorizationRequest{message_20::Header{std::array<uint8_t, 8>{}, 0},
                                                           message_20::datatypes::Authorization::EIM,
                                                           message_20::datatypes::EIM_ASReqAuthorizationMode{}};
    ctx.respond(sentinel);
    REQUIRE_FALSE(ctx.get_request<message_20::DC_CableCheckRequest>().has_value());

    const auto res = message_20::DC_CableCheckResponse{SESSION_HEADER, message_20::datatypes::ResponseCode::OK,
                                                       message_20::datatypes::Processing::Ongoing};
    state_helper.handle_response(res);

    const auto result = fsm.feed(ev::d20::Event::V2GTP_MESSAGE);

    REQUIRE(result.transitioned() == false);
    REQUIRE(fsm.get_current_state_id() == ev::d20::StateID::DC_CableCheck);
    REQUIRE(ctx.is_session_stopped() == false);

    const auto request_message = ctx.get_request<message_20::DC_CableCheckRequest>();
    REQUIRE(request_message.has_value());
    REQUIRE(request_message->header.session_id == SESSION_HEADER.session_id);
}

SCENARIO("ISO15118-20 EV DC_CableCheck transitions to DC_PreCharge on Finished") {
    const ev::d20::session::feedback::Callbacks callbacks{};
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
    const ev::d20::session::feedback::Callbacks callbacks{};
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
    const ev::d20::session::feedback::Callbacks callbacks{};
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
    const ev::d20::session::feedback::Callbacks callbacks{};
    auto state_helper = FsmStateHelper(callbacks);
    auto& ctx = state_helper.get_context();
    ctx.get_session().set_id(SESSION_HEADER.session_id);

    fsm::v2::FSM<ev::d20::StateBase> fsm{ctx.create_state<ev::d20::state::DC_CableCheck>()};

    constexpr auto WRONG_HEADER =
        message_20::Header{std::array<uint8_t, 8>{0xDE, 0xAD, 0xBE, 0xEF, 0x00, 0x00, 0x00, 0x00}, 1691411798};
    const auto res = message_20::DC_CableCheckResponse{WRONG_HEADER, message_20::datatypes::ResponseCode::OK,
                                                       message_20::datatypes::Processing::Finished};
    state_helper.handle_response(res);

    const auto result = fsm.feed(ev::d20::Event::V2GTP_MESSAGE);

    REQUIRE(result.transitioned() == false);
    REQUIRE(fsm.get_current_state_id() == ev::d20::StateID::DC_CableCheck);
    REQUIRE(ctx.is_session_stopped() == true);
}

SCENARIO("ISO15118-20 EV DC_CableCheck stops session on FAILED_UnknownSession") {
    const ev::d20::session::feedback::Callbacks callbacks{};
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
