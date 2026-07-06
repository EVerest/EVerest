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
using message_20::datatypes::ResponseCode;

message_20::DC_ChargeLoopResponse make_res(const message_20::Header& header, ResponseCode code,
                                           std::optional<message_20::datatypes::EvseStatus> status = std::nullopt) {
    message_20::DC_ChargeLoopResponse res;
    res.header = header;
    res.response_code = code;
    res.status = status;
    res.present_voltage = message_20::datatypes::from_float(400.0f);
    res.present_current = message_20::datatypes::from_float(10.0f);
    res.control_mode = message_20::datatypes::Dynamic_DC_CLResControlMode{};
    return res;
}

message_20::DC_ChargeLoopResponse make_bpt_res(const message_20::Header& header, ResponseCode code,
                                               std::optional<message_20::datatypes::EvseStatus> status = std::nullopt) {
    message_20::DC_ChargeLoopResponse res;
    res.header = header;
    res.response_code = code;
    res.status = status;
    res.present_voltage = message_20::datatypes::from_float(400.0f);
    res.present_current = message_20::datatypes::from_float(10.0f);
    res.control_mode = message_20::datatypes::BPT_Dynamic_DC_CLResControlMode{};
    return res;
}

// DC_ChargeLoop reads present_voltage from the DC params for each loop request.
const auto seed_present_400 = [](FsmStateHelper& helper) {
    ev::DcChargeParams params{};
    params.present_voltage = 400.0f;
    helper.set_dc_params(params);
};

// A BPT session seeded with discharge limits alongside the charge params.
const auto seed_bpt_present_400 = [](FsmStateHelper& helper) {
    helper.get_context().set_selected_service(message_20::datatypes::ServiceCategory::DC_BPT);
    ev::DcChargeParams params{};
    params.max_charge_power = 11000.0f;
    params.max_charge_current = 200.0f;
    params.max_voltage = 500.0f;
    params.min_voltage = 200.0f;
    params.max_discharge_power = 9000.0f;
    params.min_discharge_power = 500.0f;
    params.max_discharge_current = 180.0f;
    params.present_voltage = 400.0f;
    helper.set_dc_params(params);
};

// A stop_from_charger observer wired into the callbacks.
struct StopObserver {
    bool fired = false;
    ev::feedback::Callbacks callbacks{};
    StopObserver() {
        callbacks.stop_from_charger = [this]() { fired = true; };
    }
};
} // namespace

SCENARIO("ISO15118-20 EV DC_ChargeLoop emits a Dynamic DC_ChargeLoopRequest on enter") {
    const ev::feedback::Callbacks callbacks{};
    const auto seed_full = [](FsmStateHelper& helper) {
        ev::DcChargeParams params{};
        params.max_charge_power = 11000.0f;
        params.max_charge_current = 32.0f;
        params.max_voltage = 500.0f;
        params.min_voltage = 200.0f;
        params.present_voltage = 400.0f;
        helper.set_dc_params(params);
    };
    PrimedState<ev::d20::state::DC_ChargeLoop> primed{callbacks, seed_full};

    const auto requests = primed.take_requests();
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
    StopObserver obs;
    PrimedState<ev::d20::state::DC_ChargeLoop> primed{obs.callbacks, seed_present_400};

    primed.handle_response(make_res(SESSION_HEADER, ResponseCode::OK));
    const auto result = primed.feed(ev::d20::Event::V2GTP_MESSAGE);

    REQUIRE(result.transitioned() == false);
    REQUIRE(primed.fsm.get_current_state_id() == ev::d20::StateID::DC_ChargeLoop);
    REQUIRE(primed.ctx.is_session_stopped() == false);
    REQUIRE(obs.fired == false);

    // A fresh loop request is queued so the session never consumes without producing.
    const auto requests = primed.take_requests();
    const auto request_message = requests.get<message_20::DC_ChargeLoopRequest>();
    REQUIRE(request_message.has_value());
    REQUIRE_FALSE(requests.get<message_20::PowerDeliveryRequest>().has_value());
}

