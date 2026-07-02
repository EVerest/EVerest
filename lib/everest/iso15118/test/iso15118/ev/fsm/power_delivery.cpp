// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#include <catch2/catch_test_macros.hpp>

#include "helper.hpp"

#include <iso15118/ev/d20/state/power_delivery.hpp>
#include <iso15118/message/authorization.hpp>
#include <iso15118/message/power_delivery.hpp>
#include <iso15118/message/type.hpp>

using namespace iso15118;

namespace {
ev::d20::Context& prime_context(FsmStateHelper& helper) {
    auto& ctx = helper.get_context();
    ctx.get_session().set_id(SESSION_HEADER.session_id);
    return ctx;
}
} // namespace

SCENARIO("ISO15118-20 EV PowerDelivery sends Finished + chosen progress on enter") {
    const ev::feedback::Callbacks callbacks{};
    auto state_helper = FsmStateHelper(callbacks);
    auto& ctx = prime_context(state_helper);

    fsm::v2::FSM<ev::d20::StateBase> fsm{
        ctx.create_state<ev::d20::state::PowerDelivery>(message_20::datatypes::Progress::Start)};

    const auto requests = drain_requests(state_helper.get_message_exchange());
    const auto request_message = requests.get<message_20::PowerDeliveryRequest>();
    REQUIRE(request_message.has_value());
    REQUIRE(request_message->header.session_id == SESSION_HEADER.session_id);
    REQUIRE(request_message->processing == message_20::datatypes::Processing::Finished);
    REQUIRE(request_message->charge_progress == message_20::datatypes::Progress::Start);
    REQUIRE_FALSE(request_message->power_profile.has_value());
    REQUIRE_FALSE(request_message->channel_selection.has_value());
}

SCENARIO("ISO15118-20 EV PowerDelivery transitions to DC_ChargeLoop on OK response") {
    const ev::feedback::Callbacks callbacks{};
    auto state_helper = FsmStateHelper(callbacks);
    auto& ctx = prime_context(state_helper);

    fsm::v2::FSM<ev::d20::StateBase> fsm{
        ctx.create_state<ev::d20::state::PowerDelivery>(message_20::datatypes::Progress::Start)};

    const auto res =
        message_20::PowerDeliveryResponse{SESSION_HEADER, message_20::datatypes::ResponseCode::OK, std::nullopt};
    state_helper.handle_response(res);

    const auto result = fsm.feed(ev::d20::Event::V2GTP_MESSAGE);

    REQUIRE(result.transitioned() == true);
    REQUIRE(fsm.get_current_state_id() == ev::d20::StateID::DC_ChargeLoop);
    REQUIRE(ctx.is_session_stopped() == false);
}

SCENARIO("ISO15118-20 EV PowerDelivery transitions to DC_WeldingDetection on Stop") {
    const ev::feedback::Callbacks callbacks{};
    auto state_helper = FsmStateHelper(callbacks);
    auto& ctx = prime_context(state_helper);

    fsm::v2::FSM<ev::d20::StateBase> fsm{
        ctx.create_state<ev::d20::state::PowerDelivery>(message_20::datatypes::Progress::Stop)};

    const auto res =
        message_20::PowerDeliveryResponse{SESSION_HEADER, message_20::datatypes::ResponseCode::OK, std::nullopt};
    state_helper.handle_response(res);

    const auto result = fsm.feed(ev::d20::Event::V2GTP_MESSAGE);

    REQUIRE(result.transitioned() == true);
    REQUIRE(fsm.get_current_state_id() == ev::d20::StateID::DC_WeldingDetection);
    REQUIRE(ctx.is_session_stopped() == false);
}

SCENARIO("ISO15118-20 EV PowerDelivery stops session on FAILED response") {
    const ev::feedback::Callbacks callbacks{};
    auto state_helper = FsmStateHelper(callbacks);
    auto& ctx = prime_context(state_helper);

    fsm::v2::FSM<ev::d20::StateBase> fsm{
        ctx.create_state<ev::d20::state::PowerDelivery>(message_20::datatypes::Progress::Start)};

    const auto res =
        message_20::PowerDeliveryResponse{SESSION_HEADER, message_20::datatypes::ResponseCode::FAILED, std::nullopt};
    state_helper.handle_response(res);

    const auto result = fsm.feed(ev::d20::Event::V2GTP_MESSAGE);

    REQUIRE(result.transitioned() == false);
    REQUIRE(fsm.get_current_state_id() == ev::d20::StateID::PowerDelivery);
    REQUIRE(ctx.is_session_stopped() == true);
}

