// SPDX-License-Identifier: Apache-2.0
// Copyright 2024 Pionix GmbH and Contributors to EVerest
#include <catch2/catch_test_macros.hpp>

#include <iso15118/message_2/pre_charge.hpp>
#include <iso15118/message_2/variant.hpp>

#include "helper.hpp"

using namespace iso15118;
using namespace iso15118::message_2::datatypes;

SCENARIO("Se/Deserialize ISO-2 pre charge messages") {

    GIVEN("Round-trip pre_charge_req") {
        message_2::PreChargeRequest req;
        req.header.session_id = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08};
        req.dc_ev_status = {true, DC_EVErrorCode::NO_ERROR, 42};
        req.ev_target_voltage = to_physical_value(400, Unit::V);
        req.ev_target_current = to_physical_value(2, Unit::A);

        const auto serialized = serialize_helper(req);
        const io::StreamInputView stream_view{serialized.data(), serialized.size()};
        message_2::Variant variant(stream_view);

        THEN("It should be deserialized successfully") {
            REQUIRE(variant.get_type() == message_2::Type::PreChargeReq);
            const auto& msg = variant.get<message_2::PreChargeRequest>();
            REQUIRE(from_physical_value(msg.ev_target_voltage) == 400);
            REQUIRE(from_physical_value(msg.ev_target_current) == 2);
        }
    }

    GIVEN("Round-trip pre_charge_res") {
        message_2::PreChargeResponse res;
        res.header.session_id = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08};
        res.response_code = ResponseCode::OK;
        res.dc_evse_status = {0, EVSENotification::None, std::nullopt, DC_EVSEStatusCode::EVSE_Ready};
        res.evse_present_voltage = to_physical_value(399, Unit::V);

        const auto serialized = serialize_helper(res);
        const io::StreamInputView stream_view{serialized.data(), serialized.size()};
        message_2::Variant variant(stream_view);

        THEN("It should be deserialized successfully") {
            REQUIRE(variant.get_type() == message_2::Type::PreChargeRes);
            const auto& msg = variant.get<message_2::PreChargeResponse>();
            REQUIRE(msg.response_code == ResponseCode::OK);
            REQUIRE(from_physical_value(msg.evse_present_voltage) == 399);
        }
    }
}
