// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Pionix GmbH and Contributors to EVerest
#include <catch2/catch_test_macros.hpp>

#include <iso15118/detail/d2/ev/state/service_discovery.hpp>

using namespace iso15118;
namespace dt = message_2::datatypes;

namespace {
message_2::ServiceDiscoveryResponse make_response(dt::EnergyTransferMode offered,
                                                 bool offer_eim = true) {
    message_2::ServiceDiscoveryResponse res;
    res.response_code = dt::ResponseCode::OK;
    if (offer_eim) {
        res.payment_option_list.push_back(dt::PaymentOption::ExternalPayment);
    } else {
        res.payment_option_list.push_back(dt::PaymentOption::Contract);
    }
    res.charge_service.service_id = 1;
    res.charge_service.service_category = dt::ServiceCategory::EVCharging;
    res.charge_service.free_service = true;
    res.charge_service.supported_energy_transfer_mode.push_back(offered);
    return res;
}
} // namespace

SCENARIO("EVCC ISO-2 ServiceDiscovery request/response handling") {

    GIVEN("A default request") {
        const auto req = d2::ev::state::service_discovery::create_request();
        THEN("No scope/category filter is set") {
            REQUIRE(not req.service_scope.has_value());
            REQUIRE(not req.service_category.has_value());
        }
    }

    GIVEN("A response offering the requested DC mode") {
        const auto res = make_response(dt::EnergyTransferMode::DC_extended);
        const auto result =
            d2::ev::state::service_discovery::handle_response(res, dt::EnergyTransferMode::DC_extended);
        THEN("The mode is supported, EIM is offered and the charge service id captured") {
            REQUIRE(result.valid);
            REQUIRE(result.mode_supported);
            REQUIRE(result.eim_offered);
            REQUIRE(result.charge_service_id == 1);
        }
    }

    GIVEN("A response that does not offer ExternalPayment") {
        const auto res = make_response(dt::EnergyTransferMode::DC_extended, false);
        const auto result =
            d2::ev::state::service_discovery::handle_response(res, dt::EnergyTransferMode::DC_extended);
        THEN("EIM is not offered") {
            REQUIRE(result.valid);
            REQUIRE(not result.eim_offered);
        }
    }

    GIVEN("A response that does not offer the requested mode") {
        const auto res = make_response(dt::EnergyTransferMode::AC_three_phase_core);
        const auto result =
            d2::ev::state::service_discovery::handle_response(res, dt::EnergyTransferMode::DC_extended);
        THEN("The mode is not supported") {
            REQUIRE(result.valid);
            REQUIRE(not result.mode_supported);
        }
    }

    GIVEN("A failed response") {
        auto res = make_response(dt::EnergyTransferMode::DC_extended);
        res.response_code = dt::ResponseCode::FAILED;
        const auto result =
            d2::ev::state::service_discovery::handle_response(res, dt::EnergyTransferMode::DC_extended);
        THEN("The result is invalid") {
            REQUIRE(not result.valid);
        }
    }
}
