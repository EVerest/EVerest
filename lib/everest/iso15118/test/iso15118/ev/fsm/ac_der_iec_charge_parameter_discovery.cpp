// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <optional>

#include "helper.hpp"

#include <iso15118/ev/d20/state/ac_der_iec_charge_parameter_discovery.hpp>
#include <iso15118/message/ac_der_iec_charge_parameter_discovery.hpp>
#include <iso15118/message/common_types.hpp>
#include <iso15118/message/schedule_exchange.hpp>
#include <iso15118/message/type.hpp>

using namespace iso15118;

namespace {
using message_20::datatypes::ResponseCode;

message_20::DER_AC_ChargeParameterDiscoveryResponse make_response(const message_20::Header& header, ResponseCode code) {
    message_20::DER_AC_ChargeParameterDiscoveryResponse res{};
    res.header = header;
    res.response_code = code;
    auto& mode = res.transfer_mode;
    mode.max_charge_power = message_20::datatypes::from_float(15000.0f);
    mode.min_charge_power = message_20::datatypes::from_float(500.0f);
    mode.nominal_frequency = message_20::datatypes::from_float(50.0f);
    mode.nominal_charge_power = message_20::datatypes::from_float(15000.0f);
    mode.nominal_discharge_power = message_20::datatypes::from_float(11000.0f);
    mode.max_discharge_power = message_20::datatypes::from_float(11000.0f);
    mode.operating_mode = message_20::datatypes::OperatingMode::GridForming;
    mode.grid_connection_mode = message_20::datatypes::GridConnectionMode::GridIslanded;
    return res;
}

// AC_DER_IEC_ChargeParameterDiscovery builds its request from the EV's AC charge params.
const auto seed_charge_limits = [](FsmStateHelper& helper) {
    ev::AcChargeParams p{};
    p.phase_count = 1;
    p.max_charge_power = 22000.0f;
    p.min_charge_power = 1000.0f;
    p.max_discharge_power = 15000.0f;
    p.min_discharge_power = 800.0f;
    helper.set_ac_params(p);
};
} // namespace

SCENARIO("ISO15118-20 EV AC_DER_IEC_ChargeParameterDiscovery emits a DER request on enter") {
    const ev::feedback::Callbacks callbacks{};
    PrimedState<ev::d20::state::AC_DER_IEC_ChargeParameterDiscovery> primed{callbacks, seed_charge_limits};

    const auto requests = primed.take_requests();
    const auto request_message = requests.get<message_20::DER_AC_ChargeParameterDiscoveryRequest>();
    REQUIRE(request_message.has_value());
    REQUIRE(request_message->header.session_id == SESSION_HEADER.session_id);

    const auto& mode = request_message->transfer_mode;
    REQUIRE(message_20::datatypes::from_RationalNumber(mode.max_charge_power) == Catch::Approx(22000.0f));
    REQUIRE(message_20::datatypes::from_RationalNumber(mode.min_charge_power) == Catch::Approx(1000.0f));
    // Discharge limits come from the discharge params, never mirrored from charge.
    REQUIRE(message_20::datatypes::from_RationalNumber(mode.max_discharge_power) == Catch::Approx(15000.0f));
    REQUIRE(message_20::datatypes::from_RationalNumber(mode.min_discharge_power) == Catch::Approx(800.0f));
    // The EV drives a single discovery round: processing is Finished.
    REQUIRE(mode.processing == message_20::datatypes::Processing::Finished);
    // The limits are three-phase totals: no per-phase charge or discharge field is advertised.
    REQUIRE_FALSE(mode.max_charge_power_L2.has_value());
    REQUIRE_FALSE(mode.max_charge_power_L3.has_value());
    REQUIRE_FALSE(mode.min_charge_power_L2.has_value());
    REQUIRE_FALSE(mode.min_charge_power_L3.has_value());
    REQUIRE_FALSE(mode.max_discharge_power_L2.has_value());
    REQUIRE_FALSE(mode.max_discharge_power_L3.has_value());
    REQUIRE_FALSE(mode.min_discharge_power_L2.has_value());
    REQUIRE_FALSE(mode.min_discharge_power_L3.has_value());
}

SCENARIO("ISO15118-20 EV AC_DER_IEC_ChargeParameterDiscovery states the totals for a ThreePhase connector") {
    const ev::feedback::Callbacks callbacks{};
    PrimedState<ev::d20::state::AC_DER_IEC_ChargeParameterDiscovery> primed{
        callbacks, [](FsmStateHelper& helper) {
            ev::AcChargeParams p{};
            p.phase_count = 3;
            p.max_charge_power = 22500.0f;
            p.min_charge_power = 1500.0f;
            helper.set_ac_params(p);
            helper.get_context().set_selected_ac_connector(message_20::datatypes::AcConnector::ThreePhase);
        }};

    const auto requests = primed.take_requests();
    const auto request_message = requests.get<message_20::DER_AC_ChargeParameterDiscoveryRequest>();
    REQUIRE(request_message.has_value());

    const auto& mode = request_message->transfer_mode;

    // [V2G20-1820]: the base element alone states the three-phase total.
    REQUIRE(message_20::datatypes::from_RationalNumber(mode.max_charge_power) == 22500.0f);
    REQUIRE_FALSE(mode.max_charge_power_L2.has_value());
    REQUIRE_FALSE(mode.max_charge_power_L3.has_value());

    REQUIRE(message_20::datatypes::from_RationalNumber(mode.min_charge_power) == 1500.0f);
    REQUIRE_FALSE(mode.min_charge_power_L2.has_value());
}

