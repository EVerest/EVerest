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

const auto seed_present_5000 = [](FsmStateHelper& helper) {
    ev::AcChargeParams p{};
    p.max_charge_power = 11000.0f;
    p.min_charge_power = 1000.0f;
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
    const auto seed_single = [](FsmStateHelper& helper) {
        ev::AcChargeParams p{};
        p.max_charge_power = 11000.0f;
        p.min_charge_power = 1000.0f;
        p.present_active_power = 5000.0f;
        p.three_phase = false;
        helper.set_ac_params(p);
    };
    PrimedState<ev::d20::state::AC_DER_IEC_ChargeLoop> primed{callbacks, seed_single};

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
    REQUIRE(message_20::datatypes::from_RationalNumber(mode.max_discharge_power) == Catch::Approx(11000.0f));
    REQUIRE(message_20::datatypes::from_RationalNumber(mode.min_discharge_power) == Catch::Approx(1000.0f));
    // No grid event asserted by the EV.
    REQUIRE(mode.grid_event_condition == 0);
}

SCENARIO("ISO15118-20 EV AC_DER_IEC_ChargeLoop emits per-phase charge and discharge limits when three_phase") {
    const ev::feedback::Callbacks callbacks{};
    const auto seed_three_phase = [](FsmStateHelper& helper) {
        ev::AcChargeParams p{};
        p.max_charge_power = 11000.0f;
        p.min_charge_power = 1000.0f;
        p.present_active_power = 5000.0f;
        p.three_phase = true;
        helper.set_ac_params(p);
    };
    PrimedState<ev::d20::state::AC_DER_IEC_ChargeLoop> primed{callbacks, seed_three_phase};

    const auto requests = primed.take_requests();
    const auto request_message = requests.get<message_20::DER_AC_ChargeLoopRequest>();
    REQUIRE(request_message.has_value());
    REQUIRE(
        std::holds_alternative<message_20::datatypes::DER_Dynamic_AC_CLReqControlMode>(request_message->control_mode));
    const auto& mode = std::get<message_20::datatypes::DER_Dynamic_AC_CLReqControlMode>(request_message->control_mode);

    // Per-phase charge limits mirror the aggregate on L2 and L3.
    REQUIRE(mode.max_charge_power_L2.has_value());
    REQUIRE(mode.max_charge_power_L3.has_value());
    REQUIRE(mode.min_charge_power_L2.has_value());
    REQUIRE(mode.min_charge_power_L3.has_value());
    REQUIRE(message_20::datatypes::from_RationalNumber(*mode.max_charge_power_L2) == Catch::Approx(11000.0f));
    REQUIRE(message_20::datatypes::from_RationalNumber(*mode.max_charge_power_L3) == Catch::Approx(11000.0f));
    REQUIRE(message_20::datatypes::from_RationalNumber(*mode.min_charge_power_L2) == Catch::Approx(1000.0f));
    REQUIRE(message_20::datatypes::from_RationalNumber(*mode.min_charge_power_L3) == Catch::Approx(1000.0f));

    // Present active power is reported per phase.
    REQUIRE(mode.present_active_power_L2.has_value());
    REQUIRE(mode.present_active_power_L3.has_value());
    REQUIRE(message_20::datatypes::from_RationalNumber(*mode.present_active_power_L2) == Catch::Approx(5000.0f));
    REQUIRE(message_20::datatypes::from_RationalNumber(*mode.present_active_power_L3) == Catch::Approx(5000.0f));

    // Discharge capability is advertised per phase as well.
    REQUIRE(mode.max_discharge_power_L2.has_value());
    REQUIRE(mode.max_discharge_power_L3.has_value());
    REQUIRE(mode.min_discharge_power_L2.has_value());
    REQUIRE(mode.min_discharge_power_L3.has_value());
    REQUIRE(message_20::datatypes::from_RationalNumber(*mode.max_discharge_power_L2) == Catch::Approx(11000.0f));
    REQUIRE(message_20::datatypes::from_RationalNumber(*mode.max_discharge_power_L3) == Catch::Approx(11000.0f));
    REQUIRE(message_20::datatypes::from_RationalNumber(*mode.min_discharge_power_L2) == Catch::Approx(1000.0f));
    REQUIRE(message_20::datatypes::from_RationalNumber(*mode.min_discharge_power_L3) == Catch::Approx(1000.0f));
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
        p.max_charge_power = 11000.0f;
        p.min_charge_power = 1000.0f;
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
    primed.handle_response(res);
    const auto result = primed.feed(ev::d20::Event::V2GTP_MESSAGE);

    REQUIRE(result.transitioned() == false);
    REQUIRE(primed.fsm.get_current_state_id() == ev::d20::StateID::AC_DER_IEC_ChargeLoop);
    REQUIRE(primed.ctx.is_session_stopped() == true);
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

SCENARIO("ISO15118-20 EV AC_DER_IEC_ChargeLoop passes a negotiated DSO Q setpoint through to der_control") {
    std::optional<message_20::datatypes::DER_Dynamic_AC_CLResControlMode> captured;
    ev::feedback::Callbacks callbacks{};
    callbacks.der_control = [&](const message_20::datatypes::DER_Dynamic_AC_CLResControlMode& mode) {
        captured = mode;
    };
    const auto seed_negotiate_q = [](FsmStateHelper& helper) {
        seed_present_5000(helper);
        std::bitset<ev::DER_CONTROL_FUNCTION_COUNT> negotiated{};
        negotiated.set(static_cast<size_t>(DERControlName::DSOQSetpointProvision));
        helper.get_context().set_der_negotiated_functions(negotiated);
    };
    PrimedState<ev::d20::state::AC_DER_IEC_ChargeLoop> primed{callbacks, seed_negotiate_q};

    primed.handle_response(make_res_with_setpoints(SESSION_HEADER));
    const auto result = primed.feed(ev::d20::Event::V2GTP_MESSAGE);

    REQUIRE(result.transitioned() == false);
    REQUIRE(captured.has_value());
    // Q was negotiated: it stays. cos phi was not: it is stripped.
    REQUIRE(captured->dso_q_setpoint.has_value());
    REQUIRE_FALSE(captured->dso_cos_phi_setpoint.has_value());
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