SCENARIO("ISO15118-20 EV DC_ChargeLoop continues on OK response with no EvseStatus") {
    StopObserver obs;
    PrimedState<ev::d20::state::DC_ChargeLoop> primed{obs.callbacks, seed_present_400};

    // Take the enter() request so the post-feed request proves a fresh loop iteration.
    REQUIRE(primed.helper.get_message_exchange().take_request().has_value());
    REQUIRE_FALSE(primed.helper.get_message_exchange().has_request());

    auto res = make_res(SESSION_HEADER, ResponseCode::OK);
    res.status = std::nullopt;
    primed.handle_response(res);
    const auto result = primed.feed(ev::d20::Event::V2GTP_MESSAGE);

    REQUIRE(obs.fired == false);
    REQUIRE(result.transitioned() == false);
    REQUIRE(primed.fsm.get_current_state_id() == ev::d20::StateID::DC_ChargeLoop);
    REQUIRE(primed.ctx.is_session_stopped() == false);

    // A fresh loop request was emitted by feed().
    const auto requests = primed.take_requests();
    const auto request_message = requests.get<message_20::DC_ChargeLoopRequest>();
    REQUIRE(request_message.has_value());
    REQUIRE_FALSE(requests.get<message_20::PowerDeliveryRequest>().has_value());
}

SCENARIO("ISO15118-20 EV DC_ChargeLoop continues on OK response with a non-Terminate notification") {
    StopObserver obs;
    PrimedState<ev::d20::state::DC_ChargeLoop> primed{obs.callbacks, seed_present_400};

    // Take the enter() request so the post-feed request proves a fresh loop iteration.
    REQUIRE(primed.helper.get_message_exchange().take_request().has_value());
    REQUIRE_FALSE(primed.helper.get_message_exchange().has_request());

    // A present status whose notification is NOT Terminate must keep the loop running.
    primed.handle_response(
        make_res(SESSION_HEADER, ResponseCode::OK,
                 message_20::datatypes::EvseStatus{0, message_20::datatypes::EvseNotification::Pause}));
    const auto result = primed.feed(ev::d20::Event::V2GTP_MESSAGE);

    REQUIRE(obs.fired == false);
    REQUIRE(result.transitioned() == false);
    REQUIRE(primed.fsm.get_current_state_id() == ev::d20::StateID::DC_ChargeLoop);
    REQUIRE(primed.ctx.is_session_stopped() == false);

    // A fresh loop request was emitted by feed(); no PowerDelivery transition.
    const auto requests = primed.take_requests();
    const auto request_message = requests.get<message_20::DC_ChargeLoopRequest>();
    REQUIRE(request_message.has_value());
    REQUIRE_FALSE(requests.get<message_20::PowerDeliveryRequest>().has_value());
}

SCENARIO("ISO15118-20 EV DC_ChargeLoop fires stop_from_charger and drives PowerDelivery(Stop) on Terminate") {
    StopObserver obs;
    PrimedState<ev::d20::state::DC_ChargeLoop> primed{obs.callbacks, seed_present_400};

    primed.handle_response(
        make_res(SESSION_HEADER, ResponseCode::OK,
                 message_20::datatypes::EvseStatus{0, message_20::datatypes::EvseNotification::Terminate}));
    const auto result = primed.feed(ev::d20::Event::V2GTP_MESSAGE);

    REQUIRE(obs.fired == true);
    REQUIRE(result.transitioned() == true);
    REQUIRE(primed.fsm.get_current_state_id() == ev::d20::StateID::PowerDelivery);
    REQUIRE(primed.ctx.is_session_stopped() == false);

    // PowerDelivery::enter() queued a PowerDeliveryRequest(Stop); no new loop request.
    const auto requests = primed.take_requests();
    const auto pd_request = requests.get<message_20::PowerDeliveryRequest>();
    REQUIRE(pd_request.has_value());
    REQUIRE(pd_request->charge_progress == message_20::datatypes::Progress::Stop);
}

SCENARIO("ISO15118-20 EV DC_ChargeLoop defers an EV-initiated stop to the next response boundary") {
    StopObserver obs;
    PrimedState<ev::d20::state::DC_ChargeLoop> primed{obs.callbacks, seed_present_400};

    // Session::deliver_control_event records an EV-initiated stop on the Context.
    // The stop request does not transition immediately: a loop request is still outstanding, and a
    // CONTROL_MESSAGE is a no-op inside the state.
    primed.ctx.set_stop_charging_requested(true);
    const auto control_result = primed.feed(ev::d20::Event::CONTROL_MESSAGE);

    REQUIRE(control_result.transitioned() == false);
    REQUIRE(primed.fsm.get_current_state_id() == ev::d20::StateID::DC_ChargeLoop);
    const auto pre_stop_requests = primed.take_requests();
    REQUIRE_FALSE(pre_stop_requests.get<message_20::PowerDeliveryRequest>().has_value());

    // The next response boundary drives PowerDelivery(Stop) without a Terminate notification.
    primed.handle_response(make_res(SESSION_HEADER, ResponseCode::OK));
    const auto result = primed.feed(ev::d20::Event::V2GTP_MESSAGE);

    REQUIRE(result.transitioned() == true);
    REQUIRE(primed.fsm.get_current_state_id() == ev::d20::StateID::PowerDelivery);
    REQUIRE(obs.fired == false);

    const auto requests = primed.take_requests();
    const auto pd_request = requests.get<message_20::PowerDeliveryRequest>();
    REQUIRE(pd_request.has_value());
    REQUIRE(pd_request->charge_progress == message_20::datatypes::Progress::Stop);
}

