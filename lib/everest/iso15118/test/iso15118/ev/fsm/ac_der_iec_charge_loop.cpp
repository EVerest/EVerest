// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "helper.hpp"

#include <iso15118/d20/der_functions.hpp>
#include <iso15118/ev/d20/control_event.hpp>
#include <iso15118/ev/d20/state/ac_der_iec_charge_loop.hpp>
#include <iso15118/ev/d20/state/power_delivery.hpp>
#include <iso15118/ev/der_control_functions.hpp>
#include <iso15118/message/ac_der_iec_charge_loop.hpp>
#include <iso15118/message/authorization.hpp>
#include <iso15118/message/common_types.hpp>
#include <iso15118/message/type.hpp>

using namespace iso15118;

namespace {
using message_20::datatypes::ResponseCode;

message_20::DER_AC_ChargeLoopResponse make_res(const message_20::Header& header, ResponseCode code,
                                               std::optional<message_20::datatypes::EvseStatus> status = std::nullopt) {
    message_20::DER_AC_ChargeLoopResponse res;
    res.header = header;
    res.response_code = code;
    res.status = status;
    message_20::datatypes::DER_Dynamic_AC_CLResControlMode mode{};
    mode.target_active_power = message_20::datatypes::from_float(7000.0f);
    mode.max_charge_power = message_20::datatypes::from_float(11000.0f);
    mode.max_discharge_power = message_20::datatypes::from_float(11000.0f);
    res.control_mode = mode;
    return res;
}

// Discharge limits differ from the charge limits so a request that substitutes one for the
// other fails rather than passing by coincidence.
const auto seed_present_5000 = [](FsmStateHelper& helper) {
    ev::AcChargeParams p{};
    p.phase_count = 1;
    p.max_charge_power = 11000.0f;
    p.min_charge_power = 1000.0f;
    p.max_discharge_power = 9000.0f;
    p.min_discharge_power = 800.0f;
    p.present_active_power = 5000.0f;
    helper.set_ac_params(p);
};

// Observes stop_from_charger and whether der_control fired, so stop paths can prove no
// directive was surfaced.
struct StopObserver {
    bool fired = false;
    bool der_control_fired = false;
    ev::feedback::Callbacks callbacks{};
    StopObserver() {
        callbacks.stop_from_charger = [this]() { fired = true; };
        callbacks.der_control = [this](const message_20::datatypes::DER_Dynamic_AC_CLResControlMode&) {
            der_control_fired = true;
        };
    }
};
} // namespace

SCENARIO("ISO15118-20 EV AC_DER_IEC_ChargeLoop emits a Dynamic DER_AC_ChargeLoopRequest on enter") {
    const ev::feedback::Callbacks callbacks{};
    PrimedState<ev::d20::state::AC_DER_IEC_ChargeLoop> primed{callbacks, seed_present_5000};

    const auto requests = primed.take_requests();
    const auto request_message = requests.get<message_20::DER_AC_ChargeLoopRequest>();
    REQUIRE(request_message.has_value());
    REQUIRE(request_message->header.session_id == SESSION_HEADER.session_id);
    REQUIRE(request_message->meter_info_requested == false);
    REQUIRE_FALSE(request_message->display_parameters.has_value());
    REQUIRE(
        std::holds_alternative<message_20::datatypes::DER_Dynamic_AC_CLReqControlMode>(request_message->control_mode));
    const auto& mode = std::get<message_20::datatypes::DER_Dynamic_AC_CLReqControlMode>(request_message->control_mode);
    REQUIRE(message_20::datatypes::from_RationalNumber(mode.max_charge_power) == Catch::Approx(11000.0f));
    REQUIRE(message_20::datatypes::from_RationalNumber(mode.min_charge_power) == Catch::Approx(1000.0f));
    REQUIRE(message_20::datatypes::from_RationalNumber(mode.present_active_power) == Catch::Approx(5000.0f));
    // Discharge capability advertised on every loop request.
    REQUIRE(message_20::datatypes::from_RationalNumber(mode.max_discharge_power) == Catch::Approx(9000.0f));
    REQUIRE(message_20::datatypes::from_RationalNumber(mode.min_discharge_power) == Catch::Approx(800.0f));
    // No grid event asserted by the EV.
    REQUIRE(mode.grid_event_condition == 0);
    // The power limits are three-phase totals: no per-phase field is advertised.
    REQUIRE_FALSE(mode.max_charge_power_L2.has_value());
    REQUIRE_FALSE(mode.max_charge_power_L3.has_value());
    REQUIRE_FALSE(mode.min_charge_power_L2.has_value());
    REQUIRE_FALSE(mode.min_charge_power_L3.has_value());
    REQUIRE_FALSE(mode.present_active_power_L2.has_value());
    REQUIRE_FALSE(mode.present_active_power_L3.has_value());
    REQUIRE_FALSE(mode.max_discharge_power_L2.has_value());
    REQUIRE_FALSE(mode.max_discharge_power_L3.has_value());
    REQUIRE_FALSE(mode.min_discharge_power_L2.has_value());
    REQUIRE_FALSE(mode.min_discharge_power_L3.has_value());
}