SCENARIO("ISO15118-20 EV PowerDelivery stops session on mismatched response session_id") {
    const ev::feedback::Callbacks callbacks{};
    auto state_helper = FsmStateHelper(callbacks);
    auto& ctx = prime_context(state_helper);

    fsm::v2::FSM<ev::d20::StateBase> fsm{
        ctx.create_state<ev::d20::state::PowerDelivery>(message_20::datatypes::Progress::Start)};

    const auto res =
        message_20::PowerDeliveryResponse{WRONG_HEADER, message_20::datatypes::ResponseCode::OK, std::nullopt};
    state_helper.handle_response(res);

    const auto result = fsm.feed(ev::d20::Event::V2GTP_MESSAGE);

    REQUIRE(result.transitioned() == false);
    REQUIRE(fsm.get_current_state_id() == ev::d20::StateID::PowerDelivery);
    REQUIRE(ctx.is_session_stopped() == true);
}

SCENARIO("ISO15118-20 EV PowerDelivery stops session on wrong variant") {
    const ev::feedback::Callbacks callbacks{};
    auto state_helper = FsmStateHelper(callbacks);
    auto& ctx = prime_context(state_helper);

    fsm::v2::FSM<ev::d20::StateBase> fsm{
        ctx.create_state<ev::d20::state::PowerDelivery>(message_20::datatypes::Progress::Start)};

    const auto wrong = message_20::AuthorizationResponse{SESSION_HEADER, message_20::datatypes::ResponseCode::OK,
                                                         message_20::datatypes::Processing::Finished};
    state_helper.handle_response(wrong);

    const auto result = fsm.feed(ev::d20::Event::V2GTP_MESSAGE);

    REQUIRE(result.transitioned() == false);
    REQUIRE(fsm.get_current_state_id() == ev::d20::StateID::PowerDelivery);
    REQUIRE(ctx.is_session_stopped() == true);
}

SCENARIO("ISO15118-20 EV PowerDelivery accepts OK_PowerToleranceConfirmed") {
    const ev::feedback::Callbacks callbacks{};
    auto state_helper = FsmStateHelper(callbacks);
    auto& ctx = prime_context(state_helper);

    fsm::v2::FSM<ev::d20::StateBase> fsm{
        ctx.create_state<ev::d20::state::PowerDelivery>(message_20::datatypes::Progress::Start)};

    const auto res = message_20::PowerDeliveryResponse{
        SESSION_HEADER, message_20::datatypes::ResponseCode::OK_PowerToleranceConfirmed, std::nullopt};
    state_helper.handle_response(res);

    const auto result = fsm.feed(ev::d20::Event::V2GTP_MESSAGE);

    REQUIRE(result.transitioned() == true);
    REQUIRE(fsm.get_current_state_id() == ev::d20::StateID::DC_ChargeLoop);
    REQUIRE(ctx.is_session_stopped() == false);
}

SCENARIO("ISO15118-20 EV PowerDelivery accepts WARNING_StandbyNotAllowed") {
    const ev::feedback::Callbacks callbacks{};
    auto state_helper = FsmStateHelper(callbacks);
    auto& ctx = prime_context(state_helper);

    fsm::v2::FSM<ev::d20::StateBase> fsm{
        ctx.create_state<ev::d20::state::PowerDelivery>(message_20::datatypes::Progress::Standby)};

    const auto res = message_20::PowerDeliveryResponse{
        SESSION_HEADER, message_20::datatypes::ResponseCode::WARNING_StandbyNotAllowed, std::nullopt};
    state_helper.handle_response(res);

    const auto result = fsm.feed(ev::d20::Event::V2GTP_MESSAGE);

    REQUIRE(result.transitioned() == false);
    REQUIRE(fsm.get_current_state_id() == ev::d20::StateID::PowerDelivery);
    REQUIRE(ctx.is_session_stopped() == false);
}

SCENARIO("ISO15118-20 EV PowerDelivery stops session on FAILED_ContactorError") {
    const ev::feedback::Callbacks callbacks{};
    auto state_helper = FsmStateHelper(callbacks);
    auto& ctx = prime_context(state_helper);

    fsm::v2::FSM<ev::d20::StateBase> fsm{
        ctx.create_state<ev::d20::state::PowerDelivery>(message_20::datatypes::Progress::Start)};

    const auto res = message_20::PowerDeliveryResponse{
        SESSION_HEADER, message_20::datatypes::ResponseCode::FAILED_ContactorError, std::nullopt};
    state_helper.handle_response(res);

    const auto result = fsm.feed(ev::d20::Event::V2GTP_MESSAGE);

    REQUIRE(result.transitioned() == false);
    REQUIRE(fsm.get_current_state_id() == ev::d20::StateID::PowerDelivery);
    REQUIRE(ctx.is_session_stopped() == true);
}
