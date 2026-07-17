// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Pionix GmbH and Contributors to EVerest
#include <catch2/catch_test_macros.hpp>

#include <iso15118/detail/d20/ev/state/session_stop.hpp>

using namespace iso15118;
namespace dt = message_20::datatypes;

SCENARIO("EVCC SessionStop request/response handling") {

    GIVEN("A terminate request") {
        const auto req = d20::ev::state::session_stop::create_request(dt::ChargingSession::Terminate);
        THEN("The charging session is Terminate") {
            REQUIRE(req.charging_session == dt::ChargingSession::Terminate);
        }
    }

    GIVEN("A pause request") {
        const auto req = d20::ev::state::session_stop::create_request(dt::ChargingSession::Pause);
        THEN("The charging session is Pause") {
            REQUIRE(req.charging_session == dt::ChargingSession::Pause);
        }
    }

    GIVEN("An OK response") {
        message_20::SessionStopResponse res;
        res.response_code = dt::ResponseCode::OK;
        const auto result = d20::ev::state::session_stop::handle_response(res);
        THEN("The result is valid") {
            REQUIRE(result.valid == true);
        }
    }
}