SCENARIO("ISO15118-20 EV DC_ChargeLoop honors a stop request set before the state was entered") {
    // The Context stop request can be set (via Session::deliver_control_event) while the
    // FSM is in an earlier state; DC_ChargeLoop must honor it on the next response even
    // though it never saw the CONTROL_MESSAGE itself.
    StopObserver obs;
    const auto seed_stop_request = [](FsmStateHelper& helper) {
        ev::DcChargeParams params{};
        params.present_voltage = 400.0f;
        helper.set_dc_params(params);
        // Record the stop BEFORE DC_ChargeLoop is entered.
        helper.get_context().set_stop_charging_requested(true);
    };
    PrimedState<ev::d20::state::DC_ChargeLoop> primed{obs.callbacks, seed_stop_request};

    primed.handle_response(make_res(SESSION_HEADER, ResponseCode::OK));
    const auto result = primed.feed(ev::d20::Event::V2GTP_MESSAGE);

    REQUIRE(result.transitioned() == true);
    REQUIRE(primed.fsm.get_current_state_id() == ev::d20::StateID::PowerDelivery);
    REQUIRE(obs.fired == false);

    const auto requests = primed.take_requests();
    const auto pd_request = requests.get<message_20::PowerDeliveryRequest>();
    REQUIRE(pd_request.has_value());
    REQUIRE(pd_request->charge_progress == message_20::datatypes::Progress::Stop);
}

SCENARIO("ISO15118-20 EV DC_ChargeLoop emits a BPT_Dynamic request with discharge limits for a BPT session") {
    const ev::feedback::Callbacks callbacks{};
    PrimedState<ev::d20::state::DC_ChargeLoop> primed{callbacks, seed_bpt_present_400};

    const auto requests = primed.take_requests();
    const auto request_message = requests.get<message_20::DC_ChargeLoopRequest>();
    REQUIRE(request_message.has_value());
    REQUIRE(
        std::holds_alternative<message_20::datatypes::BPT_Dynamic_DC_CLReqControlMode>(request_message->control_mode));
    const auto& mode = std::get<message_20::datatypes::BPT_Dynamic_DC_CLReqControlMode>(request_message->control_mode);
    // Charge-side fields carried over unchanged.
    REQUIRE(message_20::datatypes::from_RationalNumber(mode.max_charge_power) == Catch::Approx(11000.0f));
    REQUIRE(message_20::datatypes::from_RationalNumber(mode.max_charge_current) == Catch::Approx(200.0f));
    REQUIRE(message_20::datatypes::from_RationalNumber(mode.max_voltage) == Catch::Approx(500.0f));
    REQUIRE(message_20::datatypes::from_RationalNumber(mode.min_voltage) == Catch::Approx(200.0f));
    // Discharge fields from the DC params.
    REQUIRE(message_20::datatypes::from_RationalNumber(mode.max_discharge_power) == Catch::Approx(9000.0f));
    REQUIRE(message_20::datatypes::from_RationalNumber(mode.min_discharge_power) == Catch::Approx(500.0f));
    REQUIRE(message_20::datatypes::from_RationalNumber(mode.max_discharge_current) == Catch::Approx(180.0f));
    // v2x energy request fields are deliberately omitted.
    REQUIRE_FALSE(mode.max_v2x_energy_request.has_value());
    REQUIRE_FALSE(mode.min_v2x_energy_request.has_value());
}

SCENARIO("ISO15118-20 EV DC_ChargeLoop continues on a BPT_Dynamic response for a BPT session") {
    StopObserver obs;
    PrimedState<ev::d20::state::DC_ChargeLoop> primed{obs.callbacks, seed_bpt_present_400};

    primed.handle_response(make_bpt_res(SESSION_HEADER, ResponseCode::OK));
    const auto result = primed.feed(ev::d20::Event::V2GTP_MESSAGE);

    REQUIRE(result.transitioned() == false);
    REQUIRE(primed.fsm.get_current_state_id() == ev::d20::StateID::DC_ChargeLoop);
    REQUIRE(primed.ctx.is_session_stopped() == false);
    REQUIRE(obs.fired == false);

    const auto requests = primed.take_requests();
    REQUIRE(requests.get<message_20::DC_ChargeLoopRequest>().has_value());
}

