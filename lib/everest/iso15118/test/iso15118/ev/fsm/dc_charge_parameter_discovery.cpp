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

// DC_ChargeParameterDiscovery builds its request from the EV's DC charge params.
const auto seed_params = [](FsmStateHelper& helper) {
    ev::DcChargeParams p{};
    p.max_charge_power = 11000.0f;
    p.max_charge_current = 200.0f;
    p.max_voltage = 500.0f;
    p.min_voltage = 150.0f;
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
