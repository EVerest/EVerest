// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include "helper.hpp"

#include <iso15118/ev/d20/state/session_stop.hpp>
#include <iso15118/message/authorization.hpp>
#include <iso15118/message/session_stop.hpp>
#include <iso15118/message/type.hpp>

using namespace iso15118;

namespace {
// SessionStop as its predecessors enter it, through to_session_stop: the request is pending.
auto stopping_for(ev::session::StopReason reason) {
    return entered_by(
        [reason](FsmStateHelper& helper) { return ev::d20::state::to_session_stop(helper.get_context(), reason); });
}
} // namespace

SCENARIO("ISO15118-20 EV SessionStop is entered with a Terminate request") {
    const ev::feedback::Callbacks callbacks{};
    PrimedState<ev::d20::state::SessionStop> primed{callbacks, no_seed, stopping_for(ev::session::StopReason::EvStop)};

    const auto requests = primed.take_requests();
    const auto request_message = requests.get<message_20::SessionStopRequest>();
    REQUIRE(request_message.has_value());
    REQUIRE(request_message->header.session_id == SESSION_HEADER.session_id);
    REQUIRE(request_message->charging_session == message_20::datatypes::ChargingSession::Terminate);
    REQUIRE_FALSE(request_message->ev_termination_code.has_value());
    REQUIRE_FALSE(request_message->ev_termination_explanation.has_value());
}

SCENARIO("ISO15118-20 EV SessionStop stops session on OK response") {
    const ev::feedback::Callbacks callbacks{};
    PrimedState<ev::d20::state::SessionStop> primed{callbacks, no_seed, stopping_for(ev::session::StopReason::EvStop)};

    expect_stops_session(primed,
                         message_20::SessionStopResponse{SESSION_HEADER, message_20::datatypes::ResponseCode::OK},
                         ev::d20::StateID::SessionStop);
}

SCENARIO("ISO15118-20 EV SessionStop with non-OK response still stops session") {
    const ev::feedback::Callbacks callbacks{};
    PrimedState<ev::d20::state::SessionStop> primed{callbacks, no_seed, stopping_for(ev::session::StopReason::EvStop)};

    expect_stops_session(primed,
                         message_20::SessionStopResponse{SESSION_HEADER, message_20::datatypes::ResponseCode::FAILED},
                         ev::d20::StateID::SessionStop);
}

SCENARIO("ISO15118-20 EV SessionStop with wrong-variant response stops session") {
    const ev::feedback::Callbacks callbacks{};
    PrimedState<ev::d20::state::SessionStop> primed{callbacks, no_seed, stopping_for(ev::session::StopReason::EvStop)};

    const auto wrong = message_20::AuthorizationResponse{SESSION_HEADER, message_20::datatypes::ResponseCode::OK,
                                                         message_20::datatypes::Processing::Finished};
    expect_stops_session(primed, wrong, ev::d20::StateID::SessionStop);
}

SCENARIO("ISO15118-20 EV SessionStop sends Pause for either side's pause") {
    const auto reason = GENERATE(ev::session::StopReason::EvPause, ev::session::StopReason::EvsePause);
    const ev::feedback::Callbacks callbacks{};
    PrimedState<ev::d20::state::SessionStop> primed{callbacks, no_seed, stopping_for(reason)};

    const auto requests = primed.take_requests();
    const auto request_message = requests.get<message_20::SessionStopRequest>();
    REQUIRE(request_message.has_value());
    REQUIRE(request_message->charging_session == message_20::datatypes::ChargingSession::Pause);
}

SCENARIO("ISO15118-20 EV SessionStop sends Terminate for either side's stop") {
    const auto reason = GENERATE(ev::session::StopReason::EvStop, ev::session::StopReason::EvseTerminate);
    const ev::feedback::Callbacks callbacks{};
    PrimedState<ev::d20::state::SessionStop> primed{callbacks, no_seed, stopping_for(reason)};

    const auto requests = primed.take_requests();
    const auto request_message = requests.get<message_20::SessionStopRequest>();
    REQUIRE(request_message.has_value());
    REQUIRE(request_message->charging_session == message_20::datatypes::ChargingSession::Terminate);
}

SCENARIO("ISO15118-20 EV SessionStop pauses the session on an OK response to a Pause") {
    const ev::feedback::Callbacks callbacks{};
    PrimedState<ev::d20::state::SessionStop> primed{callbacks, no_seed, stopping_for(ev::session::StopReason::EvPause)};

    expect_stops_session(primed,
                         message_20::SessionStopResponse{SESSION_HEADER, message_20::datatypes::ResponseCode::OK},
                         ev::d20::StateID::SessionStop);
    REQUIRE(primed.ctx.is_session_paused() == true);
}

SCENARIO("ISO15118-20 EV SessionStop does not pause on a rejected Pause") {
    const ev::feedback::Callbacks callbacks{};
    PrimedState<ev::d20::state::SessionStop> primed{callbacks, no_seed, stopping_for(ev::session::StopReason::EvPause)};

    expect_stops_session(
        primed,
        message_20::SessionStopResponse{SESSION_HEADER, message_20::datatypes::ResponseCode::FAILED_PauseNotAllowed},
        ev::d20::StateID::SessionStop);
    REQUIRE(primed.ctx.is_session_paused() == false);
}

SCENARIO("ISO15118-20 EV SessionStop answers for the reason it sent, not a latch set afterwards") {
    const ev::feedback::Callbacks callbacks{};

    GIVEN("A Pause on the wire and an EV stop latched after it") {
        PrimedState<ev::d20::state::SessionStop> primed{callbacks, no_seed,
                                                        stopping_for(ev::session::StopReason::EvPause)};
        primed.ctx.set_stop_charging_requested(true);

        expect_stops_session(primed,
                             message_20::SessionStopResponse{SESSION_HEADER, message_20::datatypes::ResponseCode::OK},
                             ev::d20::StateID::SessionStop);
        REQUIRE(primed.ctx.is_session_paused() == true);
    }
    GIVEN("A Terminate on the wire and an EV pause latched after it") {
        PrimedState<ev::d20::state::SessionStop> primed{callbacks, no_seed,
                                                        stopping_for(ev::session::StopReason::EvseTerminate)};
        primed.ctx.set_pause_charging_requested(true);

        expect_stops_session(primed,
                             message_20::SessionStopResponse{SESSION_HEADER, message_20::datatypes::ResponseCode::OK},
                             ev::d20::StateID::SessionStop);
        REQUIRE(primed.ctx.is_session_paused() == false);
    }
}

SCENARIO("ISO15118-20 EV SessionStop raises a carried pause to an EV stop latched before it is entered") {
    const ev::feedback::Callbacks callbacks{};
    const auto stop_latched = [](FsmStateHelper& helper) { helper.get_context().set_stop_charging_requested(true); };
    PrimedState<ev::d20::state::SessionStop> primed{callbacks, stop_latched,
                                                    stopping_for(ev::session::StopReason::EvPause)};

    const auto request_message = primed.take_requests().get<message_20::SessionStopRequest>();
    REQUIRE(request_message.has_value());
    REQUIRE(request_message->charging_session == message_20::datatypes::ChargingSession::Terminate);

    expect_stops_session(primed,
                         message_20::SessionStopResponse{SESSION_HEADER, message_20::datatypes::ResponseCode::OK},
                         ev::d20::StateID::SessionStop);
    REQUIRE(primed.ctx.is_session_paused() == false);
}
