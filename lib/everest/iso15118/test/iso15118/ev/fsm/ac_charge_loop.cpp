// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "helper.hpp"

#include <iso15118/ev/d20/control_event.hpp>
#include <iso15118/ev/d20/state/ac_charge_loop.hpp>
#include <iso15118/ev/d20/state/power_delivery.hpp>
#include <iso15118/message/ac_charge_loop.hpp>
#include <iso15118/message/authorization.hpp>
#include <iso15118/message/common_types.hpp>
#include <iso15118/message/type.hpp>

using namespace iso15118;

namespace {
using message_20::datatypes::ResponseCode;

message_20::AC_ChargeLoopResponse make_res(const message_20::Header& header, ResponseCode code,
                                           std::optional<message_20::datatypes::EvseStatus> status = std::nullopt) {
    message_20::AC_ChargeLoopResponse res;
    res.header = header;
    res.response_code = code;
    res.status = status;
    message_20::datatypes::Dynamic_AC_CLResControlMode mode{};
    mode.target_active_power = message_20::datatypes::from_float(7000.0f);
    res.control_mode = mode;
    return res;
}

// AC_ChargeLoop builds each loop request from the EV's AC charge params.
const auto seed_present_5000 = [](FsmStateHelper& helper) {
    ev::AcChargeParams p{};
    p.max_charge_power = 11000.0f;
    p.min_charge_power = 1000.0f;
    p.present_active_power = 5000.0f;
    helper.set_ac_params(p);
};

// A BPT session seeded with discharge limits alongside the charge params.
const auto seed_bpt_present_5000 = [](FsmStateHelper& helper) {
    helper.get_context().set_selected_service(message_20::datatypes::ServiceCategory::AC_BPT);
    ev::AcChargeParams p{};
    p.max_charge_power = 11000.0f;
    p.min_charge_power = 1000.0f;
    p.max_discharge_power = 9000.0f;
    p.min_discharge_power = 500.0f;
    p.present_active_power = 5000.0f;
    helper.set_ac_params(p);
};

message_20::AC_ChargeLoopResponse make_bpt_res(const message_20::Header& header, ResponseCode code,
                                               std::optional<message_20::datatypes::EvseStatus> status = std::nullopt) {
    message_20::AC_ChargeLoopResponse res;
    res.header = header;
    res.response_code = code;
    res.status = status;
    message_20::datatypes::BPT_Dynamic_AC_CLResControlMode mode{};
    mode.target_active_power = message_20::datatypes::from_float(7000.0f);
    res.control_mode = mode;
    return res;
}

// A stop_from_charger observer wired into the callbacks. Also records whether
// ac_target_power fired, so stop paths can prove no setpoint was pushed.
struct StopObserver {
    bool fired = false;
    bool ac_target_fired = false;
    ev::feedback::Callbacks callbacks{};
    StopObserver() {
        callbacks.stop_from_charger = [this]() { fired = true; };
        callbacks.ac_target_power = [this](const message_20::datatypes::Dynamic_AC_CLResControlMode&) {
            ac_target_fired = true;
        };
    }
};
} // namespace

