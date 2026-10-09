// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#include <catch2/catch_test_macros.hpp>

#include "helper.hpp"

#include <iso15118/ev/din/state/contract_authentication.hpp>
#include <iso15118/message_din/charge_parameter_discovery.hpp>
#include <iso15118/message_din/contract_authentication.hpp>
#include <iso15118/message_din/session_stop.hpp>

using namespace iso15118;

namespace {
namespace dt = message_din::datatypes;
using ev::din::StateID;
using State = ev::din::state::ContractAuthentication;

message_din::ContractAuthenticationResponse make_response(const message_din::Header& hdr, dt::ResponseCode code,
                                                          dt::EvseProcessing processing) {
    message_din::ContractAuthenticationResponse res;
    res.header = hdr;
    res.response_code = code;
    res.evse_processing = processing;
    return res;
}

message_din::ContractAuthenticationResponse ok_response(const message_din::Header& hdr) {
    return make_response(hdr, dt::ResponseCode::OK, dt::EvseProcessing::Finished);
}
} // namespace

SCENARIO("DIN 70121 EV ContractAuthentication sends the EIM request on enter") {
    const ev::feedback::Callbacks callbacks{};
    PrimedState<State> primed{callbacks, no_seed};

    const auto request = primed.take_requests().get<message_din::ContractAuthenticationRequest>();
    REQUIRE(request.has_value());
    REQUIRE_FALSE(request->id.has_value());
    REQUIRE_FALSE(request->gen_challenge.has_value());
}

SCENARIO("DIN 70121 EV ContractAuthentication stays and re-polls on Ongoing") {
    const ev::feedback::Callbacks callbacks{};
    PrimedState<State> primed{callbacks, no_seed};

    primed.handle_response(make_response(header(), dt::ResponseCode::OK, dt::EvseProcessing::Ongoing));
    const auto result = primed.feed(ev::din::Event::V2GTP_MESSAGE);

    REQUIRE(result.output == ev::din::Disposition::Awaiting);
    REQUIRE(primed.fsm.get_current_state_id() == StateID::ContractAuthentication);
    REQUIRE(primed.take_requests().get<message_din::ContractAuthenticationRequest>().has_value());
}

SCENARIO("DIN 70121 EV ContractAuthentication transitions to ChargeParameterDiscovery on Finished") {
    const ev::feedback::Callbacks callbacks{};
    PrimedState<State> primed{callbacks, no_seed};

    primed.handle_response(ok_response(header()));
    const auto result = primed.feed(ev::din::Event::V2GTP_MESSAGE);

    REQUIRE(result.transitioned() == true);
    REQUIRE(primed.fsm.get_current_state_id() == StateID::ChargeParameterDiscovery);
    REQUIRE(primed.take_requests().get<message_din::ChargeParameterDiscoveryRequest>().has_value());
}

SCENARIO("DIN 70121 EV ContractAuthentication diverts to SessionStop on a latched stop request") {
    const ev::feedback::Callbacks callbacks{};
    PrimedState<State> primed{callbacks, no_seed};

    primed.ctx.set_stop_charging_requested(true);
    primed.handle_response(make_response(header(), dt::ResponseCode::OK, dt::EvseProcessing::Ongoing));
    primed.feed(ev::din::Event::V2GTP_MESSAGE);

    REQUIRE(primed.fsm.get_current_state_id() == StateID::SessionStop);
    REQUIRE(primed.take_requests().get<message_din::SessionStopRequest>().has_value());
}

SCENARIO("DIN 70121 EV ContractAuthentication rejects malformed responses") {
    const ev::feedback::Callbacks callbacks{};
    const auto make_fsm = [](DinStateHelper& helper) {
        auto& ctx = helper.get_context();
        ctx.set_session_id(SESSION_ID);
        return fsm::v2::FSM<ev::din::StateBase>{ctx.create_state<State>()};
    };
    message_din::ChargeParameterDiscoveryResponse wrong;
    wrong.header = header();
    wrong.response_code = dt::ResponseCode::OK;
    check_rejection_paths(callbacks, StateID::ContractAuthentication, make_fsm, ok_response, wrong);
}
