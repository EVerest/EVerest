// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Pionix GmbH and Contributors to EVerest
#include <catch2/catch_test_macros.hpp>

#include <array>

#include <iso15118/detail/din/ev/state/session_setup.hpp>

using namespace iso15118;
namespace dt = message_din::datatypes;
using namespace din::ev::state::session_setup;

SCENARIO("EVCC DIN SessionSetup request/response handling") {
    const std::array<uint8_t, 6> mac{0x02, 0x11, 0x22, 0x33, 0x44, 0x55};

    GIVEN("A fresh request (all-zero session id)") {
        const auto req = create_request(mac, dt::SessionId{});
        THEN("The EVCCID carries the MAC and the session id is all zero") {
            REQUIRE(req.evcc_id == message_din::datatypes::EvccId(mac.begin(), mac.end()));
            REQUIRE(req.header.session_id == dt::SessionId{0, 0, 0, 0, 0, 0, 0, 0});
        }
    }

    GIVEN("A resumed request carrying a session id") {
        const auto resumed = dt::SessionId{0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08};
        const auto req = create_request(mac, resumed);
        THEN("The session id is carried in the header") {
            REQUIRE(req.header.session_id == resumed);
        }
    }

    GIVEN("A new-session response") {
        const auto assigned = dt::SessionId{0x10, 0x34, 0xAB, 0x7A, 0x01, 0xF3, 0x95, 0x02};
        message_din::SessionSetupResponse res;
        res.response_code = dt::ResponseCode::OK_NewSessionEstablished;
        res.header.session_id = assigned;
        res.evse_id = {0xAB, 0xCD};

        const auto result = handle_response(res);
        THEN("It is valid, a new session, and captures id + evse id") {
            REQUIRE(result.valid);
            REQUIRE(result.new_session);
            REQUIRE(result.session_id == assigned);
            REQUIRE(result.evse_id == "ABCD");
        }
    }

    GIVEN("An OldSessionJoined response (pause re-join)") {
        message_din::SessionSetupResponse res;
        res.response_code = dt::ResponseCode::OK_OldSessionJoined;
        const auto result = handle_response(res);
        THEN("It is accepted but is not a new session") {
            REQUIRE(result.valid);
            REQUIRE(not result.new_session);
        }
    }

    GIVEN("A failed response") {
        message_din::SessionSetupResponse res;
        res.response_code = dt::ResponseCode::FAILED_UnknownSession;
        const auto result = handle_response(res);
        THEN("It is invalid") {
            REQUIRE(not result.valid);
        }
    }
}