SCENARIO("ISO15118-20 EV AC_DER_IEC_ChargeLoop states charge and discharge limits for a ThreePhase connector") {
    const ev::feedback::Callbacks callbacks{};
    PrimedState<ev::d20::state::AC_DER_IEC_ChargeLoop> primed{callbacks, [](FsmStateHelper& helper) {
                                                                  ev::AcChargeParams p{};
                                                                  p.phase_count = 3;
                                                                  p.max_charge_power = 11100.0f;
                                                                  p.min_charge_power = 900.0f;
                                                                  p.max_discharge_power = 9000.0f;
                                                                  p.min_discharge_power = 600.0f;
                                                                  p.present_active_power = 5100.0f;
                                                                  helper.set_ac_params(p);
                                                                  helper.get_context().set_selected_ac_connector(
                                                                      message_20::datatypes::AcConnector::ThreePhase);
                                                              }};

    const auto requests = primed.take_requests();
    const auto request_message = requests.get<message_20::DER_AC_ChargeLoopRequest>();
    REQUIRE(request_message.has_value());
    const auto& mode = std::get<message_20::datatypes::DER_Dynamic_AC_CLReqControlMode>(request_message->control_mode);

    REQUIRE(message_20::datatypes::from_RationalNumber(mode.max_charge_power) == Catch::Approx(11100.0f));
    REQUIRE_FALSE(mode.max_charge_power_L2.has_value());
    REQUIRE_FALSE(mode.max_charge_power_L3.has_value());

    REQUIRE(message_20::datatypes::from_RationalNumber(mode.min_charge_power) == Catch::Approx(900.0f));
    REQUIRE_FALSE(mode.min_charge_power_L2.has_value());

    REQUIRE(message_20::datatypes::from_RationalNumber(mode.present_active_power) == Catch::Approx(5100.0f));
    REQUIRE_FALSE(mode.present_active_power_L2.has_value());

    // Discharge is stated the same way: the total on the base element, no line elements.
    REQUIRE(message_20::datatypes::from_RationalNumber(mode.max_discharge_power) == Catch::Approx(9000.0f));
    REQUIRE_FALSE(mode.max_discharge_power_L2.has_value());
    REQUIRE_FALSE(mode.max_discharge_power_L3.has_value());

    REQUIRE(message_20::datatypes::from_RationalNumber(mode.min_discharge_power) == Catch::Approx(600.0f));
    REQUIRE_FALSE(mode.min_discharge_power_L2.has_value());
}

