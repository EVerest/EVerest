// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Pionix GmbH and Contributors to EVerest
#include <catch2/catch_test_macros.hpp>

#include <iso15118/detail/din/ev/state/service_discovery.hpp>

using namespace iso15118;
namespace dt = message_din::datatypes;
using namespace din::ev::state::service_discovery;

namespace {

message_din::ServiceDiscoveryResponse make_response(dt::SupportedEnergyTransferMode mode, bool eim) {
    message_din::ServiceDiscoveryResponse res;
    res.response_code = dt::ResponseCode::OK;
    res.charge_service.service_tag.service_id = 5;
    res.charge_service.energy_transfer_type = mode;
    if (eim) {
        res.payment_options.push_back(dt::PaymentOption::ExternalPayment);
    } else {
        res.payment_options.push_back(dt::PaymentOption::Contract);
    }
    return res;
}

} // namespace

SCENARIO("EVCC DIN ServiceDiscovery request/response handling") {
    GIVEN("An empty request") {
        const auto req = create_request();
        THEN("No scope or category is set") {
            REQUIRE(not req.service_scope.has_value());
            REQUIRE(not req.service_category.has_value());
        }
    }

    GIVEN("A response offering DC_extended and EIM matching a DC_extended request") {
        const auto res = make_response(dt::SupportedEnergyTransferMode::DC_extended, true);
        const auto result = handle_response(res, dt::EnergyTransferMode::DC_extended);
        THEN("It is valid, supported, EIM offered, and the service id captured") {
            REQUIRE(result.valid);
            REQUIRE(result.charge_service_supported);
            REQUIRE(result.eim_offered);
            REQUIRE(result.charge_service_id == 5);
        }
    }

    GIVEN("A response offering a mismatching energy transfer type") {
        const auto res = make_response(dt::SupportedEnergyTransferMode::DC_core, true);
        const auto result = handle_response(res, dt::EnergyTransferMode::DC_extended);
        THEN("The charge service is not supported") {
            REQUIRE(result.valid);
            REQUIRE(not result.charge_service_supported);
        }
    }

    GIVEN("A response without ExternalPayment") {
        const auto res = make_response(dt::SupportedEnergyTransferMode::DC_extended, false);
        const auto result = handle_response(res, dt::EnergyTransferMode::DC_extended);
        THEN("EIM is not offered") {
            REQUIRE(not result.eim_offered);
        }
    }

    GIVEN("A failed response") {
        auto res = make_response(dt::SupportedEnergyTransferMode::DC_extended, true);
        res.response_code = dt::ResponseCode::FAILED;
        const auto result = handle_response(res, dt::EnergyTransferMode::DC_extended);
        THEN("It is invalid") {
            REQUIRE(not result.valid);
        }
    }
}
