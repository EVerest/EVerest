// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Pionix GmbH and Contributors to EVerest
#include <catch2/catch_test_macros.hpp>

#include <iso15118/detail/d2/state/session_stop.hpp>

using namespace iso15118;
namespace dt = message_2::datatypes;

SCENARIO("ISO 15118-2 SECC SessionStop handling") {
    const dt::SessionId id{0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08};

    GIVEN("A terminate request") {
        message_2::SessionStopRequest req;
        req.charging_session = dt::ChargingSession::Terminate;
        const auto res = d2::state::handle_request(req, id);
        THEN("OK") {
            REQUIRE(res.response_code == dt::ResponseCode::OK);
            REQUIRE(res.header.session_id == id);
        }
    }

    GIVEN("A pause request") {
        message_2::SessionStopRequest req;
        req.charging_session = dt::ChargingSession::Pause;
        const auto res = d2::state::handle_request(req, id);
        THEN("OK") {
            REQUIRE(res.response_code == dt::ResponseCode::OK);
        }
    }
}