SCENARIO("ISO15118-20 EV AC_DER_IEC_ChargeLoop fires der_control on a Dynamic response") {
    float reported = 0.0f;
    bool fired = false;
    ev::feedback::Callbacks callbacks{};
    callbacks.der_control = [&](const message_20::datatypes::DER_Dynamic_AC_CLResControlMode& mode) {
        fired = true;
        reported = message_20::datatypes::from_RationalNumber(mode.target_active_power);
    };
    PrimedState<ev::d20::state::AC_DER_IEC_ChargeLoop> primed{callbacks, seed_present_5000};

    primed.handle_response(make_res(SESSION_HEADER, ResponseCode::OK));
    const auto result = primed.feed(ev::d20::Event::V2GTP_MESSAGE);

    REQUIRE(fired == true);
    REQUIRE(reported == Catch::Approx(7000.0f));
    REQUIRE(result.transitioned() == false);
    REQUIRE(primed.fsm.get_current_state_id() == ev::d20::StateID::AC_DER_IEC_ChargeLoop);
}

SCENARIO("ISO15118-20 EV AC_DER_IEC_ChargeLoop stays and re-emits a request on a non-Terminate response") {
    StopObserver obs;
    PrimedState<ev::d20::state::AC_DER_IEC_ChargeLoop> primed{obs.callbacks, seed_present_5000};

    REQUIRE(primed.helper.get_message_exchange().take_request().has_value());
    REQUIRE_FALSE(primed.helper.get_message_exchange().has_request());

    primed.handle_response(make_res(SESSION_HEADER, ResponseCode::OK));
    const auto result = primed.feed(ev::d20::Event::V2GTP_MESSAGE);

    REQUIRE(result.transitioned() == false);
    REQUIRE(primed.fsm.get_current_state_id() == ev::d20::StateID::AC_DER_IEC_ChargeLoop);
    REQUIRE(primed.ctx.is_session_stopped() == false);
    REQUIRE(obs.fired == false);

    const auto requests = primed.take_requests();
    REQUIRE(requests.get<message_20::DER_AC_ChargeLoopRequest>().has_value());
    REQUIRE_FALSE(requests.get<message_20::PowerDeliveryRequest>().has_value());
}

SCENARIO("ISO15118-20 EV AC_DER_IEC_ChargeLoop does not substitute the dictated target for a measurement") {
    const ev::feedback::Callbacks callbacks{};
    // Unfed present power stays unreported; see the note in ac_charge_loop.cpp.
    const auto seed_unfed_present = [](FsmStateHelper& helper) {
        ev::AcChargeParams p{};
        p.phase_count = 1;
        p.max_charge_power = 11000.0f;
        p.min_charge_power = 1000.0f;
        p.max_discharge_power = 9000.0f;
        p.min_discharge_power = 800.0f;
        helper.set_ac_params(p);
    };
    PrimedState<ev::d20::state::AC_DER_IEC_ChargeLoop> primed{callbacks, seed_unfed_present};

    const auto first = primed.take_requests().get<message_20::DER_AC_ChargeLoopRequest>();
    REQUIRE(first.has_value());
    const auto& first_mode = std::get<message_20::datatypes::DER_Dynamic_AC_CLReqControlMode>(first->control_mode);
    REQUIRE(message_20::datatypes::from_RationalNumber(first_mode.present_active_power) == Catch::Approx(0.0f));

    primed.handle_response(make_res(SESSION_HEADER, ResponseCode::OK));
    primed.feed(ev::d20::Event::V2GTP_MESSAGE);

    const auto second = primed.take_requests().get<message_20::DER_AC_ChargeLoopRequest>();
    REQUIRE(second.has_value());
    const auto& second_mode = std::get<message_20::datatypes::DER_Dynamic_AC_CLReqControlMode>(second->control_mode);
    REQUIRE(message_20::datatypes::from_RationalNumber(second_mode.present_active_power) == Catch::Approx(0.0f));
}

