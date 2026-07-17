// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Pionix GmbH and Contributors to EVerest
#include <catch2/catch_test_macros.hpp>

#include <iso15118/detail/d2/ev/state/session_stop.hpp>

using namespace iso15118;
namespace dt = message_2::datatypes;

SCENARIO("EVCC ISO-2 SessionStop request/response handling") {

    GIVEN("A terminate request") {
        const auto req = d2::ev::state::session_stop::create_request(dt::ChargingSession::Terminate);
        THEN("ChargingSession is Terminate") {
            REQUIRE(req.charging_session == dt::ChargingSession::Terminate);
        }
    }

    GIVEN("A pause request") {
        const auto req = d2::ev::state::session_stop::create_request(dt::ChargingSession::Pause);
        THEN("ChargingSession is Pause") {
            REQUIRE(req.charging_session == dt::ChargingSession::Pause);
        }
    }

    GIVEN("An OK response") {
        message_2::SessionStopResponse res;
        res.response_code = dt::ResponseCode::OK;
        THEN("It is valid") {
            REQUIRE(d2::ev::state::session_stop::handle_response(res).valid);
        }
    }

    GIVEN("A failed response") {
        message_2::SessionStopResponse res;
        res.response_code = dt::ResponseCode::FAILED;
        THEN("It is invalid") {
            REQUIRE(not d2::ev::state::session_stop::handle_response(res).valid);
        }
    }
}
