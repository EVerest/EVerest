// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "helper.hpp"

#include <iso15118/ev/d20/control_event.hpp>
#include <iso15118/ev/d20/state/dc_charge_loop.hpp>
#include <iso15118/ev/d20/state/power_delivery.hpp>
#include <iso15118/message/authorization.hpp>
#include <iso15118/message/common_types.hpp>
#include <iso15118/message/dc_charge_loop.hpp>
#include <iso15118/message/type.hpp>

using namespace iso15118;

namespace {
message_20::DC_ChargeLoopResponse make_res(message_20::datatypes::ResponseCode code,
                                           std::optional<message_20::datatypes::EvseStatus> status = std::nullopt) {
    message_20::DC_ChargeLoopResponse res;
    res.header = SESSION_HEADER;
    res.response_code = code;
    res.status = status;
    res.present_voltage = message_20::datatypes::from_float(400.0f);
    res.present_current = message_20::datatypes::from_float(10.0f);
    res.control_mode = message_20::datatypes::Dynamic_DC_CLResControlMode{};
    return res;
}
} // namespace

SCENARIO("ISO15118-20 EV DC_ChargeLoop emits a Dynamic DC_ChargeLoopRequest on enter") {
    const ev::feedback::Callbacks callbacks{};
    auto state_helper = FsmStateHelper(callbacks);
    ev::DcChargeParams params{};
    params.max_charge_power = 11000.0f;
    params.max_charge_current = 32.0f;
    params.max_voltage = 500.0f;
    params.min_voltage = 200.0f;
    params.present_voltage = 400.0f;
    state_helper.set_dc_params(params);
    auto& ctx = state_helper.get_context();
    ctx.get_session().set_id(SESSION_HEADER.session_id);

    fsm::v2::FSM<ev::d20::StateBase> fsm{ctx.create_state<ev::d20::state::DC_ChargeLoop>()};

    const auto requests = drain_requests(state_helper.get_message_exchange());
    const auto request_message = requests.get<message_20::DC_ChargeLoopRequest>();
    REQUIRE(request_message.has_value());
    REQUIRE(request_message->header.session_id == SESSION_HEADER.session_id);
    REQUIRE(request_message->meter_info_requested == false);
    REQUIRE_FALSE(request_message->display_parameters.has_value());
    REQUIRE(message_20::datatypes::from_RationalNumber(request_message->present_voltage) == Catch::Approx(400.0f));
    REQUIRE(std::holds_alternative<message_20::datatypes::Dynamic_DC_CLReqControlMode>(request_message->control_mode));
    const auto& mode = std::get<message_20::datatypes::Dynamic_DC_CLReqControlMode>(request_message->control_mode);
    REQUIRE(message_20::datatypes::from_RationalNumber(mode.max_charge_power) == Catch::Approx(11000.0f));
    REQUIRE(message_20::datatypes::from_RationalNumber(mode.max_charge_current) == Catch::Approx(32.0f));
    REQUIRE(message_20::datatypes::from_RationalNumber(mode.max_voltage) == Catch::Approx(500.0f));
    REQUIRE(message_20::datatypes::from_RationalNumber(mode.min_voltage) == Catch::Approx(200.0f));
}

SCENARIO("ISO15118-20 EV DC_ChargeLoop stays and re-emits a request on a non-Terminate response") {
    bool stop_from_charger_fired = false;
    ev::feedback::Callbacks callbacks{};
    callbacks.stop_from_charger = [&stop_from_charger_fired]() { stop_from_charger_fired = true; };
    auto state_helper = FsmStateHelper(callbacks);
    ev::DcChargeParams params{};
    params.present_voltage = 400.0f;
    state_helper.set_dc_params(params);
    auto& ctx = state_helper.get_context();
    ctx.get_session().set_id(SESSION_HEADER.session_id);

    fsm::v2::FSM<ev::d20::StateBase> fsm{ctx.create_state<ev::d20::state::DC_ChargeLoop>()};

    state_helper.handle_response(make_res(message_20::datatypes::ResponseCode::OK));

    const auto result = fsm.feed(ev::d20::Event::V2GTP_MESSAGE);

    REQUIRE(result.transitioned() == false);
    REQUIRE(fsm.get_current_state_id() == ev::d20::StateID::DC_ChargeLoop);
    REQUIRE(ctx.is_session_stopped() == false);
    REQUIRE(stop_from_charger_fired == false);

    // A fresh loop request is queued so the session never consumes without producing.
    const auto requests = drain_requests(state_helper.get_message_exchange());
    const auto request_message = requests.get<message_20::DC_ChargeLoopRequest>();
    REQUIRE(request_message.has_value());
    REQUIRE_FALSE(requests.get<message_20::PowerDeliveryRequest>().has_value());
}

