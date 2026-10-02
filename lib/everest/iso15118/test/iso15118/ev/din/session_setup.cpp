// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#include <catch2/catch_test_macros.hpp>

#include "helper.hpp"

#include <iso15118/ev/din/state/session_setup.hpp>
#include <iso15118/message_din/service_discovery.hpp>
#include <iso15118/message_din/session_setup.hpp>

using namespace iso15118;

namespace {
namespace dt = message_din::datatypes;
using ev::din::StateID;
using State = ev::din::state::SessionSetup;

message_din::SessionSetupResponse make_response(dt::ResponseCode code) {
    message_din::SessionSetupResponse res;
    res.header = header();
    res.response_code = code;
    res.evse_id = dt::EvseId{0x2A, 0x2B};
    return res;
}
} // namespace

SCENARIO("DIN 70121 EV SessionSetup advances to ServiceDiscovery on a new session") {
    const ev::feedback::Callbacks callbacks{};
    PrimedState<State> primed{callbacks, no_seed};
    primed.take_requests();

    primed.handle_response(make_response(dt::ResponseCode::OK_NewSessionEstablished));
    const auto result = primed.feed(ev::din::Event::V2GTP_MESSAGE);

    REQUIRE(result.transitioned() == true);
    REQUIRE(primed.fsm.get_current_state_id() == StateID::ServiceDiscovery);
    REQUIRE(primed.ctx.is_session_stopped() == false);
    REQUIRE(primed.take_requests().get<message_din::ServiceDiscoveryRequest>().has_value());
}

SCENARIO("DIN 70121 EV SessionSetup stops when the EVSE joins an old session unasked") {
    const ev::feedback::Callbacks callbacks{};
    PrimedState<State> primed{callbacks, no_seed};
    primed.take_requests();

    // Nothing was resumed, so there is no old session for the EVSE to have joined and the id it
    // answers with is not one this EV asked for.
    primed.handle_response(make_response(dt::ResponseCode::OK_OldSessionJoined));
    primed.feed(ev::din::Event::V2GTP_MESSAGE);

    REQUIRE(primed.fsm.get_current_state_id() == StateID::SessionSetup);
    REQUIRE(primed.ctx.is_session_stopped() == true);
}

SCENARIO("DIN 70121 EV SessionSetup accepts an old session it asked to re-join") {
    const ev::feedback::Callbacks callbacks{};
    DinStateHelper helper{ev::feedback::Callbacks{callbacks}, ev::EvSessionParams{}, false, SESSION_ID};
    auto& ctx = helper.get_context();
    auto fsm = fsm::v2::FSM<ev::din::StateBase>{ctx.create_state<State>()};
    take_all_requests(helper.get_message_exchange());

    helper.handle_response(make_response(dt::ResponseCode::OK_OldSessionJoined));
    const auto result = fsm.feed(ev::din::Event::V2GTP_MESSAGE);

    REQUIRE(result.transitioned() == true);
    REQUIRE(fsm.get_current_state_id() == StateID::ServiceDiscovery);
    REQUIRE(ctx.is_session_stopped() == false);
}

SCENARIO("DIN 70121 EV SessionSetup stops on a response code SessionSetupRes never carries") {
    const ev::feedback::Callbacks callbacks{};
    PrimedState<State> primed{callbacks, no_seed};
    primed.take_requests();

    // SessionSetupRes is defined to answer with OK_NewSessionEstablished or OK_OldSessionJoined
    // only. A bare OK alongside a fresh session id must not read as a re-join.
    expect_stops_session(primed, make_response(dt::ResponseCode::OK), StateID::SessionSetup);
}
