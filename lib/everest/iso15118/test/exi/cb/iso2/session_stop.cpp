// SPDX-License-Identifier: Apache-2.0
// Copyright 2024 Pionix GmbH and Contributors to EVerest
#include <catch2/catch_test_macros.hpp>

#include <iso15118/message_2/session_stop.hpp>
#include <iso15118/message_2/variant.hpp>

#include "helper.hpp"

using namespace iso15118;
using namespace iso15118::message_2::datatypes;

SCENARIO("Se/Deserialize ISO-2 session stop messages") {

    GIVEN("Round-trip session_stop_req (Terminate)") {
        message_2::SessionStopRequest req;
        req.header.session_id = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08};
        req.charging_session = ChargingSession::Terminate;

        const auto serialized = serialize_helper(req);
        const io::StreamInputView stream_view{serialized.data(), serialized.size()};
        message_2::Variant variant(stream_view);

        THEN("It should be deserialized successfully") {
            REQUIRE(variant.get_type() == message_2::Type::SessionStopReq);
            const auto& msg = variant.get<message_2::SessionStopRequest>();
            REQUIRE(msg.charging_session == ChargingSession::Terminate);
        }
    }

    GIVEN("Round-trip session_stop_req (Pause)") {
        message_2::SessionStopRequest req;
        req.header.session_id = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08};
        req.charging_session = ChargingSession::Pause;

        const auto serialized = serialize_helper(req);
        const io::StreamInputView stream_view{serialized.data(), serialized.size()};
        message_2::Variant variant(stream_view);

        THEN("It should be deserialized successfully") {
            REQUIRE(variant.get_type() == message_2::Type::SessionStopReq);
            const auto& msg = variant.get<message_2::SessionStopRequest>();
            REQUIRE(msg.charging_session == ChargingSession::Pause);
        }
    }

    GIVEN("Round-trip session_stop_res") {
        message_2::SessionStopResponse res;
        res.header.session_id = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08};
        res.response_code = ResponseCode::OK;

        const auto serialized = serialize_helper(res);
        const io::StreamInputView stream_view{serialized.data(), serialized.size()};
        message_2::Variant variant(stream_view);

        THEN("It should be deserialized successfully") {
            REQUIRE(variant.get_type() == message_2::Type::SessionStopRes);
            const auto& msg = variant.get<message_2::SessionStopResponse>();
            REQUIRE(msg.response_code == ResponseCode::OK);
        }
    }
}
