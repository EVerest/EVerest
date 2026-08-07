// SPDX-License-Identifier: Apache-2.0
// Copyright 2024 Pionix GmbH and Contributors to EVerest
#include <catch2/catch_test_macros.hpp>

#include <iso15118/message_din/service_discovery.hpp>
#include <iso15118/message_din/variant.hpp>

#include "helper.hpp"

using namespace iso15118;
using namespace iso15118::message_din;

SCENARIO("Se/Deserialize DIN service discovery messages") {

    const datatypes::SessionId session_id = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08};

    GIVEN("Serialize and deserialize service_discovery_req") {
        ServiceDiscoveryRequest req;
        req.header.session_id = session_id;
        req.service_category = datatypes::ServiceCategory::EVCharging;

        const auto bytes = serialize_helper(req);

        THEN("It round-trips through the Variant") {
            const io::StreamInputView view{bytes.data(), bytes.size()};
            message_din::Variant variant(view);

            REQUIRE(variant.get_type() == Type::ServiceDiscoveryReq);
            const auto& msg = variant.get<ServiceDiscoveryRequest>();
            REQUIRE(msg.header.session_id == session_id);
            REQUIRE(msg.service_category.has_value());
            REQUIRE(msg.service_category.value() == datatypes::ServiceCategory::EVCharging);
        }
    }

    GIVEN("Serialize and deserialize service_discovery_res") {
        ServiceDiscoveryResponse res;
        res.header.session_id = session_id;
        res.response_code = datatypes::ResponseCode::OK;
        res.payment_options = {datatypes::PaymentOption::ExternalPayment, datatypes::PaymentOption::Contract};
        res.charge_service.service_tag.service_id = 1;
        res.charge_service.service_tag.service_category = datatypes::ServiceCategory::EVCharging;
        res.charge_service.free_service = true;
        res.charge_service.energy_transfer_type = datatypes::SupportedEnergyTransferMode::DC_extended;

        const auto bytes = serialize_helper(res);

        THEN("It round-trips through the Variant") {
            const io::StreamInputView view{bytes.data(), bytes.size()};
            message_din::Variant variant(view);

            REQUIRE(variant.get_type() == Type::ServiceDiscoveryRes);
            const auto& msg = variant.get<ServiceDiscoveryResponse>();
            REQUIRE(msg.header.session_id == session_id);
            REQUIRE(msg.response_code == datatypes::ResponseCode::OK);
            REQUIRE(msg.payment_options.size() == 2);
            REQUIRE(msg.payment_options[0] == datatypes::PaymentOption::ExternalPayment);
            REQUIRE(msg.payment_options[1] == datatypes::PaymentOption::Contract);
            REQUIRE(msg.charge_service.service_tag.service_id == 1);
            REQUIRE(msg.charge_service.free_service == true);
            REQUIRE(msg.charge_service.energy_transfer_type == datatypes::SupportedEnergyTransferMode::DC_extended);
            REQUIRE(msg.service_list.has_value() == false);
        }
    }
}
