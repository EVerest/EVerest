// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#include <catch2/catch_test_macros.hpp>

#include "helper.hpp"

#include <iso15118/ev/d20/state/dc_cable_check.hpp>
#include <iso15118/ev/d20/state/dc_pre_charge.hpp>
#include <iso15118/message/authorization.hpp>
#include <iso15118/message/dc_cable_check.hpp>
#include <iso15118/message/type.hpp>

using namespace iso15118;

namespace {
using message_20::datatypes::Processing;
using message_20::datatypes::ResponseCode;

message_20::DC_CableCheckResponse make_response(const message_20::Header& header, ResponseCode code,
                                                Processing processing) {
    return message_20::DC_CableCheckResponse{header, code, processing};
}
} // namespace

SCENARIO("ISO15118-20 EV DC_CableCheck sends initial request on enter") {
    const ev::feedback::Callbacks callbacks{};
    PrimedState<ev::d20::state::DC_CableCheck> primed{callbacks, no_seed};

    const auto requests = primed.take_requests();
    const auto request_message = requests.get<message_20::DC_CableCheckRequest>();
    REQUIRE(request_message.has_value());
    REQUIRE(request_message->header.session_id == SESSION_HEADER.session_id);
}

SCENARIO("ISO15118-20 EV DC_CableCheck stays and resends on Ongoing") {
    const ev::feedback::Callbacks callbacks{};
    PrimedState<ev::d20::state::DC_CableCheck> primed{callbacks, no_seed};

    // Consume the request emitted on enter() the way the reactor transmits it before the
    // response arrives, so the post-feed assertion proves feed() emitted a *fresh*
    // DC_CableCheckRequest rather than observing the initial one.
    REQUIRE(primed.helper.get_message_exchange().take_request().has_value());
    REQUIRE_FALSE(primed.helper.get_message_exchange().has_request());

    primed.handle_response(make_response(SESSION_HEADER, ResponseCode::OK, Processing::Ongoing));
    const auto result = primed.feed(ev::d20::Event::V2GTP_MESSAGE);

    REQUIRE(result.transitioned() == false);
    REQUIRE(primed.fsm.get_current_state_id() == ev::d20::StateID::DC_CableCheck);
    REQUIRE(primed.ctx.is_session_stopped() == false);

    const auto requests = primed.take_requests();
    const auto request_message = requests.get<message_20::DC_CableCheckRequest>();
    REQUIRE(request_message.has_value());
    REQUIRE(request_message->header.session_id == SESSION_HEADER.session_id);
}

SCENARIO("ISO15118-20 EV DC_CableCheck transitions to DC_PreCharge on Finished") {
    const ev::feedback::Callbacks callbacks{};
    PrimedState<ev::d20::state::DC_CableCheck> primed{callbacks, no_seed};

    primed.handle_response(make_response(SESSION_HEADER, ResponseCode::OK, Processing::Finished));
    const auto result = primed.feed(ev::d20::Event::V2GTP_MESSAGE);

    REQUIRE(result.transitioned() == true);
    REQUIRE(primed.fsm.get_current_state_id() == ev::d20::StateID::DC_PreCharge);
    REQUIRE(primed.ctx.is_session_stopped() == false);
}

SCENARIO("ISO15118-20 EV DC_CableCheck stops session on FAILED_UnknownSession") {
    // State-specific rejection beyond the shared triple.
    const ev::feedback::Callbacks callbacks{};
    PrimedState<ev::d20::state::DC_CableCheck> primed{callbacks, no_seed};

    primed.handle_response(make_response(SESSION_HEADER, ResponseCode::FAILED_UnknownSession, Processing::Finished));
    const auto result = primed.feed(ev::d20::Event::V2GTP_MESSAGE);

    REQUIRE(result.transitioned() == false);
    REQUIRE(primed.fsm.get_current_state_id() == ev::d20::StateID::DC_CableCheck);
    REQUIRE(primed.ctx.is_session_stopped() == true);
}

SCENARIO("ISO15118-20 EV DC_CableCheck rejects malformed responses") {
    const ev::feedback::Callbacks callbacks{};
    const auto make_fsm = [](FsmStateHelper& helper) {
        auto& ctx = helper.get_context();
        ctx.get_session().set_id(SESSION_HEADER.session_id);
        return fsm::v2::FSM<ev::d20::StateBase>{ctx.create_state<ev::d20::state::DC_CableCheck>()};
    };
    const auto make_ok = [](const message_20::Header& header) {
        return make_response(header, ResponseCode::OK, Processing::Finished);
    };
    const auto wrong = message_20::AuthorizationResponse{SESSION_HEADER, ResponseCode::OK, Processing::Finished};
    check_rejection_paths(callbacks, ev::d20::StateID::DC_CableCheck, make_fsm, make_ok, wrong);
}
