// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#include <catch2/catch_test_macros.hpp>

#include "helper.hpp"

#include <iso15118/ev/d20/state/dc_charge_parameter_discovery.hpp>
#include <iso15118/message/common_types.hpp>
#include <iso15118/message/dc_charge_parameter_discovery.hpp>
#include <iso15118/message/schedule_exchange.hpp>
#include <iso15118/message/type.hpp>

using namespace iso15118;

namespace {
using message_20::datatypes::ResponseCode;

message_20::DC_ChargeParameterDiscoveryResponse make_response(const message_20::Header& header, ResponseCode code) {
    message_20::DC_ChargeParameterDiscoveryResponse res{};
    res.header = header;
    res.response_code = code;
    res.transfer_mode = message_20::datatypes::DC_CPDResEnergyTransferMode{};
    return res;
}

message_20::DC_ChargeParameterDiscoveryResponse make_bpt_response(const message_20::Header& header, ResponseCode code) {
    message_20::DC_ChargeParameterDiscoveryResponse res{};
    res.header = header;
    res.response_code = code;
    message_20::datatypes::BPT_DC_CPDResEnergyTransferMode mode{};
    mode.max_charge_power = message_20::datatypes::from_float(15000.0f);
    mode.max_discharge_power = message_20::datatypes::from_float(12000.0f);
    mode.min_discharge_power = message_20::datatypes::from_float(300.0f);
    mode.max_discharge_current = message_20::datatypes::from_float(150.0f);
    res.transfer_mode = mode;
    return res;
}

// DC_ChargeParameterDiscovery builds its request from the EV's DC charge params.
const auto seed_params = [](FsmStateHelper& helper) {
    ev::DcChargeParams p{};
    p.max_charge_power = 11000.0f;
    p.max_charge_current = 200.0f;
    p.max_voltage = 500.0f;
    p.min_voltage = 150.0f;
    helper.set_dc_params(p);
};

const auto seed_bpt_params = [](FsmStateHelper& helper) {
    helper.get_context().set_selected_service(message_20::datatypes::ServiceCategory::DC_BPT);
    ev::DcChargeParams p{};
    p.max_charge_power = 11000.0f;
    p.max_charge_current = 200.0f;
    p.max_voltage = 500.0f;
    p.min_voltage = 150.0f;
    p.max_discharge_power = 9000.0f;
    p.min_discharge_power = 500.0f;
    p.max_discharge_current = 180.0f;
    helper.set_dc_params(p);
};
} // namespace

SCENARIO("ISO15118-20 EV DC_ChargeParameterDiscovery emits a DC request built from the DC params on enter") {
    const ev::feedback::Callbacks callbacks{};
    PrimedState<ev::d20::state::DC_ChargeParameterDiscovery> primed{callbacks, seed_params};

    const auto requests = primed.take_requests();
    const auto request_message = requests.get<message_20::DC_ChargeParameterDiscoveryRequest>();
    REQUIRE(request_message.has_value());
    REQUIRE(request_message->header.session_id == SESSION_HEADER.session_id);

    const auto* mode = std::get_if<message_20::datatypes::DC_CPDReqEnergyTransferMode>(&request_message->transfer_mode);
    REQUIRE(mode != nullptr);
    REQUIRE(message_20::datatypes::from_RationalNumber(mode->max_charge_power) == 11000.0f);
    REQUIRE(message_20::datatypes::from_RationalNumber(mode->max_charge_current) == 200.0f);
    REQUIRE(message_20::datatypes::from_RationalNumber(mode->max_voltage) == 500.0f);
    REQUIRE(message_20::datatypes::from_RationalNumber(mode->min_voltage) == 150.0f);
    REQUIRE_FALSE(mode->target_soc.has_value());
}

SCENARIO("ISO15118-20 EV DC_ChargeParameterDiscovery transitions to ScheduleExchange on OK") {
    const ev::feedback::Callbacks callbacks{};
    PrimedState<ev::d20::state::DC_ChargeParameterDiscovery> primed{callbacks, no_seed};

    primed.handle_response(make_response(SESSION_HEADER, ResponseCode::OK));
    const auto result = primed.feed(ev::d20::Event::V2GTP_MESSAGE);

    REQUIRE(result.transitioned() == true);
    REQUIRE(primed.fsm.get_current_state_id() == ev::d20::StateID::ScheduleExchange);
    REQUIRE(primed.ctx.is_session_stopped() == false);
}

