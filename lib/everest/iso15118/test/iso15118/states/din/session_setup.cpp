// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Pionix GmbH and Contributors to EVerest
#include <catch2/catch_test_macros.hpp>

#include <iso15118/detail/din/state/session_setup.hpp>

using namespace iso15118;
namespace dt = message_din::datatypes;

SCENARIO("DIN SECC SessionSetup state handling") {
    const dt::SessionId assigned{0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08};
    const std::vector<uint8_t> evse_id{'D', 'E', '1'};

    GIVEN("A new session") {
        message_din::SessionSetupRequest req;
        req.evcc_id = {0x02, 0x00, 0x00, 0x00, 0x00, 0x01};

        const auto res = din::state::handle_request(req, assigned, evse_id, true);

        THEN("ResponseCode is OK_NewSessionEstablished and the EVSEID is echoed") {
            REQUIRE(res.response_code == dt::ResponseCode::OK_NewSessionEstablished);
            REQUIRE(res.evse_id == evse_id);
            REQUIRE(res.header.session_id == assigned);
        }
    }

    GIVEN("A re-joined session") {
        message_din::SessionSetupRequest req;
        req.header.session_id = assigned;

        const auto res = din::state::handle_request(req, assigned, evse_id, false);

        THEN("ResponseCode is OK_OldSessionJoined") {
            REQUIRE(res.response_code == dt::ResponseCode::OK_OldSessionJoined);
        }
    }
}
