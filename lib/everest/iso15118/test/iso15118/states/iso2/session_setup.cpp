// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Pionix GmbH and Contributors to EVerest
#include <catch2/catch_test_macros.hpp>

#include <iso15118/detail/d2/state/session_setup.hpp>

using namespace iso15118;
namespace dt = message_2::datatypes;

SCENARIO("ISO 15118-2 SECC SessionSetup handling") {
    const dt::SessionId id{0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08};
    message_2::SessionSetupRequest req;

    GIVEN("A new session") {
        const auto res = d2::state::handle_request(req, id, "DE*PNX*E12345*1", true);
        THEN("OK_NewSessionEstablished, evse id and session id are set") {
            REQUIRE(res.response_code == dt::ResponseCode::OK_NewSessionEstablished);
            REQUIRE(res.evse_id == "DE*PNX*E12345*1");
            REQUIRE(res.header.session_id == id);
        }
    }

    GIVEN("A resumed (old) session") {
        const auto res = d2::state::handle_request(req, id, "DE*PNX*E12345*1", false);
        THEN("OK_OldSessionJoined") {
            REQUIRE(res.response_code == dt::ResponseCode::OK_OldSessionJoined);
            REQUIRE(res.header.session_id == id);
        }
    }
}
