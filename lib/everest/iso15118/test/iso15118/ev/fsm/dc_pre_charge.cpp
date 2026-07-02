// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "helper.hpp"

#include <iso15118/ev/d20/state/dc_pre_charge.hpp>
#include <iso15118/ev/d20/state/power_delivery.hpp>
#include <iso15118/message/authorization.hpp>
#include <iso15118/message/common_types.hpp>
#include <iso15118/message/dc_pre_charge.hpp>
#include <iso15118/message/power_delivery.hpp>
#include <iso15118/message/type.hpp>

using namespace iso15118;

namespace {
constexpr auto SESSION_HEADER =
    message_20::Header{std::array<uint8_t, 8>{0x10, 0x34, 0xAB, 0x7A, 0x01, 0xF3, 0x95, 0x02}, 1691411798};
} // namespace

SCENARIO("ISO15118-20 EV DC_PreCharge sends initial Starting request on enter") {
    const ev::d20::session::feedback::Callbacks callbacks{};
    auto state_helper = FsmStateHelper(callbacks);
    ev::DcChargeParams params{};
    params.target_voltage = 400.0f;
    state_helper.set_dc_params(params);
    auto& ctx = state_helper.get_context();
    ctx.get_session().set_id(SESSION_HEADER.session_id);

    fsm::v2::FSM<ev::d20::StateBase> fsm{ctx.create_state<ev::d20::state::DC_PreCharge>()};

    const auto request_message = ctx.get_request<message_20::DC_PreChargeRequest>();
    REQUIRE(request_message.has_value());
    REQUIRE(request_message->header.session_id == SESSION_HEADER.session_id);
    REQUIRE(request_message->processing == message_20::datatypes::Processing::Ongoing);
    REQUIRE(message_20::datatypes::from_RationalNumber(request_message->target_voltage) == Catch::Approx(400.0f));
}

SCENARIO("ISO15118-20 EV DC_PreCharge fires dc_power_on and transitions to PowerDelivery on in-tolerance response") {
    bool dc_power_on_fired = false;
    ev::d20::session::feedback::Callbacks callbacks{};
    callbacks.dc_power_on = [&dc_power_on_fired]() { dc_power_on_fired = true; };
    auto state_helper = FsmStateHelper(callbacks);
    ev::DcChargeParams params{};
    params.target_voltage = 400.0f;
    state_helper.set_dc_params(params);
    auto& ctx = state_helper.get_context();
    ctx.get_session().set_id(SESSION_HEADER.session_id);

    fsm::v2::FSM<ev::d20::StateBase> fsm{ctx.create_state<ev::d20::state::DC_PreCharge>()};

    // SECC reports present voltage in tolerance of the 400 V target.
    const auto res = message_20::DC_PreChargeResponse{SESSION_HEADER, message_20::datatypes::ResponseCode::OK,
                                                      message_20::datatypes::from_float(400.0f)};
    state_helper.handle_response(res);

    const auto result = fsm.feed(ev::d20::Event::V2GTP_MESSAGE);

    REQUIRE(dc_power_on_fired == true);
    REQUIRE(result.transitioned() == true);
    REQUIRE(fsm.get_current_state_id() == ev::d20::StateID::PowerDelivery);
    REQUIRE(ctx.is_session_stopped() == false);

    // The transition itself signals "finished". feed() emits no new precharge request:
    // the only DC_PreChargeRequest is the Ongoing one from enter() — never a Finished one.
    const auto pre_charge_request = ctx.get_request<message_20::DC_PreChargeRequest>();
    REQUIRE(pre_charge_request.has_value());
    REQUIRE(pre_charge_request->processing == message_20::datatypes::Processing::Ongoing);

    // PowerDelivery::enter() queued a PowerDeliveryRequest(Start).
    const auto pd_request = ctx.get_request<message_20::PowerDeliveryRequest>();
    REQUIRE(pd_request.has_value());
    REQUIRE(pd_request->charge_progress == message_20::datatypes::Progress::Start);
}

SCENARIO("ISO15118-20 EV DC_PreCharge resends Ongoing request when voltage not in tolerance") {
    bool dc_power_on_fired = false;
    ev::d20::session::feedback::Callbacks callbacks{};
    callbacks.dc_power_on = [&dc_power_on_fired]() { dc_power_on_fired = true; };
    auto state_helper = FsmStateHelper(callbacks);
    ev::DcChargeParams params{};
    params.target_voltage = 400.0f;
    state_helper.set_dc_params(params);
    auto& ctx = state_helper.get_context();
    ctx.get_session().set_id(SESSION_HEADER.session_id);

    fsm::v2::FSM<ev::d20::StateBase> fsm{ctx.create_state<ev::d20::state::DC_PreCharge>()};

    // SECC reports present voltage far below the 400 V target — precharge not complete.
    const auto res = message_20::DC_PreChargeResponse{SESSION_HEADER, message_20::datatypes::ResponseCode::OK,
                                                      message_20::datatypes::from_float(100.0f)};
    state_helper.handle_response(res);

    const auto result = fsm.feed(ev::d20::Event::V2GTP_MESSAGE);

    REQUIRE(dc_power_on_fired == false);
    REQUIRE(result.transitioned() == false);
    REQUIRE(fsm.get_current_state_id() == ev::d20::StateID::DC_PreCharge);
    REQUIRE(ctx.is_session_stopped() == false);

    // A single Ongoing DC_PreChargeRequest is re-emitted; no PowerDelivery transition.
    const auto request_message = ctx.get_request<message_20::DC_PreChargeRequest>();
    REQUIRE(request_message.has_value());
    REQUIRE(request_message->processing == message_20::datatypes::Processing::Ongoing);
    REQUIRE_FALSE(ctx.get_request<message_20::PowerDeliveryRequest>().has_value());
}

