// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Pionix GmbH and Contributors to EVerest
#include <catch2/catch_test_macros.hpp>

#include <iso15118/detail/d2/ev/state/session_setup.hpp>

using namespace iso15118;
namespace dt = message_2::datatypes;

SCENARIO("EVCC ISO-2 SessionSetup request/response handling") {

    GIVEN("An EV building the initial request") {
        const std::array<uint8_t, 6> mac{0x00, 0x7d, 0xfa, 0x01, 0x02, 0x03};
        const auto req = d2::ev::state::session_setup::create_request(mac);

        THEN("The EVCCID is the MAC and the session id defaults to all zero") {
            REQUIRE(req.evcc_id == mac);
            REQUIRE(req.header.session_id == dt::SessionId{0, 0, 0, 0, 0, 0, 0, 0});
        }
    }

    GIVEN("A new-session response") {
        const auto assigned_id = dt::SessionId{0x10, 0x34, 0xAB, 0x7A, 0x01, 0xF3, 0x95, 0x02};
        message_2::SessionSetupResponse res;
        res.response_code = dt::ResponseCode::OK_NewSessionEstablished;
        res.header.session_id = assigned_id;
        res.evse_id = "everest se";

        const auto result = d2::ev::state::session_setup::handle_response(res);
        THEN("The assigned session id and evseid are captured, not flagged as old session") {
            REQUIRE(result.valid);
            REQUIRE(not result.old_session_joined);
            REQUIRE(result.session_id == assigned_id);
            REQUIRE(result.evse_id == "everest se");
        }
    }

    GIVEN("An OK_OldSessionJoined response (resume)") {
        message_2::SessionSetupResponse res;
        res.response_code = dt::ResponseCode::OK_OldSessionJoined;
        res.evse_id = "everest se";

        const auto result = d2::ev::state::session_setup::handle_response(res);
        THEN("It is accepted and flagged as old session joined") {
            REQUIRE(result.valid);
            REQUIRE(result.old_session_joined);
        }
    }

    GIVEN("A failed response") {
        message_2::SessionSetupResponse res;
        res.response_code = dt::ResponseCode::FAILED_UnknownSession;

        const auto result = d2::ev::state::session_setup::handle_response(res);
        THEN("The result is invalid") {
            REQUIRE(not result.valid);
        }
    }
}
