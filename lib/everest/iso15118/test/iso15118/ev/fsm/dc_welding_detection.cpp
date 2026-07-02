// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#include <catch2/catch_test_macros.hpp>

#include "helper.hpp"

#include <iso15118/ev/d20/state/dc_welding_detection.hpp>
#include <iso15118/ev/d20/state/session_stop.hpp>
#include <iso15118/message/authorization.hpp>
#include <iso15118/message/dc_welding_detection.hpp>
#include <iso15118/message/session_stop.hpp>
#include <iso15118/message/type.hpp>

using namespace iso15118;

SCENARIO("ISO15118-20 EV DC_WeldingDetection sends Starting request on enter") {
    const ev::feedback::Callbacks callbacks{};
    auto state_helper = FsmStateHelper(callbacks);
    auto& ctx = state_helper.get_context();
    ctx.get_session().set_id(SESSION_HEADER.session_id);

    fsm::v2::FSM<ev::d20::StateBase> fsm{ctx.create_state<ev::d20::state::DC_WeldingDetection>()};

    const auto requests = drain_requests(state_helper.get_message_exchange());
    const auto request_message = requests.get<message_20::DC_WeldingDetectionRequest>();
    REQUIRE(request_message.has_value());
    REQUIRE(request_message->header.session_id == SESSION_HEADER.session_id);
    REQUIRE(request_message->processing == message_20::datatypes::Processing::Ongoing);
}

SCENARIO("ISO15118-20 EV DC_WeldingDetection transitions to SessionStop on OK response") {
    const ev::feedback::Callbacks callbacks{};
    auto state_helper = FsmStateHelper(callbacks);
    auto& ctx = state_helper.get_context();
    ctx.get_session().set_id(SESSION_HEADER.session_id);

    fsm::v2::FSM<ev::d20::StateBase> fsm{ctx.create_state<ev::d20::state::DC_WeldingDetection>()};

    const auto res = message_20::DC_WeldingDetectionResponse{SESSION_HEADER, message_20::datatypes::ResponseCode::OK,
                                                             message_20::datatypes::RationalNumber{0, 0}};
    state_helper.handle_response(res);

    const auto result = fsm.feed(ev::d20::Event::V2GTP_MESSAGE);

    REQUIRE(result.transitioned() == true);
    REQUIRE(fsm.get_current_state_id() == ev::d20::StateID::SessionStop);
    REQUIRE(ctx.is_session_stopped() == false);

    // No second (Finished) welding request is emitted: the only welding request is the
    // Ongoing one from enter(). SessionStop::enter() queued a SessionStopRequest instead.
    const auto requests = drain_requests(state_helper.get_message_exchange());

    const auto welding_request = requests.get<message_20::DC_WeldingDetectionRequest>();
    REQUIRE(welding_request.has_value());
    REQUIRE(welding_request->processing == message_20::datatypes::Processing::Ongoing);

    const auto session_stop_request = requests.get<message_20::SessionStopRequest>();
    REQUIRE(session_stop_request.has_value());
    REQUIRE(session_stop_request->charging_session == message_20::datatypes::ChargingSession::Terminate);
}

SCENARIO("ISO15118-20 EV DC_WeldingDetection stops session on FAILED response") {
    const ev::feedback::Callbacks callbacks{};
    auto state_helper = FsmStateHelper(callbacks);
    auto& ctx = state_helper.get_context();
    ctx.get_session().set_id(SESSION_HEADER.session_id);

    fsm::v2::FSM<ev::d20::StateBase> fsm{ctx.create_state<ev::d20::state::DC_WeldingDetection>()};

    const auto res = message_20::DC_WeldingDetectionResponse{
        SESSION_HEADER, message_20::datatypes::ResponseCode::FAILED, message_20::datatypes::RationalNumber{0, 0}};
    state_helper.handle_response(res);

    const auto result = fsm.feed(ev::d20::Event::V2GTP_MESSAGE);

    REQUIRE(result.transitioned() == false);
    REQUIRE(fsm.get_current_state_id() == ev::d20::StateID::DC_WeldingDetection);
    REQUIRE(ctx.is_session_stopped() == true);
}

SCENARIO("ISO15118-20 EV DC_WeldingDetection stops session on wrong variant") {
    const ev::feedback::Callbacks callbacks{};
    auto state_helper = FsmStateHelper(callbacks);
    auto& ctx = state_helper.get_context();
    ctx.get_session().set_id(SESSION_HEADER.session_id);

    fsm::v2::FSM<ev::d20::StateBase> fsm{ctx.create_state<ev::d20::state::DC_WeldingDetection>()};

    const auto wrong = message_20::AuthorizationResponse{SESSION_HEADER, message_20::datatypes::ResponseCode::OK,
                                                         message_20::datatypes::Processing::Finished};
    state_helper.handle_response(wrong);

    const auto result = fsm.feed(ev::d20::Event::V2GTP_MESSAGE);

    REQUIRE(result.transitioned() == false);
    REQUIRE(fsm.get_current_state_id() == ev::d20::StateID::DC_WeldingDetection);
    REQUIRE(ctx.is_session_stopped() == true);
}

SCENARIO("ISO15118-20 EV DC_WeldingDetection stops session on mismatched response session_id") {
    const ev::feedback::Callbacks callbacks{};
    auto state_helper = FsmStateHelper(callbacks);
    auto& ctx = state_helper.get_context();
    ctx.get_session().set_id(SESSION_HEADER.session_id);

    fsm::v2::FSM<ev::d20::StateBase> fsm{ctx.create_state<ev::d20::state::DC_WeldingDetection>()};

    const auto res = message_20::DC_WeldingDetectionResponse{WRONG_HEADER, message_20::datatypes::ResponseCode::OK,
                                                             message_20::datatypes::RationalNumber{0, 0}};
    state_helper.handle_response(res);

    const auto result = fsm.feed(ev::d20::Event::V2GTP_MESSAGE);

    REQUIRE(result.transitioned() == false);
    REQUIRE(fsm.get_current_state_id() == ev::d20::StateID::DC_WeldingDetection);
    REQUIRE(ctx.is_session_stopped() == true);
}

SCENARIO("ISO15118-20 EV DC_WeldingDetection stops session on FAILED_UnknownSession") {
    const ev::feedback::Callbacks callbacks{};
    auto state_helper = FsmStateHelper(callbacks);
    auto& ctx = state_helper.get_context();
    ctx.get_session().set_id(SESSION_HEADER.session_id);

    fsm::v2::FSM<ev::d20::StateBase> fsm{ctx.create_state<ev::d20::state::DC_WeldingDetection>()};

    const auto res = message_20::DC_WeldingDetectionResponse{SESSION_HEADER,
                                                             message_20::datatypes::ResponseCode::FAILED_UnknownSession,
                                                             message_20::datatypes::RationalNumber{0, 0}};
    state_helper.handle_response(res);

    const auto result = fsm.feed(ev::d20::Event::V2GTP_MESSAGE);

    REQUIRE(result.transitioned() == false);
    REQUIRE(fsm.get_current_state_id() == ev::d20::StateID::DC_WeldingDetection);
    REQUIRE(ctx.is_session_stopped() == true);
}