SCENARIO("ISO15118-20 EV DC_ChargeLoop continues on OK response with no EvseStatus") {
    bool stop_from_charger_fired = false;
    ev::feedback::Callbacks callbacks{};
    callbacks.stop_from_charger = [&stop_from_charger_fired]() { stop_from_charger_fired = true; };
    auto state_helper = FsmStateHelper(callbacks);
    ev::DcChargeParams params{};
    params.present_voltage = 400.0f;
    state_helper.set_dc_params(params);
    auto& ctx = state_helper.get_context();
    ctx.get_session().set_id(SESSION_HEADER.session_id);

    fsm::v2::FSM<ev::d20::StateBase> fsm{ctx.create_state<ev::d20::state::DC_ChargeLoop>()};

    // Drain the enter() request so the post-feed request proves a fresh loop iteration.
    REQUIRE(state_helper.get_message_exchange().take_request().has_value());
    REQUIRE_FALSE(state_helper.get_message_exchange().has_request());

    auto res = make_res(message_20::datatypes::ResponseCode::OK);
    res.status = std::nullopt;
    state_helper.handle_response(res);

    const auto result = fsm.feed(ev::d20::Event::V2GTP_MESSAGE);

    REQUIRE(stop_from_charger_fired == false);
    REQUIRE(result.transitioned() == false);
    REQUIRE(fsm.get_current_state_id() == ev::d20::StateID::DC_ChargeLoop);
    REQUIRE(ctx.is_session_stopped() == false);

    // A fresh loop request was emitted by feed().
    const auto requests = drain_requests(state_helper.get_message_exchange());
    const auto request_message = requests.get<message_20::DC_ChargeLoopRequest>();
    REQUIRE(request_message.has_value());
    REQUIRE_FALSE(requests.get<message_20::PowerDeliveryRequest>().has_value());
}

SCENARIO("ISO15118-20 EV DC_ChargeLoop continues on OK response with a non-Terminate notification") {
    bool stop_from_charger_fired = false;
    ev::feedback::Callbacks callbacks{};
    callbacks.stop_from_charger = [&stop_from_charger_fired]() { stop_from_charger_fired = true; };
    auto state_helper = FsmStateHelper(callbacks);
    ev::DcChargeParams params{};
    params.present_voltage = 400.0f;
    state_helper.set_dc_params(params);
    auto& ctx = state_helper.get_context();
    ctx.get_session().set_id(SESSION_HEADER.session_id);

    fsm::v2::FSM<ev::d20::StateBase> fsm{ctx.create_state<ev::d20::state::DC_ChargeLoop>()};

    // Drain the enter() request so the post-feed request proves a fresh loop iteration.
    REQUIRE(state_helper.get_message_exchange().take_request().has_value());
    REQUIRE_FALSE(state_helper.get_message_exchange().has_request());

    // A present status whose notification is NOT Terminate must keep the loop running.
    state_helper.handle_response(
        make_res(message_20::datatypes::ResponseCode::OK,
                 message_20::datatypes::EvseStatus{0, message_20::datatypes::EvseNotification::Pause}));

    const auto result = fsm.feed(ev::d20::Event::V2GTP_MESSAGE);

    REQUIRE(stop_from_charger_fired == false);
    REQUIRE(result.transitioned() == false);
    REQUIRE(fsm.get_current_state_id() == ev::d20::StateID::DC_ChargeLoop);
    REQUIRE(ctx.is_session_stopped() == false);

    // A fresh loop request was emitted by feed(); no PowerDelivery transition.
    const auto requests = drain_requests(state_helper.get_message_exchange());
    const auto request_message = requests.get<message_20::DC_ChargeLoopRequest>();
    REQUIRE(request_message.has_value());
    REQUIRE_FALSE(requests.get<message_20::PowerDeliveryRequest>().has_value());
}