SCENARIO("ISO15118-20 EV AC_DER_IEC_ChargeLoop fires stop_from_charger and drives PowerDelivery(Stop) on Terminate") {
    StopObserver obs;
    PrimedState<ev::d20::state::AC_DER_IEC_ChargeLoop> primed{obs.callbacks, seed_present_5000};

    primed.handle_response(
        make_res(SESSION_HEADER, ResponseCode::OK,
                 message_20::datatypes::EvseStatus{0, message_20::datatypes::EvseNotification::Terminate}));
    const auto result = primed.feed(ev::d20::Event::V2GTP_MESSAGE);

    REQUIRE(obs.fired == true);
    REQUIRE(obs.der_control_fired == false);
    REQUIRE(result.transitioned() == true);
    REQUIRE(primed.fsm.get_current_state_id() == ev::d20::StateID::PowerDelivery);
    REQUIRE(primed.ctx.is_session_stopped() == false);

    const auto requests = primed.take_requests();
    const auto pd_request = requests.get<message_20::PowerDeliveryRequest>();
    REQUIRE(pd_request.has_value());
    REQUIRE(pd_request->charge_progress == message_20::datatypes::Progress::Stop);
}

SCENARIO("ISO15118-20 EV AC_DER_IEC_ChargeLoop defers an EV-initiated stop to the next response boundary") {
    StopObserver obs;
    PrimedState<ev::d20::state::AC_DER_IEC_ChargeLoop> primed{obs.callbacks, seed_present_5000};

    primed.ctx.set_stop_charging_requested(true);
    const auto control_result = primed.feed(ev::d20::Event::CONTROL_MESSAGE);

    REQUIRE(control_result.transitioned() == false);
    REQUIRE(primed.fsm.get_current_state_id() == ev::d20::StateID::AC_DER_IEC_ChargeLoop);
    const auto pre_stop_requests = primed.take_requests();
    REQUIRE_FALSE(pre_stop_requests.get<message_20::PowerDeliveryRequest>().has_value());

    primed.handle_response(make_res(SESSION_HEADER, ResponseCode::OK));
    const auto result = primed.feed(ev::d20::Event::V2GTP_MESSAGE);

    REQUIRE(result.transitioned() == true);
    REQUIRE(primed.fsm.get_current_state_id() == ev::d20::StateID::PowerDelivery);
    REQUIRE(obs.fired == false);
    // The stop is EV-driven, not a SECC directive: no der_control surfaced.
    REQUIRE(obs.der_control_fired == false);

    const auto requests = primed.take_requests();
    const auto pd_request = requests.get<message_20::PowerDeliveryRequest>();
    REQUIRE(pd_request.has_value());
    REQUIRE(pd_request->charge_progress == message_20::datatypes::Progress::Stop);
}

SCENARIO("ISO15118-20 EV AC_DER_IEC_ChargeLoop honors a stop request set before the state was entered") {
    StopObserver obs;
    const auto seed_latched_stop = [](FsmStateHelper& helper) {
        ev::AcChargeParams p{};
        p.phase_count = 1;
        p.max_charge_power = 11000.0f;
        p.min_charge_power = 1000.0f;
        p.max_discharge_power = 9000.0f;
        p.min_discharge_power = 800.0f;
        p.present_active_power = 5000.0f;
        helper.set_ac_params(p);
        helper.get_context().set_stop_charging_requested(true);
    };
    PrimedState<ev::d20::state::AC_DER_IEC_ChargeLoop> primed{obs.callbacks, seed_latched_stop};

    primed.handle_response(make_res(SESSION_HEADER, ResponseCode::OK));
    const auto result = primed.feed(ev::d20::Event::V2GTP_MESSAGE);

    REQUIRE(result.transitioned() == true);
    REQUIRE(primed.fsm.get_current_state_id() == ev::d20::StateID::PowerDelivery);
    REQUIRE(obs.fired == false);
    // A pre-entry stop request tears down without surfacing a der_control directive.
    REQUIRE(obs.der_control_fired == false);

    const auto requests = primed.take_requests();
    const auto pd_request = requests.get<message_20::PowerDeliveryRequest>();
    REQUIRE(pd_request.has_value());
    REQUIRE(pd_request->charge_progress == message_20::datatypes::Progress::Stop);
}