SCENARIO("ISO15118-20 EV DC_ChargeParameterDiscovery emits a BPT request with discharge limits for a BPT session") {
    const ev::feedback::Callbacks callbacks{};
    PrimedState<ev::d20::state::DC_ChargeParameterDiscovery> primed{callbacks, seed_bpt_params};

    const auto requests = primed.take_requests();
    const auto request_message = requests.get<message_20::DC_ChargeParameterDiscoveryRequest>();
    REQUIRE(request_message.has_value());

    const auto* mode =
        std::get_if<message_20::datatypes::BPT_DC_CPDReqEnergyTransferMode>(&request_message->transfer_mode);
    REQUIRE(mode != nullptr);
    // Charge-side fields unchanged from the plain-DC conventions.
    REQUIRE(message_20::datatypes::from_RationalNumber(mode->max_charge_power) == 11000.0f);
    REQUIRE(message_20::datatypes::from_RationalNumber(mode->min_charge_power) == 0.0f);
    REQUIRE(message_20::datatypes::from_RationalNumber(mode->max_charge_current) == 200.0f);
    REQUIRE(message_20::datatypes::from_RationalNumber(mode->min_charge_current) == 0.0f);
    REQUIRE(message_20::datatypes::from_RationalNumber(mode->max_voltage) == 500.0f);
    REQUIRE(message_20::datatypes::from_RationalNumber(mode->min_voltage) == 150.0f);
    REQUIRE_FALSE(mode->target_soc.has_value());
    // Discharge limits sourced from the DC params; min_discharge_current pinned to {0,0}.
    REQUIRE(message_20::datatypes::from_RationalNumber(mode->max_discharge_power) == 9000.0f);
    REQUIRE(message_20::datatypes::from_RationalNumber(mode->min_discharge_power) == 500.0f);
    REQUIRE(message_20::datatypes::from_RationalNumber(mode->max_discharge_current) == 180.0f);
    REQUIRE(message_20::datatypes::from_RationalNumber(mode->min_discharge_current) == 0.0f);
}

SCENARIO("ISO15118-20 EV DC_ChargeParameterDiscovery transitions to ScheduleExchange and fires dc_bpt_limits on a "
         "BPT reply") {
    bool fired = false;
    float reported_max_discharge = 0.0f;
    float reported_min_discharge = 0.0f;
    float reported_max_discharge_current = 0.0f;
    ev::feedback::Callbacks callbacks{};
    callbacks.dc_bpt_limits = [&](const message_20::datatypes::BPT_DC_CPDResEnergyTransferMode& mode) {
        fired = true;
        reported_max_discharge = message_20::datatypes::from_RationalNumber(mode.max_discharge_power);
        reported_min_discharge = message_20::datatypes::from_RationalNumber(mode.min_discharge_power);
        reported_max_discharge_current = message_20::datatypes::from_RationalNumber(mode.max_discharge_current);
    };
    PrimedState<ev::d20::state::DC_ChargeParameterDiscovery> primed{callbacks, seed_bpt_params};

    primed.handle_response(make_bpt_response(SESSION_HEADER, ResponseCode::OK));
    const auto result = primed.feed(ev::d20::Event::V2GTP_MESSAGE);

    REQUIRE(result.transitioned() == true);
    REQUIRE(primed.fsm.get_current_state_id() == ev::d20::StateID::ScheduleExchange);
    REQUIRE(primed.ctx.is_session_stopped() == false);
    REQUIRE(fired == true);
    REQUIRE(reported_max_discharge == 12000.0f);
    REQUIRE(reported_min_discharge == 300.0f);
    REQUIRE(reported_max_discharge_current == 150.0f);
}

SCENARIO("ISO15118-20 EV DC_ChargeParameterDiscovery stops the session on a plain reply for a BPT session") {
    bool fired = false;
    ev::feedback::Callbacks callbacks{};
    callbacks.dc_bpt_limits = [&](const message_20::datatypes::BPT_DC_CPDResEnergyTransferMode&) { fired = true; };
    PrimedState<ev::d20::state::DC_ChargeParameterDiscovery> primed{callbacks, seed_bpt_params};

    primed.handle_response(make_response(SESSION_HEADER, ResponseCode::OK));
    const auto result = primed.feed(ev::d20::Event::V2GTP_MESSAGE);

    REQUIRE(result.transitioned() == false);
    REQUIRE(primed.fsm.get_current_state_id() == ev::d20::StateID::DC_ChargeParameterDiscovery);
    REQUIRE(primed.ctx.is_session_stopped() == true);
    REQUIRE(fired == false);
}

SCENARIO("ISO15118-20 EV DC_ChargeParameterDiscovery stops the session on a BPT reply for a plain DC session") {
    const ev::feedback::Callbacks callbacks{};
    PrimedState<ev::d20::state::DC_ChargeParameterDiscovery> primed{callbacks, seed_params};

    primed.handle_response(make_bpt_response(SESSION_HEADER, ResponseCode::OK));
    const auto result = primed.feed(ev::d20::Event::V2GTP_MESSAGE);

    REQUIRE(result.transitioned() == false);
    REQUIRE(primed.fsm.get_current_state_id() == ev::d20::StateID::DC_ChargeParameterDiscovery);
    REQUIRE(primed.ctx.is_session_stopped() == true);
}

SCENARIO("ISO15118-20 EV DC_ChargeParameterDiscovery rejects malformed responses") {
    const ev::feedback::Callbacks callbacks{};
    const auto make_fsm = [](FsmStateHelper& helper) {
        auto& ctx = helper.get_context();
        ctx.get_session().set_id(SESSION_HEADER.session_id);
        return fsm::v2::FSM<ev::d20::StateBase>{ctx.create_state<ev::d20::state::DC_ChargeParameterDiscovery>()};
    };
    const auto make_ok = [](const message_20::Header& header) { return make_response(header, ResponseCode::OK); };
    check_rejection_paths(callbacks, ev::d20::StateID::DC_ChargeParameterDiscovery, make_fsm, make_ok,
                          message_20::ScheduleExchangeResponse{});
}
