// SPDX-License-Identifier: Apache-2.0
// Copyright 2024 Pionix GmbH and Contributors to EVerest
#include <catch2/catch_test_macros.hpp>

#include <iso15118/message_din/current_demand.hpp>
#include <iso15118/message_din/variant.hpp>

#include "helper.hpp"

using namespace iso15118;
using namespace iso15118::message_din;

SCENARIO("Se/Deserialize DIN current demand messages") {

    const datatypes::SessionId session_id = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08};

    GIVEN("Serialize and deserialize current_demand_req") {
        CurrentDemandRequest req;
        req.header.session_id = session_id;
        req.dc_ev_status.ev_ready = true;
        req.dc_ev_status.ev_ress_soc = 70;
        req.ev_target_current = 125.0;
        req.ev_target_voltage = 420.0;
        req.ev_maximum_voltage_limit = 500.0;
        req.charging_complete = false;

        THEN("It round-trips through the Variant") {
            const auto variant = roundtrip(req);

            REQUIRE(variant.get_type() == Type::CurrentDemandReq);
            const auto& msg = variant.get<CurrentDemandRequest>();
            REQUIRE(msg.header.session_id == session_id);
            REQUIRE(msg.dc_ev_status.ev_ress_soc == 70);
            REQUIRE(msg.ev_target_current == 125.0);
            REQUIRE(msg.ev_target_voltage == 420.0);
            REQUIRE(msg.ev_maximum_voltage_limit.has_value());
            REQUIRE(msg.ev_maximum_voltage_limit.value() == 500.0);
            REQUIRE(msg.charging_complete == false);
        }

        THEN("The wire carries the right units, which the domain type does not keep") {
            const auto& raw = decode_raw(serialize_helper(req)).V2G_Message.Body.CurrentDemandReq;

            REQUIRE(raw.EVTargetCurrent.Unit == din_unitSymbolType_A);
            REQUIRE(raw.EVTargetVoltage.Unit == din_unitSymbolType_V);
            REQUIRE(raw.EVMaximumVoltageLimit_isUsed);
            REQUIRE(raw.EVMaximumVoltageLimit.Unit == din_unitSymbolType_V);
        }
    }

    GIVEN("Serialize and deserialize current_demand_res") {
        CurrentDemandResponse res;
        res.header.session_id = session_id;
        res.response_code = datatypes::ResponseCode::OK;
        res.dc_evse_status.evse_status_code = datatypes::DcEvseStatusCode::EVSE_Ready;
        res.evse_present_voltage = 419.0;
        res.evse_present_current = 124.0;
        res.evse_current_limit_achieved = false;
        res.evse_voltage_limit_achieved = false;
        res.evse_power_limit_achieved = false;
        res.evse_maximum_current_limit = 300.0;

        THEN("It round-trips through the Variant") {
            const auto variant = roundtrip(res);

            REQUIRE(variant.get_type() == Type::CurrentDemandRes);
            const auto& msg = variant.get<CurrentDemandResponse>();
            REQUIRE(msg.header.session_id == session_id);
            REQUIRE(msg.response_code == datatypes::ResponseCode::OK);
            REQUIRE(msg.evse_present_voltage == 419.0);
            REQUIRE(msg.evse_present_current == 124.0);
            REQUIRE(msg.evse_current_limit_achieved == false);
            REQUIRE(msg.evse_maximum_current_limit.has_value());
            REQUIRE(msg.evse_maximum_current_limit.value() == 300.0);
        }

        THEN("The wire carries the right units, which the domain type does not keep") {
            const auto& raw = decode_raw(serialize_helper(res)).V2G_Message.Body.CurrentDemandRes;

            REQUIRE(raw.EVSEPresentVoltage.Unit == din_unitSymbolType_V);
            REQUIRE(raw.EVSEPresentCurrent.Unit == din_unitSymbolType_A);
            REQUIRE(raw.EVSEMaximumCurrentLimit_isUsed);
            REQUIRE(raw.EVSEMaximumCurrentLimit.Unit == din_unitSymbolType_A);
        }
    }
}