SCENARIO("ISO15118-20 EV DC_PreCharge stops session on FAILED response") {
    const ev::d20::session::feedback::Callbacks callbacks{};
    auto state_helper = FsmStateHelper(callbacks);
    auto& ctx = state_helper.get_context();
    ctx.get_session().set_id(SESSION_HEADER.session_id);

    fsm::v2::FSM<ev::d20::StateBase> fsm{ctx.create_state<ev::d20::state::DC_PreCharge>()};

    const auto res = message_20::DC_PreChargeResponse{SESSION_HEADER, message_20::datatypes::ResponseCode::FAILED,
                                                      message_20::datatypes::RationalNumber{0, 0}};
    state_helper.handle_response(res);

    const auto result = fsm.feed(ev::d20::Event::V2GTP_MESSAGE);

    REQUIRE(result.transitioned() == false);
    REQUIRE(fsm.get_current_state_id() == ev::d20::StateID::DC_PreCharge);
    REQUIRE(ctx.is_session_stopped() == true);
}

SCENARIO("ISO15118-20 EV DC_PreCharge stops session on wrong variant") {
    const ev::d20::session::feedback::Callbacks callbacks{};
    auto state_helper = FsmStateHelper(callbacks);
    auto& ctx = state_helper.get_context();
    ctx.get_session().set_id(SESSION_HEADER.session_id);

    fsm::v2::FSM<ev::d20::StateBase> fsm{ctx.create_state<ev::d20::state::DC_PreCharge>()};

    const auto wrong = message_20::AuthorizationResponse{SESSION_HEADER, message_20::datatypes::ResponseCode::OK,
                                                         message_20::datatypes::Processing::Finished};
    state_helper.handle_response(wrong);

    const auto result = fsm.feed(ev::d20::Event::V2GTP_MESSAGE);

    REQUIRE(result.transitioned() == false);
    REQUIRE(fsm.get_current_state_id() == ev::d20::StateID::DC_PreCharge);
    REQUIRE(ctx.is_session_stopped() == true);
}

SCENARIO("ISO15118-20 EV DC_PreCharge stops session on mismatched response session_id") {
    const ev::d20::session::feedback::Callbacks callbacks{};
    auto state_helper = FsmStateHelper(callbacks);
    auto& ctx = state_helper.get_context();
    ctx.get_session().set_id(SESSION_HEADER.session_id);

    fsm::v2::FSM<ev::d20::StateBase> fsm{ctx.create_state<ev::d20::state::DC_PreCharge>()};

    constexpr auto WRONG_HEADER =
        message_20::Header{std::array<uint8_t, 8>{0xDE, 0xAD, 0xBE, 0xEF, 0x00, 0x00, 0x00, 0x00}, 1691411798};
    const auto res = message_20::DC_PreChargeResponse{WRONG_HEADER, message_20::datatypes::ResponseCode::OK,
                                                      message_20::datatypes::RationalNumber{0, 0}};
    state_helper.handle_response(res);

    const auto result = fsm.feed(ev::d20::Event::V2GTP_MESSAGE);

    REQUIRE(result.transitioned() == false);
    REQUIRE(fsm.get_current_state_id() == ev::d20::StateID::DC_PreCharge);
    REQUIRE(ctx.is_session_stopped() == true);
}

SCENARIO("ISO15118-20 EV DC_PreCharge stops session on FAILED_UnknownSession") {
    const ev::d20::session::feedback::Callbacks callbacks{};
    auto state_helper = FsmStateHelper(callbacks);
    auto& ctx = state_helper.get_context();
    ctx.get_session().set_id(SESSION_HEADER.session_id);

    fsm::v2::FSM<ev::d20::StateBase> fsm{ctx.create_state<ev::d20::state::DC_PreCharge>()};

    const auto res =
        message_20::DC_PreChargeResponse{SESSION_HEADER, message_20::datatypes::ResponseCode::FAILED_UnknownSession,
                                         message_20::datatypes::RationalNumber{0, 0}};
    state_helper.handle_response(res);

    const auto result = fsm.feed(ev::d20::Event::V2GTP_MESSAGE);

    REQUIRE(result.transitioned() == false);
    REQUIRE(fsm.get_current_state_id() == ev::d20::StateID::DC_PreCharge);
    REQUIRE(ctx.is_session_stopped() == true);
}