SCENARIO("ISO15118-20 EV DC_ChargeLoop stops a BPT session on a plain Dynamic reply it never requested") {
    StopObserver obs;
    PrimedState<ev::d20::state::DC_ChargeLoop> primed{obs.callbacks, seed_bpt_present_400};

    primed.handle_response(make_res(SESSION_HEADER, ResponseCode::OK));
    const auto result = primed.feed(ev::d20::Event::V2GTP_MESSAGE);

    REQUIRE(result.transitioned() == false);
    REQUIRE(primed.fsm.get_current_state_id() == ev::d20::StateID::DC_ChargeLoop);
    REQUIRE(primed.ctx.is_session_stopped() == true);
    REQUIRE(obs.fired == false);
}

SCENARIO("ISO15118-20 EV DC_ChargeLoop stops a plain DC session on a BPT_Dynamic reply it never requested") {
    StopObserver obs;
    PrimedState<ev::d20::state::DC_ChargeLoop> primed{obs.callbacks, seed_present_400};

    primed.handle_response(make_bpt_res(SESSION_HEADER, ResponseCode::OK));
    const auto result = primed.feed(ev::d20::Event::V2GTP_MESSAGE);

    REQUIRE(result.transitioned() == false);
    REQUIRE(primed.fsm.get_current_state_id() == ev::d20::StateID::DC_ChargeLoop);
    REQUIRE(primed.ctx.is_session_stopped() == true);
    REQUIRE(obs.fired == false);
}

SCENARIO("ISO15118-20 EV DC_ChargeLoop fires stop_from_charger and drives PowerDelivery(Stop) on Terminate for BPT") {
    StopObserver obs;
    PrimedState<ev::d20::state::DC_ChargeLoop> primed{obs.callbacks, seed_bpt_present_400};

    primed.handle_response(
        make_bpt_res(SESSION_HEADER, ResponseCode::OK,
                     message_20::datatypes::EvseStatus{0, message_20::datatypes::EvseNotification::Terminate}));
    const auto result = primed.feed(ev::d20::Event::V2GTP_MESSAGE);

    REQUIRE(obs.fired == true);
    REQUIRE(result.transitioned() == true);
    REQUIRE(primed.fsm.get_current_state_id() == ev::d20::StateID::PowerDelivery);
    REQUIRE(primed.ctx.is_session_stopped() == false);

    const auto requests = primed.take_requests();
    const auto pd_request = requests.get<message_20::PowerDeliveryRequest>();
    REQUIRE(pd_request.has_value());
    REQUIRE(pd_request->charge_progress == message_20::datatypes::Progress::Stop);
}

SCENARIO("ISO15118-20 EV DC_ChargeLoop honors a stop request for a BPT session") {
    StopObserver obs;
    const auto seed_bpt_latched = [](FsmStateHelper& helper) {
        helper.get_context().set_selected_service(message_20::datatypes::ServiceCategory::DC_BPT);
        ev::DcChargeParams params{};
        params.present_voltage = 400.0f;
        params.max_discharge_power = 9000.0f;
        helper.set_dc_params(params);
        helper.get_context().set_stop_charging_requested(true);
    };
    PrimedState<ev::d20::state::DC_ChargeLoop> primed{obs.callbacks, seed_bpt_latched};

    primed.handle_response(make_bpt_res(SESSION_HEADER, ResponseCode::OK));
    const auto result = primed.feed(ev::d20::Event::V2GTP_MESSAGE);

    REQUIRE(result.transitioned() == true);
    REQUIRE(primed.fsm.get_current_state_id() == ev::d20::StateID::PowerDelivery);
    REQUIRE(obs.fired == false);

    const auto requests = primed.take_requests();
    const auto pd_request = requests.get<message_20::PowerDeliveryRequest>();
    REQUIRE(pd_request.has_value());
    REQUIRE(pd_request->charge_progress == message_20::datatypes::Progress::Stop);
}

SCENARIO("ISO15118-20 EV DC_ChargeLoop rejects malformed responses") {
    const ev::feedback::Callbacks callbacks{};
    const auto make_fsm = [](FsmStateHelper& helper) {
        auto& ctx = helper.get_context();
        ctx.get_session().set_id(SESSION_HEADER.session_id);
        return fsm::v2::FSM<ev::d20::StateBase>{ctx.create_state<ev::d20::state::DC_ChargeLoop>()};
    };
    const auto make_ok = [](const message_20::Header& header) { return make_res(header, ResponseCode::OK); };
    const auto wrong = message_20::AuthorizationResponse{SESSION_HEADER, ResponseCode::OK,
                                                         message_20::datatypes::Processing::Finished};
    check_rejection_paths(callbacks, ev::d20::StateID::DC_ChargeLoop, make_fsm, make_ok, wrong);
}