SCENARIO("ISO15118-20 EV DC_ChargeLoop fires stop_from_charger and drives PowerDelivery(Stop) on Terminate") {
    bool stop_from_charger_fired = false;
    ev::feedback::Callbacks callbacks{};
    callbacks.stop_from_charger = [&stop_from_charger_fired]() { stop_from_charger_fired = true; };
    auto state_helper = FsmStateHelper(callbacks);
    ev::DcChargeParams params{};
    params.present_voltage = 400.0f;
    state_helper.set_dc_params(params);
    auto& ctx = state_helper.get_context();
    ctx.get_session().set_id(SESSION_HEADER.session_id);

    fsm::v2::FSM<ev::d20::StateBase> fsm{ctx.create_state<ev::d20::state::DC_ChargeLoop>()};

    state_helper.handle_response(
        make_res(message_20::datatypes::ResponseCode::OK,
                 message_20::datatypes::EvseStatus{0, message_20::datatypes::EvseNotification::Terminate}));

    const auto result = fsm.feed(ev::d20::Event::V2GTP_MESSAGE);

    REQUIRE(stop_from_charger_fired == true);
    REQUIRE(result.transitioned() == true);
    REQUIRE(fsm.get_current_state_id() == ev::d20::StateID::PowerDelivery);
    REQUIRE(ctx.is_session_stopped() == false);

    // PowerDelivery::enter() queued a PowerDeliveryRequest(Stop); no new loop request.
    const auto requests = drain_requests(state_helper.get_message_exchange());
    const auto pd_request = requests.get<message_20::PowerDeliveryRequest>();
    REQUIRE(pd_request.has_value());
    REQUIRE(pd_request->charge_progress == message_20::datatypes::Progress::Stop);
}

SCENARIO("ISO15118-20 EV DC_ChargeLoop defers an EV-initiated stop to the next response boundary") {
    bool stop_from_charger_fired = false;
    ev::feedback::Callbacks callbacks{};
    callbacks.stop_from_charger = [&stop_from_charger_fired]() { stop_from_charger_fired = true; };
    auto state_helper = FsmStateHelper(callbacks);
    ev::DcChargeParams params{};
    params.present_voltage = 400.0f;
    state_helper.set_dc_params(params);
    auto& ctx = state_helper.get_context();
    ctx.get_session().set_id(SESSION_HEADER.session_id);

    fsm::v2::FSM<ev::d20::StateBase> fsm{ctx.create_state<ev::d20::state::DC_ChargeLoop>()};

    // A StopCharging control event does not transition immediately (a loop request is outstanding).
    state_helper.set_control_event(ev::d20::StopCharging{true});
    const auto control_result = fsm.feed(ev::d20::Event::CONTROL_MESSAGE);
    state_helper.clear_control_event();

    REQUIRE(control_result.transitioned() == false);
    REQUIRE(fsm.get_current_state_id() == ev::d20::StateID::DC_ChargeLoop);
    const auto pre_stop_requests = drain_requests(state_helper.get_message_exchange());
    REQUIRE_FALSE(pre_stop_requests.get<message_20::PowerDeliveryRequest>().has_value());

    // The next response boundary drives PowerDelivery(Stop) without a Terminate notification.
    state_helper.handle_response(make_res(message_20::datatypes::ResponseCode::OK));
    const auto result = fsm.feed(ev::d20::Event::V2GTP_MESSAGE);

    REQUIRE(result.transitioned() == true);
    REQUIRE(fsm.get_current_state_id() == ev::d20::StateID::PowerDelivery);
    REQUIRE(stop_from_charger_fired == false);

    const auto requests = drain_requests(state_helper.get_message_exchange());
    const auto pd_request = requests.get<message_20::PowerDeliveryRequest>();
    REQUIRE(pd_request.has_value());
    REQUIRE(pd_request->charge_progress == message_20::datatypes::Progress::Stop);
}

