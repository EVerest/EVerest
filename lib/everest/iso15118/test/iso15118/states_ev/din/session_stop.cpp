// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Pionix GmbH and Contributors to EVerest
#include <catch2/catch_test_macros.hpp>

#include <iso15118/detail/din/ev/state/session_stop.hpp>

using namespace iso15118;
namespace dt = message_din::datatypes;
using namespace din::ev::state::session_stop;

SCENARIO("EVCC DIN SessionStop request/response handling") {
    GIVEN("A request") {
        const auto req = create_request();
        THEN("The body is empty (only the header)") {
            REQUIRE(req.header.session_id == dt::SessionId{});
        }
    }

    GIVEN("An OK response") {
        message_din::SessionStopResponse res;
        res.response_code = dt::ResponseCode::OK;
        THEN("It is valid") {
            REQUIRE(handle_response(res).valid);
        }
    }

    GIVEN("A failed response") {
        message_din::SessionStopResponse res;
        res.response_code = dt::ResponseCode::FAILED;
        THEN("It is invalid") {
            REQUIRE(not handle_response(res).valid);
        }
    }
}