SCENARIO("ISO15118-20 EV AC_ChargeLoop emits a Dynamic AC_ChargeLoopRequest on enter") {
    const ev::feedback::Callbacks callbacks{};
    const auto seed_single = [](FsmStateHelper& helper) {
        ev::AcChargeParams p{};
        p.max_charge_power = 11000.0f;
        p.min_charge_power = 1000.0f;
        p.present_active_power = 5000.0f;
        p.three_phase = false;
        helper.set_ac_params(p);
    };
    PrimedState<ev::d20::state::AC_ChargeLoop> primed{callbacks, seed_single};

    const auto requests = primed.take_requests();
    const auto request_message = requests.get<message_20::AC_ChargeLoopRequest>();
    REQUIRE(request_message.has_value());
    REQUIRE(request_message->header.session_id == SESSION_HEADER.session_id);
    REQUIRE(request_message->meter_info_requested == false);
    REQUIRE_FALSE(request_message->display_parameters.has_value());
    REQUIRE(std::holds_alternative<message_20::datatypes::Dynamic_AC_CLReqControlMode>(request_message->control_mode));
    const auto& mode = std::get<message_20::datatypes::Dynamic_AC_CLReqControlMode>(request_message->control_mode);
    REQUIRE(message_20::datatypes::from_RationalNumber(mode.max_charge_power) == Catch::Approx(11000.0f));
    REQUIRE(message_20::datatypes::from_RationalNumber(mode.min_charge_power) == Catch::Approx(1000.0f));
    REQUIRE(message_20::datatypes::from_RationalNumber(mode.present_active_power) == Catch::Approx(5000.0f));
    // Mandatory base fields the EV does not steer are pinned at zero.
    REQUIRE(message_20::datatypes::from_RationalNumber(mode.target_energy_request) == Catch::Approx(0.0f));
    REQUIRE(message_20::datatypes::from_RationalNumber(mode.max_energy_request) == Catch::Approx(0.0f));
    REQUIRE(message_20::datatypes::from_RationalNumber(mode.min_energy_request) == Catch::Approx(0.0f));
    REQUIRE(message_20::datatypes::from_RationalNumber(mode.present_reactive_power) == Catch::Approx(0.0f));
    // Single-phase: no per-phase fields.
    REQUIRE_FALSE(mode.max_charge_power_L2.has_value());
    REQUIRE_FALSE(mode.max_charge_power_L3.has_value());
    REQUIRE_FALSE(mode.min_charge_power_L2.has_value());
    REQUIRE_FALSE(mode.min_charge_power_L3.has_value());
    REQUIRE_FALSE(mode.present_active_power_L2.has_value());
    REQUIRE_FALSE(mode.present_active_power_L3.has_value());
    REQUIRE_FALSE(mode.present_reactive_power_L2.has_value());
    REQUIRE_FALSE(mode.present_reactive_power_L3.has_value());
}

SCENARIO("ISO15118-20 EV AC_ChargeLoop emits per-phase fields when three_phase") {
    const ev::feedback::Callbacks callbacks{};
    const auto seed_three = [](FsmStateHelper& helper) {
        ev::AcChargeParams p{};
        p.max_charge_power = 11000.0f;
        p.min_charge_power = 1000.0f;
        p.present_active_power = 5000.0f;
        p.three_phase = true;
        helper.set_ac_params(p);
    };
    PrimedState<ev::d20::state::AC_ChargeLoop> primed{callbacks, seed_three};

    const auto requests = primed.take_requests();
    const auto request_message = requests.get<message_20::AC_ChargeLoopRequest>();
    REQUIRE(request_message.has_value());
    const auto& mode = std::get<message_20::datatypes::Dynamic_AC_CLReqControlMode>(request_message->control_mode);
    REQUIRE(mode.max_charge_power_L2.has_value());
    REQUIRE(mode.max_charge_power_L3.has_value());
    REQUIRE(mode.min_charge_power_L2.has_value());
    REQUIRE(mode.min_charge_power_L3.has_value());
    REQUIRE(mode.present_active_power_L2.has_value());
    REQUIRE(mode.present_active_power_L3.has_value());
    REQUIRE(message_20::datatypes::from_RationalNumber(*mode.max_charge_power_L2) == Catch::Approx(11000.0f));
    REQUIRE(message_20::datatypes::from_RationalNumber(*mode.min_charge_power_L3) == Catch::Approx(1000.0f));
    REQUIRE(message_20::datatypes::from_RationalNumber(*mode.present_active_power_L2) == Catch::Approx(5000.0f));
    // Per-phase reactive power is never advertised, even three-phase.
    REQUIRE_FALSE(mode.present_reactive_power_L2.has_value());
    REQUIRE_FALSE(mode.present_reactive_power_L3.has_value());
}

SCENARIO("ISO15118-20 EV AC_ChargeLoop fires ac_target_power on a Dynamic response") {
    float reported = 0.0f;
    bool fired = false;
    ev::feedback::Callbacks callbacks{};
    callbacks.ac_target_power = [&](const message_20::datatypes::Dynamic_AC_CLResControlMode& mode) {
        fired = true;
        reported = message_20::datatypes::from_RationalNumber(mode.target_active_power);
    };
    PrimedState<ev::d20::state::AC_ChargeLoop> primed{callbacks, seed_present_5000};

    primed.handle_response(make_res(SESSION_HEADER, ResponseCode::OK));
    const auto result = primed.feed(ev::d20::Event::V2GTP_MESSAGE);

    REQUIRE(fired == true);
    REQUIRE(reported == Catch::Approx(7000.0f));
    REQUIRE(result.transitioned() == false);
    REQUIRE(primed.fsm.get_current_state_id() == ev::d20::StateID::AC_ChargeLoop);
}

