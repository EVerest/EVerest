// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include "helper.hpp"

#include <iso15118/ev/d20/state/authorization.hpp>
#include <iso15118/message/authorization.hpp>
#include <iso15118/message/service_discovery.hpp>
#include <iso15118/message/session_stop.hpp>
#include <iso15118/message/type.hpp>

using namespace iso15118;

namespace {
using message_20::datatypes::Processing;
using message_20::datatypes::ResponseCode;

message_20::AuthorizationResponse make_auth_res(const message_20::Header& header, ResponseCode code,
                                                Processing processing) {
    return message_20::AuthorizationResponse{header, code, processing};
}
} // namespace

SCENARIO("ISO15118-20 EV Authorization transitions to ServiceDiscovery on Finished") {
    const ev::feedback::Callbacks callbacks{};
    PrimedState<ev::d20::state::Authorization> primed{callbacks, no_seed};

    primed.handle_response(make_auth_res(SESSION_HEADER, ResponseCode::OK, Processing::Finished));
    const auto result = primed.feed(ev::d20::Event::V2GTP_MESSAGE);

    REQUIRE(result.transitioned() == true);
    REQUIRE(primed.fsm.get_current_state_id() == ev::d20::StateID::ServiceDiscovery);
    REQUIRE(primed.ctx.is_session_stopped() == false);

    const auto requests = primed.take_requests();
    const auto request_message = requests.get<message_20::ServiceDiscoveryRequest>();
    REQUIRE(request_message.has_value());
    REQUIRE(request_message->header.session_id == SESSION_HEADER.session_id);
}

SCENARIO("ISO15118-20 EV Authorization stays and resends on Ongoing") {
    const ev::feedback::Callbacks callbacks{};
    PrimedState<ev::d20::state::Authorization> primed{callbacks, no_seed};

    primed.handle_response(make_auth_res(SESSION_HEADER, ResponseCode::OK, Processing::Ongoing));
    const auto result = primed.feed(ev::d20::Event::V2GTP_MESSAGE);

    REQUIRE(result.transitioned() == false);
    REQUIRE(primed.fsm.get_current_state_id() == ev::d20::StateID::Authorization);
    REQUIRE(primed.ctx.is_session_stopped() == false);

    const auto requests = primed.take_requests();
    const auto request_message = requests.get<message_20::AuthorizationRequest>();
    REQUIRE(request_message.has_value());
    REQUIRE(request_message->header.session_id == SESSION_HEADER.session_id);
    REQUIRE(request_message->selected_authorization_service == message_20::datatypes::Authorization::EIM);
}

SCENARIO("ISO15118-20 EV Authorization stops the session on a FAILED response after an Ongoing one") {
    const ev::feedback::Callbacks callbacks{};
    PrimedState<ev::d20::state::Authorization> primed{callbacks, no_seed};

    // Poll once so the rejection below happens mid-authorization and not on the first response.
    primed.handle_response(make_auth_res(SESSION_HEADER, ResponseCode::OK, Processing::Ongoing));
    REQUIRE(primed.feed(ev::d20::Event::V2GTP_MESSAGE).transitioned() == false);
    REQUIRE(primed.ctx.is_session_stopped() == false);
    REQUIRE(primed.take_requests().get<message_20::AuthorizationRequest>().has_value());

    expect_stops_session(primed, make_auth_res(SESSION_HEADER, ResponseCode::FAILED_SequenceError, Processing::Ongoing),
                         ev::d20::StateID::Authorization);
    REQUIRE(primed.take_requests().empty());
}

SCENARIO("ISO15118-20 EV Authorization rejects malformed responses") {
    const ev::feedback::Callbacks callbacks{};
    const auto make_fsm = [](FsmStateHelper& helper) {
        auto& ctx = helper.get_context();
        ctx.get_session().set_id(SESSION_HEADER.session_id);
        return fsm::v2::FSM<ev::d20::StateBase>{ctx.create_state<ev::d20::state::Authorization>()};
    };
    const auto make_ok = [](const message_20::Header& header) {
        return make_auth_res(header, ResponseCode::OK, Processing::Finished);
    };
    check_rejection_paths(callbacks, ev::d20::StateID::Authorization, make_fsm, make_ok,
                          message_20::ServiceDiscoveryResponse{});
}

SCENARIO("ISO15118-20 EV Authorization retries a declined attempt instead of walking on") {
    const ev::feedback::Callbacks callbacks{};

    // Both codes the EVSE uses to decline while staying in Authorization. Neither ends the
    // session: the driver may authorize again at the EVSE, and the ongoing guard bounds the loop.
    const auto declined =
        GENERATE(ResponseCode::WARNING_EIMAuthorizationFailure, ResponseCode::WARNING_AuthorizationSelectionInvalid);

    PrimedState<ev::d20::state::Authorization> primed{callbacks, no_seed};

    primed.handle_response(make_auth_res(SESSION_HEADER, declined, Processing::Finished));
    const auto result = primed.feed(ev::d20::Event::V2GTP_MESSAGE);

    REQUIRE(result.transitioned() == false);
    REQUIRE(primed.fsm.get_current_state_id() == ev::d20::StateID::Authorization);
    REQUIRE(primed.ctx.is_session_stopped() == false);

    const auto requests = primed.take_requests();
    REQUIRE(requests.get<message_20::AuthorizationRequest>().has_value());
    REQUIRE_FALSE(requests.get<message_20::ServiceDiscoveryRequest>().has_value());
}

SCENARIO("ISO15118-20 EV Authorization terminates the session on a stop or pause requested before charging starts") {
    const ev::feedback::Callbacks callbacks{};
    PrimedState<ev::d20::state::Authorization> primed{callbacks, no_seed};

    const bool stop = GENERATE(true, false);
    if (stop) {
        primed.ctx.set_stop_charging_requested(true);
    } else {
        primed.ctx.set_pause_charging_requested(true);
    }

    primed.handle_response(make_auth_res(SESSION_HEADER, ResponseCode::OK, Processing::Finished));
    const auto result = primed.feed(ev::d20::Event::V2GTP_MESSAGE);

    REQUIRE(result.output == ev::d20::Disposition::Transitioning);
    REQUIRE(primed.fsm.get_current_state_id() == ev::d20::StateID::SessionStop);
    const auto requests = primed.take_requests();
    REQUIRE_FALSE(requests.get<message_20::ServiceDiscoveryRequest>().has_value());
    const auto session_stop = requests.get<message_20::SessionStopRequest>();
    REQUIRE(session_stop.has_value());
    // [V2G20-2644]: before PowerDeliveryReq(Start) a pause terminates too.
    REQUIRE(session_stop->charging_session == message_20::datatypes::ChargingSession::Terminate);

    primed.handle_response(message_20::SessionStopResponse{SESSION_HEADER, ResponseCode::OK});
    REQUIRE(primed.feed(ev::d20::Event::V2GTP_MESSAGE).output == ev::d20::Disposition::Stopping);
    REQUIRE(primed.ctx.is_session_stopped());
    REQUIRE_FALSE(primed.ctx.is_session_paused());
}
