// SPDX-License-Identifier: Apache-2.0
// Copyright 2024 Pionix GmbH and Contributors to EVerest
#include <catch2/catch_test_macros.hpp>

#include <iso15118/message_2/authorization.hpp>
#include <iso15118/message_2/variant.hpp>

#include "helper.hpp"

using namespace iso15118;
using namespace iso15118::message_2::datatypes;

SCENARIO("Se/Deserialize ISO-2 authorization messages") {

    GIVEN("Round-trip authorization_req (EIM, no Id/GenChallenge)") {
        message_2::AuthorizationRequest req;
        req.header.session_id = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08};

        const auto serialized = serialize_helper(req);
        const io::StreamInputView stream_view{serialized.data(), serialized.size()};
        message_2::Variant variant(stream_view);

        THEN("It should be deserialized successfully") {
            REQUIRE(variant.get_type() == message_2::Type::AuthorizationReq);
            const auto& msg = variant.get<message_2::AuthorizationRequest>();
            REQUIRE(not msg.id.has_value());
            REQUIRE(not msg.gen_challenge.has_value());
        }
    }

    GIVEN("Round-trip authorization_res") {
        message_2::AuthorizationResponse res;
        res.header.session_id = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08};
        res.response_code = ResponseCode::OK;
        res.evse_processing = EVSEProcessing::Finished;

        const auto serialized = serialize_helper(res);
        const io::StreamInputView stream_view{serialized.data(), serialized.size()};
        message_2::Variant variant(stream_view);

        THEN("It should be deserialized successfully") {
            REQUIRE(variant.get_type() == message_2::Type::AuthorizationRes);
            const auto& msg = variant.get<message_2::AuthorizationResponse>();
            REQUIRE(msg.response_code == ResponseCode::OK);
            REQUIRE(msg.evse_processing == EVSEProcessing::Finished);
        }
    }
}