SCENARIO(
    "ISO15118-20 EV AC_DER_IEC_ChargeLoop stops the session on a Scheduled control-mode reply it never requested") {
    bool fired = false;
    ev::feedback::Callbacks callbacks{};
    callbacks.der_control = [&](const message_20::datatypes::DER_Dynamic_AC_CLResControlMode&) { fired = true; };
    PrimedState<ev::d20::state::AC_DER_IEC_ChargeLoop> primed{callbacks, seed_present_5000};

    auto res = make_res(SESSION_HEADER, ResponseCode::OK);
    res.control_mode = message_20::datatypes::DER_Scheduled_AC_CLResControlMode{};
    expect_stops_session(primed, res, ev::d20::StateID::AC_DER_IEC_ChargeLoop);
    REQUIRE(fired == false);
}

namespace {
using iso15118::iec::DERControlName;

// A Dynamic DER response carrying both DSO setpoints, so the runtime guard has
// something to strip when the matching function was not negotiated.
message_20::DER_AC_ChargeLoopResponse make_res_with_setpoints(const message_20::Header& header) {
    auto res = make_res(header, ResponseCode::OK);
    auto& mode = std::get<message_20::datatypes::DER_Dynamic_AC_CLResControlMode>(res.control_mode);
    message_20::datatypes::DsoQSetpoint q{};
    q.dso_q_setpoint_value = message_20::datatypes::from_float(1500.0f);
    mode.dso_q_setpoint = q;
    message_20::datatypes::DsoCosPhiSetpoint cos_phi{};
    cos_phi.dso_cos_phi_setpoint_value = message_20::datatypes::from_float(0.95f);
    mode.dso_cos_phi_setpoint = cos_phi;
    return res;
}
} // namespace

SCENARIO("ISO15118-20 EV AC_DER_IEC_ChargeLoop strips an un-negotiated DSO setpoint before firing der_control") {
    std::optional<message_20::datatypes::DER_Dynamic_AC_CLResControlMode> captured;
    ev::feedback::Callbacks callbacks{};
    callbacks.der_control = [&](const message_20::datatypes::DER_Dynamic_AC_CLResControlMode& mode) {
        captured = mode;
    };
    PrimedState<ev::d20::state::AC_DER_IEC_ChargeLoop> primed{callbacks, seed_present_5000};
    // Nothing negotiated: both DSO setpoints must be stripped from the surfaced directive.

    primed.handle_response(make_res_with_setpoints(SESSION_HEADER));
    const auto result = primed.feed(ev::d20::Event::V2GTP_MESSAGE);

    REQUIRE(result.transitioned() == false);
    REQUIRE(captured.has_value());
    REQUIRE_FALSE(captured->dso_q_setpoint.has_value());
    REQUIRE_FALSE(captured->dso_cos_phi_setpoint.has_value());
}

SCENARIO("ISO15118-20 EV AC_DER_IEC_ChargeLoop passes a supported DSO Q setpoint through to der_control") {
    std::optional<message_20::datatypes::DER_Dynamic_AC_CLResControlMode> captured;
    ev::feedback::Callbacks callbacks{};
    callbacks.der_control = [&](const message_20::datatypes::DER_Dynamic_AC_CLResControlMode& mode) {
        captured = mode;
    };
    ev::d20::SessionOptions options{};
    options.der_control_functions.dso_q_setpoint_provision = true;
    PrimedState<ev::d20::state::AC_DER_IEC_ChargeLoop> primed{
        callbacks, message_20::datatypes::ServiceCategory::AC_DER_IEC, options, seed_present_5000};

    primed.handle_response(make_res_with_setpoints(SESSION_HEADER));
    const auto result = primed.feed(ev::d20::Event::V2GTP_MESSAGE);

    REQUIRE(result.transitioned() == false);
    REQUIRE(captured.has_value());
    // Q is supported, so it stays. cos phi is not, so it is stripped: acting on it is impossible.
    REQUIRE(captured->dso_q_setpoint.has_value());
    REQUIRE_FALSE(captured->dso_cos_phi_setpoint.has_value());
}

