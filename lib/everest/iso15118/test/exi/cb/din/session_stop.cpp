// SPDX-License-Identifier: Apache-2.0
// Copyright 2024 Pionix GmbH and Contributors to EVerest
#include <catch2/catch_test_macros.hpp>

#include <iso15118/message_din/session_stop.hpp>
#include <iso15118/message_din/variant.hpp>

#include "helper.hpp"

using namespace iso15118;
using namespace iso15118::message_din;

SCENARIO("Se/Deserialize DIN session stop messages") {

    const datatypes::SessionId session_id = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08};

    GIVEN("Serialize and deserialize session_stop_req (empty body)") {
        SessionStopRequest req;
        req.header.session_id = session_id;

        const auto bytes = serialize_helper(req);

        THEN("It round-trips through the Variant") {
            const io::StreamInputView view{bytes.data(), bytes.size()};
            message_din::Variant variant(view);

            REQUIRE(variant.get_type() == Type::SessionStopReq);
            const auto& msg = variant.get<SessionStopRequest>();
            REQUIRE(msg.header.session_id == session_id);
        }
    }

    GIVEN("Serialize and deserialize session_stop_res") {
        SessionStopResponse res;
        res.header.session_id = session_id;
        res.response_code = datatypes::ResponseCode::OK;

        const auto bytes = serialize_helper(res);

        THEN("It round-trips through the Variant") {
            const io::StreamInputView view{bytes.data(), bytes.size()};
            message_din::Variant variant(view);

            REQUIRE(variant.get_type() == Type::SessionStopRes);
            const auto& msg = variant.get<SessionStopResponse>();
            REQUIRE(msg.header.session_id == session_id);
            REQUIRE(msg.response_code == datatypes::ResponseCode::OK);
        }
    }
}
