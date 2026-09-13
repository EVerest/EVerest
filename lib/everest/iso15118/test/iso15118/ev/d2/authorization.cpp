// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#include <catch2/catch_test_macros.hpp>

#include "helper.hpp"

#include <iso15118/ev/d2/state/authorization.hpp>
#include <iso15118/message_2/authorization.hpp>
#include <iso15118/message_2/session_stop.hpp>

using namespace iso15118;
namespace dt = message_2::datatypes;

namespace {

constexpr dt::GenChallenge CHALLENGE{16, 15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1};

message_2::AuthorizationResponse make_response(const message_2::Header& header, dt::ResponseCode code,
                                               dt::EVSEProcessing processing) {
    message_2::AuthorizationResponse res;
    res.header = header;
    res.response_code = code;
    res.evse_processing = processing;
    return res;
}

} // namespace

SCENARIO("ISO15118-2 EV Authorization sends an empty EIM request on enter") {
    const ev::feedback::Callbacks callbacks{};
    PrimedState<ev::d2::state::Authorization> primed{callbacks, d2_no_seed};

    const auto request = primed.take_requests().get<message_2::AuthorizationRequest>();
    REQUIRE(request.has_value());
    REQUIRE(request->header.session_id == D2_SESSION_ID);
    REQUIRE_FALSE(request->id.has_value());
    REQUIRE_FALSE(request->gen_challenge.has_value());
}

SCENARIO("ISO15118-2 EV Authorization resends on Ongoing") {
    const ev::feedback::Callbacks callbacks{};
    PrimedState<ev::d2::state::Authorization> primed{callbacks, d2_no_seed};

    primed.handle_response(make_response(d2_header(), dt::ResponseCode::OK, dt::EVSEProcessing::Ongoing));
    const auto result = primed.feed(ev::d2::Event::V2GTP_MESSAGE);

    REQUIRE(result.output == ev::d2::Disposition::Awaiting);
    REQUIRE(primed.fsm.get_current_state_id() == ev::d2::StateID::Authorization);
    REQUIRE(primed.take_requests().get<message_2::AuthorizationRequest>().has_value());
}

SCENARIO("ISO15118-2 EV Authorization leaves an Ongoing poll on a latched pause request") {
    const ev::feedback::Callbacks callbacks{};
    PrimedState<ev::d2::state::Authorization> primed{callbacks, d2_no_seed};

    primed.ctx.set_pause_charging_requested(true);
    primed.handle_response(make_response(d2_header(), dt::ResponseCode::OK, dt::EVSEProcessing::Ongoing));
    REQUIRE(primed.feed(ev::d2::Event::V2GTP_MESSAGE).transitioned() == true);

    REQUIRE(primed.fsm.get_current_state_id() == ev::d2::StateID::SessionStop);
}

SCENARIO("ISO15118-2 EV Authorization transitions to ChargeParameterDiscovery on Finished") {
    const ev::feedback::Callbacks callbacks{};
    PrimedState<ev::d2::state::Authorization> primed{callbacks, d2_no_seed};

    primed.handle_response(make_response(d2_header(), dt::ResponseCode::OK, dt::EVSEProcessing::Finished));
    REQUIRE(primed.feed(ev::d2::Event::V2GTP_MESSAGE).transitioned() == true);
    REQUIRE(primed.fsm.get_current_state_id() == ev::d2::StateID::ChargeParameterDiscovery);
}

SCENARIO("ISO15118-2 EV Authorization diverts to SessionStop on a latched pause") {
    const ev::feedback::Callbacks callbacks{};
    PrimedState<ev::d2::state::Authorization> primed{callbacks, d2_no_seed};

    primed.ctx.set_pause_charging_requested(true);
    primed.handle_response(make_response(d2_header(), dt::ResponseCode::OK, dt::EVSEProcessing::Finished));
    REQUIRE(primed.feed(ev::d2::Event::V2GTP_MESSAGE).transitioned() == true);
    REQUIRE(primed.fsm.get_current_state_id() == ev::d2::StateID::SessionStop);
    const auto stop = primed.take_requests().get<message_2::SessionStopRequest>();
    REQUIRE(stop.has_value());
    REQUIRE(stop->charging_session == dt::ChargingSession::Pause);
}

SCENARIO("ISO15118-2 EV Authorization signs the request on a Contract session") {
    const ev::feedback::Callbacks callbacks{};
    const auto seed_contract = [](D2StateHelper& helper) {
        auto& pnc = helper.get_context().pnc;
        pnc.contract_selected = true;
        pnc.gen_challenge = CHALLENGE;
        pnc.contract_key_pem = make_test_ec_key_pem();
    };
    PrimedState<ev::d2::state::Authorization> primed{callbacks, seed_contract};

    const auto request = primed.take_requests().get<message_2::AuthorizationRequest>();
    REQUIRE(request.has_value());
    REQUIRE(request->header.session_id == D2_SESSION_ID);
    REQUIRE(request->id == "id1");
    REQUIRE(request->gen_challenge.has_value());
    REQUIRE(request->gen_challenge.value() == CHALLENGE);
}

SCENARIO("ISO15118-2 EV Authorization stops the session when the contract key is unusable") {
    const ev::feedback::Callbacks callbacks{};
    const auto seed_bad_key = [](D2StateHelper& helper) {
        auto& pnc = helper.get_context().pnc;
        pnc.contract_selected = true;
        pnc.contract_key_pem = "not a key";
    };
    PrimedState<ev::d2::state::Authorization> primed{callbacks, seed_bad_key};

    REQUIRE(primed.take_requests().empty());
    REQUIRE(primed.ctx.is_session_stopped() == true);
}

SCENARIO("ISO15118-2 EV Authorization rejects malformed responses") {
    const ev::feedback::Callbacks callbacks{};
    const auto make_fsm = [](D2StateHelper& helper) {
        auto& ctx = helper.get_context();
        ctx.set_session_id(D2_SESSION_ID);
        return fsm::v2::FSM<ev::d2::StateBase>{ctx.create_state<ev::d2::state::Authorization>()};
    };
    const auto make_ok = [](const message_2::Header& header) {
        return make_response(header, dt::ResponseCode::OK, dt::EVSEProcessing::Finished);
    };
    const message_2::SessionStopResponse wrong{d2_header(), dt::ResponseCode::OK};
    check_rejection_paths(callbacks, ev::d2::StateID::Authorization, make_fsm, make_ok, wrong);
}