SCENARIO("ISO15118-20 EV AC_DER_IEC_ChargeLoop stops the session on a FAILED response mid-loop") {
    StopObserver obs;
    PrimedState<ev::d20::state::AC_DER_IEC_ChargeLoop> primed{obs.callbacks, seed_present_5000};

    // One accepted iteration first, so the rejection below happens mid-loop and not on entry.
    primed.handle_response(make_res(SESSION_HEADER, ResponseCode::OK));
    REQUIRE(primed.feed(ev::d20::Event::V2GTP_MESSAGE).transitioned() == false);
    REQUIRE(primed.ctx.is_session_stopped() == false);
    REQUIRE(primed.take_requests().get<message_20::DER_AC_ChargeLoopRequest>().has_value());
    obs.der_control_fired = false;

    expect_stops_session(primed, make_res(SESSION_HEADER, ResponseCode::FAILED_SequenceError),
                         ev::d20::StateID::AC_DER_IEC_ChargeLoop);
    REQUIRE(obs.fired == false);
    REQUIRE(obs.der_control_fired == false);
    REQUIRE(primed.take_requests().empty());
}

SCENARIO("ISO15118-20 EV AC_DER_IEC_ChargeLoop rejects malformed responses") {
    const ev::feedback::Callbacks callbacks{};
    const auto make_fsm = [](FsmStateHelper& helper) {
        auto& ctx = helper.get_context();
        ctx.get_session().set_id(SESSION_HEADER.session_id);
        return fsm::v2::FSM<ev::d20::StateBase>{ctx.create_state<ev::d20::state::AC_DER_IEC_ChargeLoop>()};
    };
    const auto make_ok = [](const message_20::Header& header) { return make_res(header, ResponseCode::OK); };
    const auto wrong = message_20::AuthorizationResponse{SESSION_HEADER, ResponseCode::OK,
                                                         message_20::datatypes::Processing::Finished};
    check_rejection_paths(callbacks, ev::d20::StateID::AC_DER_IEC_ChargeLoop, make_fsm, make_ok, wrong);
}

namespace {
struct PauseObserver {
    bool fired = false;
    ev::feedback::Callbacks callbacks{};
    PauseObserver() {
        callbacks.pause_from_charger = [this]() { fired = true; };
    }
};
} // namespace

SCENARIO("ISO15118-20 EV AC_DER_IEC_ChargeLoop fires pause_from_charger and drives PowerDelivery(Stop) on Pause") {
    PauseObserver obs;
    PrimedState<ev::d20::state::AC_DER_IEC_ChargeLoop> primed{
        obs.callbacks, message_20::datatypes::ServiceCategory::AC_DER_IEC, seed_present_5000};

    primed.handle_response(
        make_res(SESSION_HEADER, ResponseCode::OK,
                 message_20::datatypes::EvseStatus{0, message_20::datatypes::EvseNotification::Pause}));
    const auto result = primed.feed(ev::d20::Event::V2GTP_MESSAGE);

    REQUIRE(obs.fired == true);
    REQUIRE(result.transitioned() == true);
    REQUIRE(primed.fsm.get_current_state_id() == ev::d20::StateID::PowerDelivery);
    REQUIRE(primed.ctx.requested_stop_reason() == message_20::datatypes::ChargingSession::Pause);

    const auto requests = primed.take_requests();
    const auto pd_request = requests.get<message_20::PowerDeliveryRequest>();
    REQUIRE(pd_request.has_value());
    REQUIRE(pd_request->charge_progress == message_20::datatypes::Progress::Stop);
}

