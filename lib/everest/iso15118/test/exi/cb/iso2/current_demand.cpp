// SPDX-License-Identifier: Apache-2.0
// Copyright 2024 Pionix GmbH and Contributors to EVerest
#include <catch2/catch_test_macros.hpp>

#include <iso15118/message_2/current_demand.hpp>
#include <iso15118/message_2/variant.hpp>

#include "helper.hpp"

using namespace iso15118;
using namespace iso15118::message_2::datatypes;

SCENARIO("Se/Deserialize ISO-2 current demand messages") {

    GIVEN("Round-trip current_demand_req") {
        message_2::CurrentDemandRequest req;
        req.header.session_id = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08};
        req.dc_ev_status = {true, DC_EVErrorCode::NO_ERROR, 55};
        req.ev_target_current = to_physical_value(80, Unit::A);
        req.ev_target_voltage = to_physical_value(400, Unit::V);
        req.charging_complete = false;
        req.ev_maximum_current_limit = to_physical_value(200, Unit::A);
        req.bulk_charging_complete = false;

        const auto serialized = serialize_helper(req);
        const io::StreamInputView stream_view{serialized.data(), serialized.size()};
        message_2::Variant variant(stream_view);

        THEN("It should be deserialized successfully") {
            REQUIRE(variant.get_type() == message_2::Type::CurrentDemandReq);
            const auto& msg = variant.get<message_2::CurrentDemandRequest>();
            REQUIRE(from_physical_value(msg.ev_target_current) == 80);
            REQUIRE(from_physical_value(msg.ev_target_voltage) == 400);
            REQUIRE(msg.charging_complete == false);
            REQUIRE(msg.ev_maximum_current_limit.has_value());
            REQUIRE(from_physical_value(msg.ev_maximum_current_limit.value()) == 200);
        }
    }

    GIVEN("Round-trip current_demand_res") {
        message_2::CurrentDemandResponse res;
        res.header.session_id = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08};
        res.response_code = ResponseCode::OK;
        res.dc_evse_status = {0, EVSENotification::None, std::nullopt, DC_EVSEStatusCode::EVSE_Ready};
        res.evse_present_voltage = to_physical_value(400, Unit::V);
        res.evse_present_current = to_physical_value(79, Unit::A);
        res.evse_current_limit_achieved = false;
        res.evse_voltage_limit_achieved = false;
        res.evse_power_limit_achieved = true;
        res.evse_id = "DE*PNX*E12345*1";
        res.sa_schedule_tuple_id = 1;
        res.evse_maximum_power_limit = to_physical_value(50000, Unit::W);

        const auto serialized = serialize_helper(res);
        const auto doc = decode_helper(serialized);

        THEN("The encoded response converts back field for field") {
            REQUIRE(doc.V2G_Message.Body.CurrentDemandRes_isUsed);
            const auto msg = to_response<message_2::CurrentDemandResponse>(doc, doc.V2G_Message.Body.CurrentDemandRes);
            REQUIRE(msg.response_code == ResponseCode::OK);
            REQUIRE(from_physical_value(msg.evse_present_voltage) == 400);
            REQUIRE(from_physical_value(msg.evse_present_current) == 79);
            REQUIRE(msg.evse_power_limit_achieved == true);
            REQUIRE(msg.evse_id == "DE*PNX*E12345*1");
            REQUIRE(msg.sa_schedule_tuple_id == 1);
            REQUIRE(msg.evse_maximum_power_limit.has_value());
            REQUIRE(from_physical_value(msg.evse_maximum_power_limit.value()) == 50000);
            REQUIRE_FALSE(msg.evse_maximum_voltage_limit.has_value());
            REQUIRE_FALSE(msg.evse_maximum_current_limit.has_value());
            REQUIRE_FALSE(msg.meter_info.has_value());
            REQUIRE_FALSE(msg.receipt_required.has_value());
        }
    }

    GIVEN("Round-trip current_demand_res with every optional element") {
        message_2::CurrentDemandResponse res;
        res.header.session_id = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08};
        res.response_code = ResponseCode::OK;
        res.dc_evse_status = {0, EVSENotification::None, IsolationLevel::Valid, DC_EVSEStatusCode::EVSE_Ready};
        res.evse_present_voltage = to_physical_value(400, Unit::V);
        res.evse_present_current = to_physical_value(79, Unit::A);
        res.evse_current_limit_achieved = false;
        res.evse_voltage_limit_achieved = false;
        res.evse_power_limit_achieved = false;
        res.evse_maximum_voltage_limit = to_physical_value(920, Unit::V);
        res.evse_maximum_current_limit = to_physical_value(300, Unit::A);
        res.evse_maximum_power_limit = to_physical_value(150000, Unit::W);
        res.evse_id = "DE*PNX*E12345*1";
        res.sa_schedule_tuple_id = 1;
        auto& meter = res.meter_info.emplace();
        meter.meter_id = "METER01";
        meter.meter_reading = 4242;
        meter.meter_status = 1;
        meter.t_meter = 1700000000;
        res.receipt_required = true;

        const auto serialized = serialize_helper(res);
        const auto doc = decode_helper(serialized);

        THEN("Every optional element survives the round-trip") {
            REQUIRE(doc.V2G_Message.Body.CurrentDemandRes_isUsed);
            const auto msg = to_response<message_2::CurrentDemandResponse>(doc, doc.V2G_Message.Body.CurrentDemandRes);
            REQUIRE(msg.dc_evse_status.isolation_status == IsolationLevel::Valid);
            REQUIRE(msg.evse_maximum_voltage_limit.has_value());
            REQUIRE(from_physical_value(msg.evse_maximum_voltage_limit.value()) == 920);
            REQUIRE(msg.evse_maximum_current_limit.has_value());
            REQUIRE(from_physical_value(msg.evse_maximum_current_limit.value()) == 300);
            REQUIRE(msg.evse_maximum_power_limit.has_value());
            REQUIRE(from_physical_value(msg.evse_maximum_power_limit.value()) == 150000);
            REQUIRE(msg.meter_info.has_value());
            REQUIRE(msg.meter_info->meter_id == "METER01");
            REQUIRE(msg.meter_info->meter_reading == 4242);
            REQUIRE(msg.meter_info->meter_status == 1);
            REQUIRE(msg.meter_info->t_meter == 1700000000);
            REQUIRE(msg.receipt_required == true);
        }
    }
}