SCENARIO("ISO15118-20 EV AC_DER_IEC_ChargeParameterDiscovery transitions to ScheduleExchange and fires "
         "ac_der_limits") {
    std::optional<message_20::datatypes::DER_AC_CPDResEnergyTransferMode> captured;
    bool base_fired = false;
    ev::feedback::Callbacks callbacks{};
    callbacks.ac_der_limits = [&](const message_20::datatypes::DER_AC_CPDResEnergyTransferMode& mode) {
        captured = mode;
    };
    callbacks.ac_limits = [&](const message_20::datatypes::AC_CPDResEnergyTransferMode&) { base_fired = true; };
    PrimedState<ev::d20::state::AC_DER_IEC_ChargeParameterDiscovery> primed{callbacks, seed_charge_limits};

    primed.handle_response(make_response(SESSION_HEADER, ResponseCode::OK));
    const auto result = primed.feed(ev::d20::Event::V2GTP_MESSAGE);

    REQUIRE(result.transitioned() == true);
    REQUIRE(primed.fsm.get_current_state_id() == ev::d20::StateID::ScheduleExchange);
    REQUIRE(primed.ctx.is_session_stopped() == false);
    REQUIRE(captured.has_value());

    // The AC base the DER response inherits.
    REQUIRE(message_20::datatypes::from_RationalNumber(captured->nominal_frequency) == Catch::Approx(50.0f));
    REQUIRE(message_20::datatypes::from_RationalNumber(captured->max_charge_power) == Catch::Approx(15000.0f));

    // The DER-only fields. A base-typed binding delivers none of these.
    REQUIRE(message_20::datatypes::from_RationalNumber(captured->nominal_charge_power) == Catch::Approx(15000.0f));
    REQUIRE(message_20::datatypes::from_RationalNumber(captured->nominal_discharge_power) == Catch::Approx(11000.0f));
    REQUIRE(message_20::datatypes::from_RationalNumber(captured->max_discharge_power) == Catch::Approx(11000.0f));
    REQUIRE(captured->operating_mode == message_20::datatypes::OperatingMode::GridForming);
    REQUIRE(captured->grid_connection_mode == message_20::datatypes::GridConnectionMode::GridIslanded);

    // The base-typed callback stays silent, the way a BPT response leaves ac_limits alone.
    REQUIRE_FALSE(base_fired);
}

SCENARIO("ISO15118-20 EV AC_DER_IEC_ChargeParameterDiscovery fires der_curves with the dictated DerControl") {
    std::optional<message_20::datatypes::DerControl> captured;
    ev::feedback::Callbacks callbacks{};
    callbacks.der_curves = [&](const message_20::datatypes::DerControl& control) { captured = control; };
    PrimedState<ev::d20::state::AC_DER_IEC_ChargeParameterDiscovery> primed{callbacks, seed_charge_limits};

    auto response = make_response(SESSION_HEADER, ResponseCode::OK);
    response.transfer_mode.der_control.max_level_dc_injection = message_20::datatypes::from_float(0.5f);
    primed.handle_response(response);
    const auto result = primed.feed(ev::d20::Event::V2GTP_MESSAGE);

    REQUIRE(result.transitioned() == true);
    REQUIRE(captured.has_value());
    REQUIRE(captured->max_level_dc_injection.has_value());
    REQUIRE(message_20::datatypes::from_RationalNumber(*captured->max_level_dc_injection) == Catch::Approx(0.5f));
}

SCENARIO("ISO15118-20 EV AC_DER_IEC_ChargeParameterDiscovery rejects malformed responses") {
    const ev::feedback::Callbacks callbacks{};
    const auto make_fsm = [](FsmStateHelper& helper) {
        auto& ctx = helper.get_context();
        ctx.get_session().set_id(SESSION_HEADER.session_id);
        return fsm::v2::FSM<ev::d20::StateBase>{
            ctx.create_state<ev::d20::state::AC_DER_IEC_ChargeParameterDiscovery>()};
    };
    const auto make_ok = [](const message_20::Header& header) { return make_response(header, ResponseCode::OK); };
    check_rejection_paths(callbacks, ev::d20::StateID::AC_DER_IEC_ChargeParameterDiscovery, make_fsm, make_ok,
                          message_20::ScheduleExchangeResponse{});
}
