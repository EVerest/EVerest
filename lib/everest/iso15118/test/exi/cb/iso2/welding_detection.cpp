// SPDX-License-Identifier: Apache-2.0
// Copyright 2024 Pionix GmbH and Contributors to EVerest
#include <catch2/catch_test_macros.hpp>

#include <iso15118/message_2/variant.hpp>
#include <iso15118/message_2/welding_detection.hpp>

#include "helper.hpp"

using namespace iso15118;
using namespace iso15118::message_2::datatypes;

SCENARIO("Se/Deserialize ISO-2 welding detection messages") {

    GIVEN("Round-trip welding_detection_req") {
        message_2::WeldingDetectionRequest req;
        req.header.session_id = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08};
        req.dc_ev_status = {false, DC_EVErrorCode::NO_ERROR, 90};

        const auto serialized = serialize_helper(req);
        const io::StreamInputView stream_view{serialized.data(), serialized.size()};
        message_2::Variant variant(stream_view);

        THEN("It should be deserialized successfully") {
            REQUIRE(variant.get_type() == message_2::Type::WeldingDetectionReq);
            const auto& msg = variant.get<message_2::WeldingDetectionRequest>();
            REQUIRE(msg.dc_ev_status.ev_ress_soc == 90);
        }
    }

    GIVEN("Round-trip welding_detection_res") {
        message_2::WeldingDetectionResponse res;
        res.header.session_id = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08};
        res.response_code = ResponseCode::OK;
        res.dc_evse_status = {0, EVSENotification::None, std::nullopt, DC_EVSEStatusCode::EVSE_Ready};
        res.evse_present_voltage = to_physical_value(42, Unit::V);

        const auto serialized = serialize_helper(res);
        const io::StreamInputView stream_view{serialized.data(), serialized.size()};
        message_2::Variant variant(stream_view);

        THEN("It should be deserialized successfully") {
            REQUIRE(variant.get_type() == message_2::Type::WeldingDetectionRes);
            const auto& msg = variant.get<message_2::WeldingDetectionResponse>();
            REQUIRE(msg.response_code == ResponseCode::OK);
            REQUIRE(from_physical_value(msg.evse_present_voltage) == 42);
        }
    }
}
