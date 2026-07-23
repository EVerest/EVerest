// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Pionix GmbH and Contributors to EVerest
#include <catch2/catch_test_macros.hpp>

#include <iso15118/detail/din/state/session_stop.hpp>

using namespace iso15118;
namespace dt = message_din::datatypes;

SCENARIO("DIN SECC SessionStop state handling") {
    const dt::SessionId session{0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08};

    GIVEN("A matching session id") {
        message_din::SessionStopRequest req;
        req.header.session_id = session;

        const auto res = din::state::handle_request(req, session);
        THEN("ResponseCode is OK") {
            REQUIRE(res.response_code == dt::ResponseCode::OK);
        }
    }

    GIVEN("A mismatching session id") {
        message_din::SessionStopRequest req;
        req.header.session_id = dt::SessionId{};

        const auto res = din::state::handle_request(req, session);
        THEN("ResponseCode is FAILED_UnknownSession") {
            REQUIRE(res.response_code == dt::ResponseCode::FAILED_UnknownSession);
        }
    }
}
