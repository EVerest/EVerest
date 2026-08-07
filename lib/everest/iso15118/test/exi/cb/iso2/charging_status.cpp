// SPDX-License-Identifier: Apache-2.0
// Copyright 2024 Pionix GmbH and Contributors to EVerest
#include <catch2/catch_test_macros.hpp>

#include <iso15118/message_2/charging_status.hpp>
#include <iso15118/message_2/variant.hpp>

#include "helper.hpp"

using namespace iso15118;
using namespace iso15118::message_2::datatypes;

SCENARIO("Se/Deserialize ISO-2 charging status messages") {

    GIVEN("Round-trip charging_status_req (empty)") {
        message_2::ChargingStatusRequest req;
        req.header.session_id = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08};

        const auto serialized = serialize_helper(req);
        const io::StreamInputView stream_view{serialized.data(), serialized.size()};
        message_2::Variant variant(stream_view);

        THEN("It should be deserialized successfully") {
            REQUIRE(variant.get_type() == message_2::Type::ChargingStatusReq);
            REQUIRE(variant.get_session_id() == req.header.session_id);
        }
    }

    GIVEN("Round-trip charging_status_res") {
        message_2::ChargingStatusResponse res;
        res.header.session_id = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08};
        res.response_code = ResponseCode::OK;
        res.evse_id = "DE*PNX*E12345*1";
        res.sa_schedule_tuple_id = 1;
        res.evse_max_current = to_physical_value(32, Unit::A);
        res.receipt_required = false;
        res.ac_evse_status = {0, EVSENotification::None, false};

        auto& meter = res.meter_info.emplace();
        meter.meter_id = "METER01";
        meter.meter_reading = 123456;

        const auto serialized = serialize_helper(res);
        const io::StreamInputView stream_view{serialized.data(), serialized.size()};
        message_2::Variant variant(stream_view);

        THEN("It should be deserialized successfully") {
            REQUIRE(variant.get_type() == message_2::Type::ChargingStatusRes);
            const auto& msg = variant.get<message_2::ChargingStatusResponse>();
            REQUIRE(msg.response_code == ResponseCode::OK);
            REQUIRE(msg.evse_id == "DE*PNX*E12345*1");
            REQUIRE(msg.evse_max_current.has_value());
            REQUIRE(from_physical_value(msg.evse_max_current.value()) == 32);
            REQUIRE(msg.meter_info.has_value());
            REQUIRE(msg.meter_info->meter_id == "METER01");
            REQUIRE(msg.meter_info->meter_reading.has_value());
            REQUIRE(msg.meter_info->meter_reading.value() == 123456);
            REQUIRE(msg.receipt_required.has_value());
            REQUIRE(msg.receipt_required.value() == false);
        }
    }
}
