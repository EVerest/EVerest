// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#include <catch2/catch_test_macros.hpp>

#include "helper.hpp"

#include <iso15118/ev/d20/state/ac_charge_parameter_discovery.hpp>
#include <iso15118/message/ac_charge_parameter_discovery.hpp>
#include <iso15118/message/common_types.hpp>
#include <iso15118/message/schedule_exchange.hpp>
#include <iso15118/message/type.hpp>

using namespace iso15118;

namespace {
using message_20::datatypes::ResponseCode;

message_20::AC_ChargeParameterDiscoveryResponse make_response(const message_20::Header& header, ResponseCode code) {
    message_20::AC_ChargeParameterDiscoveryResponse res{};
    res.header = header;
    res.response_code = code;
    message_20::datatypes::AC_CPDResEnergyTransferMode mode{};
    mode.max_charge_power = message_20::datatypes::from_float(15000.0f);
    mode.min_charge_power = message_20::datatypes::from_float(500.0f);
    mode.nominal_frequency = message_20::datatypes::from_float(50.0f);
    res.transfer_mode = mode;
    return res;
}

// AC_ChargeParameterDiscovery builds its request from the EV's AC charge params.
const auto seed_charge_limits = [](FsmStateHelper& helper) {
    ev::AcChargeParams p{};
    p.max_charge_power = 22000.0f;
    p.min_charge_power = 1000.0f;
    helper.set_ac_params(p);
};

const auto seed_bpt_limits = [](FsmStateHelper& helper) {
    ev::AcChargeParams p{};
    p.max_charge_power = 22000.0f;
    p.min_charge_power = 1000.0f;
    p.max_discharge_power = 15000.0f;
    p.min_discharge_power = 800.0f;
    helper.set_ac_params(p);
};

message_20::AC_ChargeParameterDiscoveryResponse make_bpt_response(const message_20::Header& header, ResponseCode code) {
    message_20::AC_ChargeParameterDiscoveryResponse res{};
    res.header = header;
    res.response_code = code;
    message_20::datatypes::BPT_AC_CPDResEnergyTransferMode mode{};
    mode.max_charge_power = message_20::datatypes::from_float(15000.0f);
    mode.min_charge_power = message_20::datatypes::from_float(500.0f);
    mode.nominal_frequency = message_20::datatypes::from_float(50.0f);
    mode.max_discharge_power = message_20::datatypes::from_float(12000.0f);
    mode.min_discharge_power = message_20::datatypes::from_float(300.0f);
    res.transfer_mode = mode;
    return res;
}
} // namespace

SCENARIO("ISO15118-20 EV AC_ChargeParameterDiscovery emits a request built from the AC params on enter") {
    const ev::feedback::Callbacks callbacks{};
    PrimedState<ev::d20::state::AC_ChargeParameterDiscovery> primed{callbacks, seed_charge_limits};

    const auto requests = primed.take_requests();
    const auto request_message = requests.get<message_20::AC_ChargeParameterDiscoveryRequest>();
    REQUIRE(request_message.has_value());
    REQUIRE(request_message->header.session_id == SESSION_HEADER.session_id);

    const auto* mode = std::get_if<message_20::datatypes::AC_CPDReqEnergyTransferMode>(&request_message->transfer_mode);
    REQUIRE(mode != nullptr);
    REQUIRE(message_20::datatypes::from_RationalNumber(mode->max_charge_power) == 22000.0f);
    REQUIRE(message_20::datatypes::from_RationalNumber(mode->min_charge_power) == 1000.0f);
    // The power limits are three-phase totals: no per-phase field is advertised.
    REQUIRE_FALSE(mode->max_charge_power_L2.has_value());
    REQUIRE_FALSE(mode->max_charge_power_L3.has_value());
    REQUIRE_FALSE(mode->min_charge_power_L2.has_value());
    REQUIRE_FALSE(mode->min_charge_power_L3.has_value());
}