SCENARIO("ISO15118-20 EV AC_DER_IEC_ChargeLoop diverts to PowerDelivery(Stop) on an EV pause request") {
    StopObserver obs;
    PrimedState<ev::d20::state::AC_DER_IEC_ChargeLoop> primed{
        obs.callbacks, message_20::datatypes::ServiceCategory::AC_DER_IEC, seed_present_5000};
    primed.ctx.set_pause_charging_requested(true);

    primed.handle_response(make_res(SESSION_HEADER, ResponseCode::OK));
    const auto result = primed.feed(ev::d20::Event::V2GTP_MESSAGE);

    REQUIRE(obs.fired == false);
    REQUIRE(obs.der_control_fired == false);
    REQUIRE(result.transitioned() == true);
    REQUIRE(primed.fsm.get_current_state_id() == ev::d20::StateID::PowerDelivery);
}

// Table L.31 offers AC_DER_IEC in both control modes, and Tables L.8 and L.10 define the
// Scheduled DER request and response, so a Scheduled session must be driven end to end.
namespace {

message_20::DER_AC_ChargeLoopResponse make_scheduled_res(const message_20::Header& header) {
    message_20::DER_AC_ChargeLoopResponse res;
    res.header = header;
    res.response_code = ResponseCode::OK;
    message_20::datatypes::DER_Scheduled_AC_CLResControlMode mode{};
    mode.max_charge_power = message_20::datatypes::from_float(11000.0f);
    mode.max_discharge_power = message_20::datatypes::from_float(9000.0f);
    res.control_mode = mode;
    return res;
}

const auto seed_scheduled = [](FsmStateHelper& helper) {
    seed_present_5000(helper);
    helper.get_context().set_selected_control_mode(message_20::datatypes::ControlMode::Scheduled);
};

} // namespace

SCENARIO("ISO15118-20 EV AC_DER_IEC_ChargeLoop drives a Scheduled DER session") {
    std::optional<message_20::datatypes::DER_Scheduled_AC_CLResControlMode> captured;
    ev::feedback::Callbacks callbacks{};
    callbacks.der_control_scheduled = [&](const message_20::datatypes::DER_Scheduled_AC_CLResControlMode& mode) {
        captured = mode;
    };
    PrimedState<ev::d20::state::AC_DER_IEC_ChargeLoop> primed{callbacks, seed_scheduled};

    const auto requests = primed.take_requests();
    const auto request_message = requests.get<message_20::DER_AC_ChargeLoopRequest>();
    REQUIRE(request_message.has_value());

    const auto* mode =
        std::get_if<message_20::datatypes::DER_Scheduled_AC_CLReqControlMode>(&request_message->control_mode);
    REQUIRE(mode != nullptr);
    // Scheduled makes the base charge element optional; the DER discharge element is mandatory.
    REQUIRE(mode->max_charge_power.has_value());
    REQUIRE(message_20::datatypes::from_RationalNumber(*mode->max_charge_power) == Catch::Approx(11000.0f));
    REQUIRE(message_20::datatypes::from_RationalNumber(mode->max_discharge_power) == Catch::Approx(9000.0f));

    primed.handle_response(make_scheduled_res(SESSION_HEADER));
    const auto result = primed.feed(ev::d20::Event::V2GTP_MESSAGE);

    REQUIRE(result.transitioned() == false);
    REQUIRE(primed.ctx.is_session_stopped() == false);
    REQUIRE(captured.has_value());
}

SCENARIO("ISO15118-20 EV AC_DER_IEC_ChargeLoop stops when the DER response changes control mode") {
    const ev::feedback::Callbacks callbacks{};
    PrimedState<ev::d20::state::AC_DER_IEC_ChargeLoop> primed{callbacks, seed_scheduled};

    // The EV asked Scheduled; a Dynamic response is not the session it negotiated.
    primed.handle_response(make_res(SESSION_HEADER, ResponseCode::OK));
    const auto result = primed.feed(ev::d20::Event::V2GTP_MESSAGE);

    REQUIRE(result.transitioned() == false);
    REQUIRE(primed.ctx.is_session_stopped() == true);
}
