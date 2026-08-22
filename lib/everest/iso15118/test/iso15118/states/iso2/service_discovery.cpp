// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Pionix GmbH and Contributors to EVerest
#include <catch2/catch_test_macros.hpp>

#include <algorithm>

#include <iso15118/detail/d2/state/service_discovery.hpp>

using namespace iso15118;
namespace dt = message_2::datatypes;

SCENARIO("ISO 15118-2 SECC ServiceDiscovery handling") {
    const dt::SessionId id{};
    message_2::ServiceDiscoveryRequest req;

    everest::lib::util::fixed_vector<dt::EnergyTransferMode, 6> modes;
    modes.push_back(dt::EnergyTransferMode::DC_extended);
    modes.push_back(dt::EnergyTransferMode::DC_core);

    WHEN("PnC is not enabled") {
        const auto res = d2::state::handle_request(req, id, 1, modes, false, /*cert_service_offered=*/false);

        THEN("OK, EIM-only payment options, DC ChargeService, no Certificate service") {
            REQUIRE(res.response_code == dt::ResponseCode::OK);
            REQUIRE(res.payment_option_list.size() == 1);
            REQUIRE(res.payment_option_list[0] == dt::PaymentOption::ExternalPayment);
            REQUIRE(res.charge_service.service_id == 1);
            REQUIRE(res.charge_service.service_category == dt::ServiceCategory::EVCharging);
            const auto& adv = res.charge_service.supported_energy_transfer_mode;
            REQUIRE(std::find(adv.begin(), adv.end(), dt::EnergyTransferMode::DC_extended) != adv.end());
            REQUIRE_FALSE(res.service_list.has_value());
        }
    }

    WHEN("PnC is enabled and certificate installation is supported") {
        const auto res = d2::state::handle_request(req, id, 1, modes, true, /*cert_service_offered=*/true);

        THEN("Contract is offered and the Certificate service (ID 2) is advertised [V2G2-410]") {
            REQUIRE(res.response_code == dt::ResponseCode::OK);
            REQUIRE(res.payment_option_list.size() == 2);
            REQUIRE(std::find(res.payment_option_list.begin(), res.payment_option_list.end(),
                              dt::PaymentOption::Contract) != res.payment_option_list.end());
            REQUIRE(res.service_list.has_value());
            REQUIRE(res.service_list->size() == 1);
            REQUIRE(res.service_list->front().service_id == dt::CERTIFICATE_SERVICE_ID);
            REQUIRE(res.service_list->front().service_category == dt::ServiceCategory::ContractCertificate);
        }
    }

    WHEN("PnC is enabled but certificate installation is not supported") {
        const auto res = d2::state::handle_request(req, id, 1, modes, true, /*cert_service_offered=*/false);

        THEN("Contract is offered but no Certificate service is advertised") {
            REQUIRE(res.response_code == dt::ResponseCode::OK);
            REQUIRE(res.payment_option_list.size() == 2);
            REQUIRE(std::find(res.payment_option_list.begin(), res.payment_option_list.end(),
                              dt::PaymentOption::Contract) != res.payment_option_list.end());
            REQUIRE_FALSE(res.service_list.has_value());
        }
    }
}