SCENARIO("ISO15118-20 EV AC_ChargeParameterDiscovery transitions to ScheduleExchange and fires ac_limits on OK") {
    bool fired = false;
    float reported_frequency = 0.0f;
    float reported_max_charge_power = 0.0f;
    float reported_min_charge_power = 0.0f;
    ev::feedback::Callbacks callbacks{};
    callbacks.ac_limits = [&](const message_20::datatypes::AC_CPDResEnergyTransferMode& mode) {
        fired = true;
        reported_frequency = message_20::datatypes::from_RationalNumber(mode.nominal_frequency);
        reported_max_charge_power = message_20::datatypes::from_RationalNumber(mode.max_charge_power);
        reported_min_charge_power = message_20::datatypes::from_RationalNumber(mode.min_charge_power);
    };
    PrimedState<ev::d20::state::AC_ChargeParameterDiscovery> primed{callbacks, seed_charge_limits};

    primed.handle_response(make_response(SESSION_HEADER, ResponseCode::OK));
    const auto result = primed.feed(ev::d20::Event::V2GTP_MESSAGE);

    REQUIRE(result.transitioned() == true);
    REQUIRE(primed.fsm.get_current_state_id() == ev::d20::StateID::ScheduleExchange);
    REQUIRE(primed.ctx.is_session_stopped() == false);
    REQUIRE(fired == true);
    REQUIRE(reported_frequency == 50.0f);
    REQUIRE(reported_max_charge_power == 15000.0f);
    REQUIRE(reported_min_charge_power == 500.0f);
}

SCENARIO("ISO15118-20 EV AC_ChargeParameterDiscovery rejects a BPT transfer-mode reply the EV never requested") {
    bool fired = false;
    ev::feedback::Callbacks callbacks{};
    callbacks.ac_limits = [&](const message_20::datatypes::AC_CPDResEnergyTransferMode&) { fired = true; };
    PrimedState<ev::d20::state::AC_ChargeParameterDiscovery> primed{callbacks, seed_charge_limits};

    message_20::AC_ChargeParameterDiscoveryResponse res{};
    res.header = SESSION_HEADER;
    res.response_code = ResponseCode::OK;
    message_20::datatypes::BPT_AC_CPDResEnergyTransferMode bpt_mode{};
    bpt_mode.max_charge_power = message_20::datatypes::from_float(15000.0f);
    bpt_mode.min_charge_power = message_20::datatypes::from_float(500.0f);
    bpt_mode.nominal_frequency = message_20::datatypes::from_float(50.0f);
    res.transfer_mode = bpt_mode;

    primed.handle_response(res);
    const auto result = primed.feed(ev::d20::Event::V2GTP_MESSAGE);

    REQUIRE(result.transitioned() == false);
    REQUIRE(primed.fsm.get_current_state_id() == ev::d20::StateID::AC_ChargeParameterDiscovery);
    REQUIRE(primed.ctx.is_session_stopped() == true);
    REQUIRE(fired == false);
}

SCENARIO("ISO15118-20 EV AC_ChargeParameterDiscovery emits a BPT request with discharge limits for a BPT session") {
    const ev::feedback::Callbacks callbacks{};
    PrimedState<ev::d20::state::AC_ChargeParameterDiscovery> primed{
        callbacks, message_20::datatypes::ServiceCategory::AC_BPT, seed_bpt_limits};

    const auto requests = primed.take_requests();
    const auto request_message = requests.get<message_20::AC_ChargeParameterDiscoveryRequest>();
    REQUIRE(request_message.has_value());

    const auto* mode =
        std::get_if<message_20::datatypes::BPT_AC_CPDReqEnergyTransferMode>(&request_message->transfer_mode);
    REQUIRE(mode != nullptr);
    REQUIRE(message_20::datatypes::from_RationalNumber(mode->max_charge_power) == 22000.0f);
    REQUIRE(message_20::datatypes::from_RationalNumber(mode->min_charge_power) == 1000.0f);
    REQUIRE(message_20::datatypes::from_RationalNumber(mode->max_discharge_power) == 15000.0f);
    REQUIRE(message_20::datatypes::from_RationalNumber(mode->min_discharge_power) == 800.0f);
    // The limits are three-phase totals: no per-phase charge or discharge field is advertised.
    REQUIRE_FALSE(mode->max_charge_power_L2.has_value());
    REQUIRE_FALSE(mode->max_charge_power_L3.has_value());
    REQUIRE_FALSE(mode->min_charge_power_L2.has_value());
    REQUIRE_FALSE(mode->min_charge_power_L3.has_value());
    REQUIRE_FALSE(mode->max_discharge_power_L2.has_value());
    REQUIRE_FALSE(mode->max_discharge_power_L3.has_value());
    REQUIRE_FALSE(mode->min_discharge_power_L2.has_value());
    REQUIRE_FALSE(mode->min_discharge_power_L3.has_value());
}

