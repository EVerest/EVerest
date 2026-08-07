// SPDX-License-Identifier: Apache-2.0
// Copyright 2024 Pionix GmbH and Contributors to EVerest
#include <catch2/catch_test_macros.hpp>

#include <iso15118/message_2/service_discovery.hpp>
#include <iso15118/message_2/variant.hpp>

#include "helper.hpp"

using namespace iso15118;
using namespace iso15118::message_2::datatypes;

SCENARIO("Se/Deserialize ISO-2 service discovery messages") {

    GIVEN("Round-trip service_discovery_req") {
        message_2::ServiceDiscoveryRequest req;
        req.header.session_id = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08};
        req.service_category = ServiceCategory::EVCharging;

        const auto serialized = serialize_helper(req);
        const io::StreamInputView stream_view{serialized.data(), serialized.size()};
        message_2::Variant variant(stream_view);

        THEN("It should be deserialized successfully") {
            REQUIRE(variant.get_type() == message_2::Type::ServiceDiscoveryReq);
            const auto& msg = variant.get<message_2::ServiceDiscoveryRequest>();
            REQUIRE(msg.service_category.has_value());
            REQUIRE(msg.service_category.value() == ServiceCategory::EVCharging);
        }
    }

    GIVEN("Round-trip service_discovery_res") {
        message_2::ServiceDiscoveryResponse res;
        res.header.session_id = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08};
        res.response_code = ResponseCode::OK;
        res.payment_option_list = {PaymentOption::ExternalPayment};
        res.charge_service.service_id = 1;
        res.charge_service.service_category = ServiceCategory::EVCharging;
        res.charge_service.free_service = true;
        res.charge_service.supported_energy_transfer_mode = {EnergyTransferMode::DC_extended,
                                                             EnergyTransferMode::AC_three_phase_core};

        const auto serialized = serialize_helper(res);
        const io::StreamInputView stream_view{serialized.data(), serialized.size()};
        message_2::Variant variant(stream_view);

        THEN("It should be deserialized successfully") {
            REQUIRE(variant.get_type() == message_2::Type::ServiceDiscoveryRes);
            const auto& msg = variant.get<message_2::ServiceDiscoveryResponse>();
            REQUIRE(msg.response_code == ResponseCode::OK);
            REQUIRE(msg.payment_option_list.size() == 1);
            REQUIRE(msg.payment_option_list[0] == PaymentOption::ExternalPayment);
            REQUIRE(msg.charge_service.service_id == 1);
            REQUIRE(msg.charge_service.free_service == true);
            REQUIRE(msg.charge_service.supported_energy_transfer_mode.size() == 2);
            REQUIRE(msg.charge_service.supported_energy_transfer_mode[0] == EnergyTransferMode::DC_extended);
        }
    }
}
