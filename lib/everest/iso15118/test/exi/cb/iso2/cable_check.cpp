// SPDX-License-Identifier: Apache-2.0
// Copyright 2024 Pionix GmbH and Contributors to EVerest
#include <catch2/catch_test_macros.hpp>

#include <iso15118/message_2/cable_check.hpp>
#include <iso15118/message_2/variant.hpp>

#include "helper.hpp"

using namespace iso15118;
using namespace iso15118::message_2::datatypes;

SCENARIO("Se/Deserialize ISO-2 cable check messages") {

    GIVEN("Round-trip cable_check_req") {
        message_2::CableCheckRequest req;
        req.header.session_id = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08};
        req.dc_ev_status = {true, DC_EVErrorCode::NO_ERROR, 42};

        const auto serialized = serialize_helper(req);
        const io::StreamInputView stream_view{serialized.data(), serialized.size()};
        message_2::Variant variant(stream_view);

        THEN("It should be deserialized successfully") {
            REQUIRE(variant.get_type() == message_2::Type::CableCheckReq);
            const auto& msg = variant.get<message_2::CableCheckRequest>();
            REQUIRE(msg.dc_ev_status.ev_ready == true);
            REQUIRE(msg.dc_ev_status.ev_ress_soc == 42);
        }
    }

    GIVEN("Round-trip cable_check_res") {
        message_2::CableCheckResponse res;
        res.header.session_id = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08};
        res.response_code = ResponseCode::OK;
        res.dc_evse_status = {0, EVSENotification::None, IsolationLevel::Valid, DC_EVSEStatusCode::EVSE_Ready};
        res.evse_processing = EVSEProcessing::Ongoing;

        const auto serialized = serialize_helper(res);
        const io::StreamInputView stream_view{serialized.data(), serialized.size()};
        message_2::Variant variant(stream_view);

        THEN("It should be deserialized successfully") {
            REQUIRE(variant.get_type() == message_2::Type::CableCheckRes);
            const auto& msg = variant.get<message_2::CableCheckResponse>();
            REQUIRE(msg.response_code == ResponseCode::OK);
            REQUIRE(msg.evse_processing == EVSEProcessing::Ongoing);
            REQUIRE(msg.dc_evse_status.isolation_status.has_value());
            REQUIRE(msg.dc_evse_status.isolation_status.value() == IsolationLevel::Valid);
        }
    }
}