SCENARIO("ISO15118-20 EV DC_ChargeLoop honors a stop latch set before the state was entered") {
    // The Context stop latch can be set (via Session::deliver_control_event) while the
    // FSM is in an earlier state; DC_ChargeLoop must honor it on the next response even
    // though it never saw the CONTROL_MESSAGE itself.
    bool stop_from_charger_fired = false;
    ev::feedback::Callbacks callbacks{};
    callbacks.stop_from_charger = [&stop_from_charger_fired]() { stop_from_charger_fired = true; };
    auto state_helper = FsmStateHelper(callbacks);
    ev::DcChargeParams params{};
    params.present_voltage = 400.0f;
    state_helper.set_dc_params(params);
    auto& ctx = state_helper.get_context();
    ctx.get_session().set_id(SESSION_HEADER.session_id);

    // Latch the stop BEFORE DC_ChargeLoop is entered.
    ctx.set_stop_charging_requested(true);

    fsm::v2::FSM<ev::d20::StateBase> fsm{ctx.create_state<ev::d20::state::DC_ChargeLoop>()};

    state_helper.handle_response(make_res(message_20::datatypes::ResponseCode::OK));
    const auto result = fsm.feed(ev::d20::Event::V2GTP_MESSAGE);

    REQUIRE(result.transitioned() == true);
    REQUIRE(fsm.get_current_state_id() == ev::d20::StateID::PowerDelivery);
    REQUIRE(stop_from_charger_fired == false);

    const auto requests = drain_requests(state_helper.get_message_exchange());
    const auto pd_request = requests.get<message_20::PowerDeliveryRequest>();
    REQUIRE(pd_request.has_value());
    REQUIRE(pd_request->charge_progress == message_20::datatypes::Progress::Stop);
}

SCENARIO("ISO15118-20 EV DC_ChargeLoop stops session on FAILED response") {
    const ev::feedback::Callbacks callbacks{};
    auto state_helper = FsmStateHelper(callbacks);
    auto& ctx = state_helper.get_context();
    ctx.get_session().set_id(SESSION_HEADER.session_id);

    fsm::v2::FSM<ev::d20::StateBase> fsm{ctx.create_state<ev::d20::state::DC_ChargeLoop>()};

    state_helper.handle_response(make_res(message_20::datatypes::ResponseCode::FAILED));

    const auto result = fsm.feed(ev::d20::Event::V2GTP_MESSAGE);

    REQUIRE(result.transitioned() == false);
    REQUIRE(fsm.get_current_state_id() == ev::d20::StateID::DC_ChargeLoop);
    REQUIRE(ctx.is_session_stopped() == true);
}

SCENARIO("ISO15118-20 EV DC_ChargeLoop stops session on wrong variant") {
    const ev::feedback::Callbacks callbacks{};
    auto state_helper = FsmStateHelper(callbacks);
    auto& ctx = state_helper.get_context();
    ctx.get_session().set_id(SESSION_HEADER.session_id);

    fsm::v2::FSM<ev::d20::StateBase> fsm{ctx.create_state<ev::d20::state::DC_ChargeLoop>()};

    const auto wrong = message_20::AuthorizationResponse{SESSION_HEADER, message_20::datatypes::ResponseCode::OK,
                                                         message_20::datatypes::Processing::Finished};
    state_helper.handle_response(wrong);

    const auto result = fsm.feed(ev::d20::Event::V2GTP_MESSAGE);

    REQUIRE(result.transitioned() == false);
    REQUIRE(fsm.get_current_state_id() == ev::d20::StateID::DC_ChargeLoop);
    REQUIRE(ctx.is_session_stopped() == true);
}

SCENARIO("ISO15118-20 EV DC_ChargeLoop stops session on mismatched response session_id") {
    const ev::feedback::Callbacks callbacks{};
    auto state_helper = FsmStateHelper(callbacks);
    auto& ctx = state_helper.get_context();
    ctx.get_session().set_id(SESSION_HEADER.session_id);

    fsm::v2::FSM<ev::d20::StateBase> fsm{ctx.create_state<ev::d20::state::DC_ChargeLoop>()};

    auto res = make_res(message_20::datatypes::ResponseCode::OK);
    res.header = WRONG_HEADER;
    state_helper.handle_response(res);

    const auto result = fsm.feed(ev::d20::Event::V2GTP_MESSAGE);

    REQUIRE(result.transitioned() == false);
    REQUIRE(fsm.get_current_state_id() == ev::d20::StateID::DC_ChargeLoop);
    REQUIRE(ctx.is_session_stopped() == true);
}
