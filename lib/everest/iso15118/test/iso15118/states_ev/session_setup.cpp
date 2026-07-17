// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Pionix GmbH and Contributors to EVerest
#include <catch2/catch_test_macros.hpp>

#include <array>

#include <iso15118/detail/d20/ev/state/session_setup.hpp>

using namespace iso15118;
namespace dt = message_20::datatypes;

SCENARIO("EVCC SessionSetup request/response handling") {

    GIVEN("An EV building the initial request") {
        const auto req = d20::ev::state::session_setup::create_request("WMIV1234567890ABCDEX");

        THEN("The evccid is set and the session id is all zero") {
            REQUIRE(req.evccid == "WMIV1234567890ABCDEX");
            REQUIRE(req.header.session_id == dt::SessionId{0, 0, 0, 0, 0, 0, 0, 0});
        }
    }

    GIVEN("A new-session response") {
        const auto assigned_id = dt::SessionId{0x10, 0x34, 0xAB, 0x7A, 0x01, 0xF3, 0x95, 0x02};
        message_20::SessionSetupResponse res;
        res.response_code = dt::ResponseCode::OK_NewSessionEstablished;
        res.header.session_id = assigned_id;
        res.evseid = "everest se";

        const auto result = d20::ev::state::session_setup::handle_response(res);
        THEN("The assigned session id and evseid are captured") {
            REQUIRE(result.valid == true);
            REQUIRE(result.new_session == true);
            REQUIRE(result.session_id == assigned_id);
            REQUIRE(result.evse_id == "everest se");
        }
    }

    GIVEN("A failed response") {
        message_20::SessionSetupResponse res;
        res.response_code = dt::ResponseCode::FAILED_UnknownSession;

        const auto result = d20::ev::state::session_setup::handle_response(res);
        THEN("The result is invalid") {
            REQUIRE(result.valid == false);
        }
    }
}
