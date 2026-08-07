// SPDX-License-Identifier: Apache-2.0
// Copyright 2024 Pionix GmbH and Contributors to EVerest
#include <catch2/catch_test_macros.hpp>

#include <iso15118/message_2/power_delivery.hpp>
#include <iso15118/message_2/variant.hpp>

#include "helper.hpp"

using namespace iso15118;
using namespace iso15118::message_2::datatypes;

SCENARIO("Se/Deserialize ISO-2 power delivery messages") {

    GIVEN("Round-trip power_delivery_req with ChargingProfile") {
        message_2::PowerDeliveryRequest req;
        req.header.session_id = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08};
        req.charge_progress = ChargeProgress::Start;
        req.sa_schedule_tuple_id = 1;

        auto& profile = req.charging_profile.emplace();
        auto& entry0 = profile.profile_entry.emplace_back();
        entry0.start = 0;
        entry0.max_power = to_physical_value(11000, Unit::W);
        entry0.max_number_of_phases_in_use = 3;
        auto& entry1 = profile.profile_entry.emplace_back();
        entry1.start = 1800;
        entry1.max_power = to_physical_value(7000, Unit::W);

        const auto serialized = serialize_helper(req);
        const io::StreamInputView stream_view{serialized.data(), serialized.size()};
        message_2::Variant variant(stream_view);

        THEN("It should be deserialized successfully") {
            REQUIRE(variant.get_type() == message_2::Type::PowerDeliveryReq);
            const auto& msg = variant.get<message_2::PowerDeliveryRequest>();
            REQUIRE(msg.charge_progress == ChargeProgress::Start);
            REQUIRE(msg.sa_schedule_tuple_id == 1);
            REQUIRE(msg.charging_profile.has_value());
            REQUIRE(msg.charging_profile->profile_entry.size() == 2);
            REQUIRE(msg.charging_profile->profile_entry[0].start == 0);
            REQUIRE(msg.charging_profile->profile_entry[0].max_number_of_phases_in_use.has_value());
            REQUIRE(msg.charging_profile->profile_entry[0].max_number_of_phases_in_use.value() == 3);
            REQUIRE(msg.charging_profile->profile_entry[1].start == 1800);
            REQUIRE(from_physical_value(msg.charging_profile->profile_entry[1].max_power) == 7000);
        }
    }

    GIVEN("Round-trip power_delivery_req with DC_EVPowerDeliveryParameter") {
        message_2::PowerDeliveryRequest req;
        req.header.session_id = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08};
        req.charge_progress = ChargeProgress::Stop;
        req.sa_schedule_tuple_id = 2;
        auto& dc = req.dc_ev_power_delivery_parameter.emplace();
        dc.dc_ev_status = {true, DC_EVErrorCode::NO_ERROR, 80};
        dc.bulk_charging_complete = false;
        dc.charging_complete = true;

        const auto serialized = serialize_helper(req);
        const io::StreamInputView stream_view{serialized.data(), serialized.size()};
        message_2::Variant variant(stream_view);

        THEN("It should be deserialized successfully") {
            REQUIRE(variant.get_type() == message_2::Type::PowerDeliveryReq);
            const auto& msg = variant.get<message_2::PowerDeliveryRequest>();
            REQUIRE(msg.dc_ev_power_delivery_parameter.has_value());
            REQUIRE(msg.dc_ev_power_delivery_parameter->charging_complete == true);
            REQUIRE(msg.dc_ev_power_delivery_parameter->dc_ev_status.ev_ress_soc == 80);
        }
    }

    GIVEN("Round-trip power_delivery_res (DC)") {
        message_2::PowerDeliveryResponse res;
        res.header.session_id = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08};
        res.response_code = ResponseCode::OK;
        res.dc_evse_status = DC_EVSEStatus{0, EVSENotification::None, std::nullopt, DC_EVSEStatusCode::EVSE_Ready};

        const auto serialized = serialize_helper(res);
        const io::StreamInputView stream_view{serialized.data(), serialized.size()};
        message_2::Variant variant(stream_view);

        THEN("It should be deserialized successfully") {
            REQUIRE(variant.get_type() == message_2::Type::PowerDeliveryRes);
            const auto& msg = variant.get<message_2::PowerDeliveryResponse>();
            REQUIRE(msg.response_code == ResponseCode::OK);
            REQUIRE(msg.dc_evse_status.has_value());
            REQUIRE(msg.dc_evse_status->status_code == DC_EVSEStatusCode::EVSE_Ready);
        }
    }
}