SCENARIO("ISO15118-20 EV AC_ChargeParameterDiscovery transitions to ScheduleExchange and fires ac_bpt_limits on a "
         "BPT reply") {
    bool fired = false;
    float reported_max_discharge = 0.0f;
    float reported_min_discharge = 0.0f;
    ev::feedback::Callbacks callbacks{};
    callbacks.ac_bpt_limits = [&](const message_20::datatypes::BPT_AC_CPDResEnergyTransferMode& mode) {
        fired = true;
        reported_max_discharge = message_20::datatypes::from_RationalNumber(mode.max_discharge_power);
        reported_min_discharge = message_20::datatypes::from_RationalNumber(mode.min_discharge_power);
    };
    PrimedState<ev::d20::state::AC_ChargeParameterDiscovery> primed{
        callbacks, message_20::datatypes::ServiceCategory::AC_BPT, seed_bpt_limits};

    primed.handle_response(make_bpt_response(SESSION_HEADER, ResponseCode::OK));
    const auto result = primed.feed(ev::d20::Event::V2GTP_MESSAGE);

    REQUIRE(result.transitioned() == true);
    REQUIRE(primed.fsm.get_current_state_id() == ev::d20::StateID::ScheduleExchange);
    REQUIRE(primed.ctx.is_session_stopped() == false);
    REQUIRE(fired == true);
    REQUIRE(reported_max_discharge == 12000.0f);
    REQUIRE(reported_min_discharge == 300.0f);
}

SCENARIO("ISO15118-20 EV AC_ChargeParameterDiscovery stops the session on a plain reply for a BPT session") {
    bool fired = false;
    ev::feedback::Callbacks callbacks{};
    callbacks.ac_bpt_limits = [&](const message_20::datatypes::BPT_AC_CPDResEnergyTransferMode&) { fired = true; };
    PrimedState<ev::d20::state::AC_ChargeParameterDiscovery> primed{
        callbacks, message_20::datatypes::ServiceCategory::AC_BPT, seed_bpt_limits};

    primed.handle_response(make_response(SESSION_HEADER, ResponseCode::OK));
    const auto result = primed.feed(ev::d20::Event::V2GTP_MESSAGE);

    REQUIRE(result.transitioned() == false);
    REQUIRE(primed.fsm.get_current_state_id() == ev::d20::StateID::AC_ChargeParameterDiscovery);
    REQUIRE(primed.ctx.is_session_stopped() == true);
    REQUIRE(fired == false);
}

SCENARIO("ISO15118-20 EV AC_ChargeParameterDiscovery rejects malformed responses") {
    const ev::feedback::Callbacks callbacks{};
    const auto make_fsm = [](FsmStateHelper& helper) {
        auto& ctx = helper.get_context();
        ctx.get_session().set_id(SESSION_HEADER.session_id);
        return fsm::v2::FSM<ev::d20::StateBase>{ctx.create_state<ev::d20::state::AC_ChargeParameterDiscovery>()};
    };
    const auto make_ok = [](const message_20::Header& header) { return make_response(header, ResponseCode::OK); };
    check_rejection_paths(callbacks, ev::d20::StateID::AC_ChargeParameterDiscovery, make_fsm, make_ok,
                          message_20::ScheduleExchangeResponse{});
}
