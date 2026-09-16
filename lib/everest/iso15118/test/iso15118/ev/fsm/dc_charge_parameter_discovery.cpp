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
message_20::DC_ChargeParameterDiscoveryResponse make_response(message_20::datatypes::ResponseCode code) {
    message_20::DC_ChargeParameterDiscoveryResponse res{};
    res.header.session_id = SESSION_HEADER.session_id;
    res.response_code = code;
    res.transfer_mode = message_20::datatypes::DC_CPDResEnergyTransferMode{};
    return res;
}

ev::DcChargeParams make_params() {
    ev::DcChargeParams p{};
    p.max_charge_power = 11000.0f;
    p.max_charge_current = 200.0f;
    p.max_voltage = 500.0f;
    p.min_voltage = 150.0f;
    return p;
}
} // namespace

SCENARIO("ISO15118-20 EV DC_ChargeParameterDiscovery emits a DC request built from the DC params on enter") {
    const ev::feedback::Callbacks callbacks{};
    auto state_helper = FsmStateHelper(callbacks);
    state_helper.set_dc_params(make_params());
    auto& ctx = state_helper.get_context();
    ctx.get_session().set_id(SESSION_HEADER.session_id);

    fsm::v2::FSM<ev::d20::StateBase> fsm{ctx.create_state<ev::d20::state::DC_ChargeParameterDiscovery>()};

    const auto requests = drain_requests(state_helper.get_message_exchange());
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
    auto state_helper = FsmStateHelper(callbacks);
    auto& ctx = state_helper.get_context();
    ctx.get_session().set_id(SESSION_HEADER.session_id);

    fsm::v2::FSM<ev::d20::StateBase> fsm{ctx.create_state<ev::d20::state::DC_ChargeParameterDiscovery>()};

    state_helper.handle_response(make_response(message_20::datatypes::ResponseCode::OK));

    const auto result = fsm.feed(ev::d20::Event::V2GTP_MESSAGE);

    REQUIRE(result.transitioned() == true);
    REQUIRE(fsm.get_current_state_id() == ev::d20::StateID::ScheduleExchange);
    REQUIRE(ctx.is_session_stopped() == false);
}

SCENARIO("ISO15118-20 EV DC_ChargeParameterDiscovery stops session on FAILED response") {
    const ev::feedback::Callbacks callbacks{};
    auto state_helper = FsmStateHelper(callbacks);
    auto& ctx = state_helper.get_context();
    ctx.get_session().set_id(SESSION_HEADER.session_id);

    fsm::v2::FSM<ev::d20::StateBase> fsm{ctx.create_state<ev::d20::state::DC_ChargeParameterDiscovery>()};

    state_helper.handle_response(make_response(message_20::datatypes::ResponseCode::FAILED));

    const auto result = fsm.feed(ev::d20::Event::V2GTP_MESSAGE);

    REQUIRE(result.transitioned() == false);
    REQUIRE(fsm.get_current_state_id() == ev::d20::StateID::DC_ChargeParameterDiscovery);
    REQUIRE(ctx.is_session_stopped() == true);
}

SCENARIO("ISO15118-20 EV DC_ChargeParameterDiscovery stops session on wrong variant") {
    const ev::feedback::Callbacks callbacks{};
    auto state_helper = FsmStateHelper(callbacks);
    auto& ctx = state_helper.get_context();
    ctx.get_session().set_id(SESSION_HEADER.session_id);

    fsm::v2::FSM<ev::d20::StateBase> fsm{ctx.create_state<ev::d20::state::DC_ChargeParameterDiscovery>()};

    state_helper.handle_response(message_20::ScheduleExchangeResponse{});

    const auto result = fsm.feed(ev::d20::Event::V2GTP_MESSAGE);

    REQUIRE(result.transitioned() == false);
    REQUIRE(fsm.get_current_state_id() == ev::d20::StateID::DC_ChargeParameterDiscovery);
    REQUIRE(ctx.is_session_stopped() == true);
}

SCENARIO("ISO15118-20 EV DC_ChargeParameterDiscovery stops session on mismatched response session_id") {
    const ev::feedback::Callbacks callbacks{};
    auto state_helper = FsmStateHelper(callbacks);
    auto& ctx = state_helper.get_context();
    ctx.get_session().set_id(SESSION_HEADER.session_id);

    fsm::v2::FSM<ev::d20::StateBase> fsm{ctx.create_state<ev::d20::state::DC_ChargeParameterDiscovery>()};

    auto res = make_response(message_20::datatypes::ResponseCode::OK);
    res.header.session_id = message_20::datatypes::SessionId{0xDE, 0xAD, 0xBE, 0xEF, 0x00, 0x00, 0x00, 0x00};
    state_helper.handle_response(res);

    const auto result = fsm.feed(ev::d20::Event::V2GTP_MESSAGE);

    REQUIRE(result.transitioned() == false);
    REQUIRE(fsm.get_current_state_id() == ev::d20::StateID::DC_ChargeParameterDiscovery);
    REQUIRE(ctx.is_session_stopped() == true);
}