SCENARIO("ISO15118-20 EV AC_ChargeLoop stays and re-emits a request on a non-Terminate response") {
    StopObserver obs;
    PrimedState<ev::d20::state::AC_ChargeLoop> primed{obs.callbacks, seed_present_5000};

    // Take the enter() request so the post-feed request proves a fresh loop iteration.
    REQUIRE(primed.helper.get_message_exchange().take_request().has_value());
    REQUIRE_FALSE(primed.helper.get_message_exchange().has_request());

    primed.handle_response(make_res(SESSION_HEADER, ResponseCode::OK));
    const auto result = primed.feed(ev::d20::Event::V2GTP_MESSAGE);

    REQUIRE(result.transitioned() == false);
    REQUIRE(primed.fsm.get_current_state_id() == ev::d20::StateID::AC_ChargeLoop);
    REQUIRE(primed.ctx.is_session_stopped() == false);
    REQUIRE(obs.fired == false);

    const auto requests = primed.take_requests();
    const auto request_message = requests.get<message_20::AC_ChargeLoopRequest>();
    REQUIRE(request_message.has_value());
    REQUIRE_FALSE(requests.get<message_20::PowerDeliveryRequest>().has_value());
}

SCENARIO("ISO15118-20 EV AC_ChargeLoop fires stop_from_charger and drives PowerDelivery(Stop) on Terminate") {
    StopObserver obs;
    PrimedState<ev::d20::state::AC_ChargeLoop> primed{obs.callbacks, seed_present_5000};

    primed.handle_response(
        make_res(SESSION_HEADER, ResponseCode::OK,
                 message_20::datatypes::EvseStatus{0, message_20::datatypes::EvseNotification::Terminate}));
    const auto result = primed.feed(ev::d20::Event::V2GTP_MESSAGE);

    REQUIRE(obs.fired == true);
    REQUIRE(obs.ac_target_fired == false);
    REQUIRE(result.transitioned() == true);
    REQUIRE(primed.fsm.get_current_state_id() == ev::d20::StateID::PowerDelivery);
    REQUIRE(primed.ctx.is_session_stopped() == false);

    const auto requests = primed.take_requests();
    const auto pd_request = requests.get<message_20::PowerDeliveryRequest>();
    REQUIRE(pd_request.has_value());
    REQUIRE(pd_request->charge_progress == message_20::datatypes::Progress::Stop);
}

