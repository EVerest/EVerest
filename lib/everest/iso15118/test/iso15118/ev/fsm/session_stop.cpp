// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#include <catch2/catch_test_macros.hpp>

#include "helper.hpp"

#include <iso15118/ev/d20/state/session_stop.hpp>
#include <iso15118/message/authorization.hpp>
#include <iso15118/message/session_stop.hpp>
#include <iso15118/message/type.hpp>

using namespace iso15118;

SCENARIO("ISO15118-20 EV SessionStop sends Terminate request on enter") {
    const ev::feedback::Callbacks callbacks{};
    PrimedState<ev::d20::state::SessionStop> primed{callbacks, no_seed};

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
    PrimedState<ev::d20::state::SessionStop> primed{callbacks, no_seed};

    expect_stops_session(primed,
                         message_20::SessionStopResponse{SESSION_HEADER, message_20::datatypes::ResponseCode::OK},
                         ev::d20::StateID::SessionStop);
}

SCENARIO("ISO15118-20 EV SessionStop with non-OK response still stops session") {
    const ev::feedback::Callbacks callbacks{};
    PrimedState<ev::d20::state::SessionStop> primed{callbacks, no_seed};

    expect_stops_session(primed,
                         message_20::SessionStopResponse{SESSION_HEADER, message_20::datatypes::ResponseCode::FAILED},
                         ev::d20::StateID::SessionStop);
}

SCENARIO("ISO15118-20 EV SessionStop with wrong-variant response stops session") {
    const ev::feedback::Callbacks callbacks{};
    PrimedState<ev::d20::state::SessionStop> primed{callbacks, no_seed};

    const auto wrong = message_20::AuthorizationResponse{SESSION_HEADER, message_20::datatypes::ResponseCode::OK,
                                                         message_20::datatypes::Processing::Finished};
    expect_stops_session(primed, wrong, ev::d20::StateID::SessionStop);
}