SCENARIO("ISO15118-20 EV AC_ChargeLoop defers an EV-initiated stop to the next response boundary") {
    StopObserver obs;
    PrimedState<ev::d20::state::AC_ChargeLoop> primed{obs.callbacks, seed_present_5000};

    primed.ctx.set_stop_charging_requested(true);
    const auto control_result = primed.feed(ev::d20::Event::CONTROL_MESSAGE);

    REQUIRE(control_result.transitioned() == false);
    REQUIRE(primed.fsm.get_current_state_id() == ev::d20::StateID::AC_ChargeLoop);
    const auto pre_stop_requests = primed.take_requests();
    REQUIRE_FALSE(pre_stop_requests.get<message_20::PowerDeliveryRequest>().has_value());

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

SCENARIO("ISO15118-20 EV AC_ChargeLoop honors a stop request set before the state was entered") {
    StopObserver obs;
    const auto seed_latched_stop = [](FsmStateHelper& helper) {
        ev::AcChargeParams p{};
        p.max_charge_power = 11000.0f;
        p.min_charge_power = 1000.0f;
        p.present_active_power = 5000.0f;
        helper.set_ac_params(p);
        helper.get_context().set_stop_charging_requested(true);
    };
    PrimedState<ev::d20::state::AC_ChargeLoop> primed{obs.callbacks, seed_latched_stop};

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

SCENARIO("ISO15118-20 EV AC_ChargeLoop stops the session on a Scheduled control-mode reply it never requested") {
    bool fired = false;
    ev::feedback::Callbacks callbacks{};
    callbacks.ac_target_power = [&](const message_20::datatypes::Dynamic_AC_CLResControlMode&) { fired = true; };
    PrimedState<ev::d20::state::AC_ChargeLoop> primed{callbacks, seed_present_5000};

    auto res = make_res(SESSION_HEADER, ResponseCode::OK);
    res.control_mode = message_20::datatypes::Scheduled_AC_CLResControlMode{};
    primed.handle_response(res);
    const auto result = primed.feed(ev::d20::Event::V2GTP_MESSAGE);

    REQUIRE(result.transitioned() == false);
    REQUIRE(primed.fsm.get_current_state_id() == ev::d20::StateID::AC_ChargeLoop);
    REQUIRE(primed.ctx.is_session_stopped() == true);
    REQUIRE(fired == false);
}

SCENARIO("ISO15118-20 EV AC_ChargeLoop stops the session on a BPT_Dynamic control-mode reply it never requested") {
    bool fired = false;
    ev::feedback::Callbacks callbacks{};
    callbacks.ac_target_power = [&](const message_20::datatypes::Dynamic_AC_CLResControlMode&) { fired = true; };
    PrimedState<ev::d20::state::AC_ChargeLoop> primed{callbacks, seed_present_5000};

    auto res = make_res(SESSION_HEADER, ResponseCode::OK);
    res.control_mode = message_20::datatypes::BPT_Dynamic_AC_CLResControlMode{};
    primed.handle_response(res);
    const auto result = primed.feed(ev::d20::Event::V2GTP_MESSAGE);

    REQUIRE(result.transitioned() == false);
    REQUIRE(primed.fsm.get_current_state_id() == ev::d20::StateID::AC_ChargeLoop);
    REQUIRE(primed.ctx.is_session_stopped() == true);
    REQUIRE(fired == false);
}

SCENARIO("ISO15118-20 EV AC_ChargeLoop emits a BPT_Dynamic request with discharge limits for a BPT session") {
    const ev::feedback::Callbacks callbacks{};
    const auto seed_single = [](FsmStateHelper& helper) {
        helper.get_context().set_selected_service(message_20::datatypes::ServiceCategory::AC_BPT);
        ev::AcChargeParams p{};
        p.max_charge_power = 11000.0f;
        p.min_charge_power = 1000.0f;
        p.max_discharge_power = 9000.0f;
        p.min_discharge_power = 500.0f;
        p.present_active_power = 5000.0f;
        p.three_phase = false;
        helper.set_ac_params(p);
    };
    PrimedState<ev::d20::state::AC_ChargeLoop> primed{callbacks, seed_single};

    const auto requests = primed.take_requests();
    const auto request_message = requests.get<message_20::AC_ChargeLoopRequest>();
    REQUIRE(request_message.has_value());
    REQUIRE(
        std::holds_alternative<message_20::datatypes::BPT_Dynamic_AC_CLReqControlMode>(request_message->control_mode));
    const auto& mode = std::get<message_20::datatypes::BPT_Dynamic_AC_CLReqControlMode>(request_message->control_mode);
    REQUIRE(message_20::datatypes::from_RationalNumber(mode.max_charge_power) == Catch::Approx(11000.0f));
    REQUIRE(message_20::datatypes::from_RationalNumber(mode.min_charge_power) == Catch::Approx(1000.0f));
    REQUIRE(message_20::datatypes::from_RationalNumber(mode.present_active_power) == Catch::Approx(5000.0f));
    REQUIRE(message_20::datatypes::from_RationalNumber(mode.max_discharge_power) == Catch::Approx(9000.0f));
    REQUIRE(message_20::datatypes::from_RationalNumber(mode.min_discharge_power) == Catch::Approx(500.0f));
    // v2x energy request fields are deliberately omitted.
    REQUIRE_FALSE(mode.max_v2x_energy_request.has_value());
    REQUIRE_FALSE(mode.min_v2x_energy_request.has_value());
    // Single-phase: no per-phase discharge fields.
    REQUIRE_FALSE(mode.max_discharge_power_L2.has_value());
    REQUIRE_FALSE(mode.max_discharge_power_L3.has_value());
    REQUIRE_FALSE(mode.min_discharge_power_L2.has_value());
    REQUIRE_FALSE(mode.min_discharge_power_L3.has_value());
}

SCENARIO("ISO15118-20 EV AC_ChargeLoop mirrors BPT discharge limits to L2/L3 when three_phase") {
    const ev::feedback::Callbacks callbacks{};
    const auto seed_three = [](FsmStateHelper& helper) {
        helper.get_context().set_selected_service(message_20::datatypes::ServiceCategory::AC_BPT);
        ev::AcChargeParams p{};
        p.max_charge_power = 11000.0f;
        p.min_charge_power = 1000.0f;
        p.max_discharge_power = 9000.0f;
        p.min_discharge_power = 500.0f;
        p.present_active_power = 5000.0f;
        p.three_phase = true;
        helper.set_ac_params(p);
    };
    PrimedState<ev::d20::state::AC_ChargeLoop> primed{callbacks, seed_three};

    const auto requests = primed.take_requests();
    const auto request_message = requests.get<message_20::AC_ChargeLoopRequest>();
    REQUIRE(request_message.has_value());
    const auto& mode = std::get<message_20::datatypes::BPT_Dynamic_AC_CLReqControlMode>(request_message->control_mode);
    REQUIRE(mode.max_discharge_power_L2.has_value());
    REQUIRE(mode.max_discharge_power_L3.has_value());
    REQUIRE(mode.min_discharge_power_L2.has_value());
    REQUIRE(mode.min_discharge_power_L3.has_value());
    REQUIRE(message_20::datatypes::from_RationalNumber(*mode.max_discharge_power_L2) == Catch::Approx(9000.0f));
    REQUIRE(message_20::datatypes::from_RationalNumber(*mode.min_discharge_power_L3) == Catch::Approx(500.0f));
}

SCENARIO("ISO15118-20 EV AC_ChargeLoop fires ac_target_power on a BPT_Dynamic response for a BPT session") {
    float reported = 0.0f;
    bool fired = false;
    ev::feedback::Callbacks callbacks{};
    callbacks.ac_target_power = [&](const message_20::datatypes::Dynamic_AC_CLResControlMode& mode) {
        fired = true;
        reported = message_20::datatypes::from_RationalNumber(mode.target_active_power);
    };
    PrimedState<ev::d20::state::AC_ChargeLoop> primed{callbacks, seed_bpt_present_5000};

    primed.handle_response(make_bpt_res(SESSION_HEADER, ResponseCode::OK));
    const auto result = primed.feed(ev::d20::Event::V2GTP_MESSAGE);

    REQUIRE(fired == true);
    REQUIRE(reported == Catch::Approx(7000.0f));
    REQUIRE(result.transitioned() == false);
    REQUIRE(primed.fsm.get_current_state_id() == ev::d20::StateID::AC_ChargeLoop);
}

SCENARIO("ISO15118-20 EV AC_ChargeLoop stops a BPT session on a plain Dynamic reply it never requested") {
    bool fired = false;
    ev::feedback::Callbacks callbacks{};
    callbacks.ac_target_power = [&](const message_20::datatypes::Dynamic_AC_CLResControlMode&) { fired = true; };
    PrimedState<ev::d20::state::AC_ChargeLoop> primed{callbacks, seed_bpt_present_5000};

    primed.handle_response(make_res(SESSION_HEADER, ResponseCode::OK));
    const auto result = primed.feed(ev::d20::Event::V2GTP_MESSAGE);

    REQUIRE(result.transitioned() == false);
    REQUIRE(primed.fsm.get_current_state_id() == ev::d20::StateID::AC_ChargeLoop);
    REQUIRE(primed.ctx.is_session_stopped() == true);
    REQUIRE(fired == false);
}

SCENARIO("ISO15118-20 EV AC_ChargeLoop stops a BPT session on a Scheduled reply it never requested") {
    bool fired = false;
    ev::feedback::Callbacks callbacks{};
    callbacks.ac_target_power = [&](const message_20::datatypes::Dynamic_AC_CLResControlMode&) { fired = true; };
    PrimedState<ev::d20::state::AC_ChargeLoop> primed{callbacks, seed_bpt_present_5000};

    auto res = make_bpt_res(SESSION_HEADER, ResponseCode::OK);
    res.control_mode = message_20::datatypes::Scheduled_AC_CLResControlMode{};
    primed.handle_response(res);
    const auto result = primed.feed(ev::d20::Event::V2GTP_MESSAGE);

    REQUIRE(result.transitioned() == false);
    REQUIRE(primed.fsm.get_current_state_id() == ev::d20::StateID::AC_ChargeLoop);
    REQUIRE(primed.ctx.is_session_stopped() == true);
    REQUIRE(fired == false);
}

SCENARIO("ISO15118-20 EV AC_ChargeLoop fires stop_from_charger and drives PowerDelivery(Stop) on Terminate for BPT") {
    StopObserver obs;
    PrimedState<ev::d20::state::AC_ChargeLoop> primed{obs.callbacks, seed_bpt_present_5000};

    primed.handle_response(
        make_bpt_res(SESSION_HEADER, ResponseCode::OK,
                     message_20::datatypes::EvseStatus{0, message_20::datatypes::EvseNotification::Terminate}));
    const auto result = primed.feed(ev::d20::Event::V2GTP_MESSAGE);

    REQUIRE(obs.fired == true);
    REQUIRE(obs.ac_target_fired == false);
    REQUIRE(result.transitioned() == true);
    REQUIRE(primed.fsm.get_current_state_id() == ev::d20::StateID::PowerDelivery);
    REQUIRE(primed.ctx.is_session_stopped() == false);

    const auto requests = primed.take_requests();
    const auto pd_request = requests.get<message_20::PowerDeliveryRequest>();
    REQUIRE(pd_request.has_value());
    REQUIRE(pd_request->charge_progress == message_20::datatypes::Progress::Stop);
}

SCENARIO("ISO15118-20 EV AC_ChargeLoop honors a stop request set before the state was entered for AC_BPT") {
    StopObserver obs;
    const auto seed_latched_stop = [](FsmStateHelper& helper) {
        helper.get_context().set_selected_service(message_20::datatypes::ServiceCategory::AC_BPT);
        ev::AcChargeParams p{};
        p.max_charge_power = 11000.0f;
        p.min_charge_power = 1000.0f;
        p.max_discharge_power = 9000.0f;
        p.min_discharge_power = 500.0f;
        p.present_active_power = 5000.0f;
        helper.set_ac_params(p);
        helper.get_context().set_stop_charging_requested(true);
    };
    PrimedState<ev::d20::state::AC_ChargeLoop> primed{obs.callbacks, seed_latched_stop};

    primed.handle_response(make_bpt_res(SESSION_HEADER, ResponseCode::OK));
    const auto result = primed.feed(ev::d20::Event::V2GTP_MESSAGE);

    REQUIRE(result.transitioned() == true);
    REQUIRE(primed.fsm.get_current_state_id() == ev::d20::StateID::PowerDelivery);
    REQUIRE(obs.fired == false);
    REQUIRE(obs.ac_target_fired == false);

    const auto requests = primed.take_requests();
    const auto pd_request = requests.get<message_20::PowerDeliveryRequest>();
    REQUIRE(pd_request.has_value());
    REQUIRE(pd_request->charge_progress == message_20::datatypes::Progress::Stop);
}

SCENARIO("ISO15118-20 EV AC_ChargeLoop rejects malformed responses") {
    const ev::feedback::Callbacks callbacks{};
    const auto make_fsm = [](FsmStateHelper& helper) {
        auto& ctx = helper.get_context();
        ctx.get_session().set_id(SESSION_HEADER.session_id);
        return fsm::v2::FSM<ev::d20::StateBase>{ctx.create_state<ev::d20::state::AC_ChargeLoop>()};
    };
    const auto make_ok = [](const message_20::Header& header) { return make_res(header, ResponseCode::OK); };
    const auto wrong = message_20::AuthorizationResponse{SESSION_HEADER, ResponseCode::OK,
                                                         message_20::datatypes::Processing::Finished};
    check_rejection_paths(callbacks, ev::d20::StateID::AC_ChargeLoop, make_fsm, make_ok, wrong);
}
