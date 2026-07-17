// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Pionix GmbH and Contributors to EVerest
#include <catch2/catch_test_macros.hpp>

#include <iso15118/detail/d2/state/payment_service_selection.hpp>

using namespace iso15118;
namespace dt = message_2::datatypes;

SCENARIO("ISO 15118-2 SECC PaymentServiceSelection handling") {
    const dt::SessionId id{};

    GIVEN("ExternalPayment with the charge service selected") {
        message_2::PaymentServiceSelectionRequest req;
        req.selected_payment_option = dt::PaymentOption::ExternalPayment;
        req.selected_service_list.push_back(dt::SelectedService{1, std::nullopt});
        const auto res = d2::state::handle_request(req, id, 1, false);
        THEN("OK") {
            REQUIRE(res.response_code == dt::ResponseCode::OK);
        }
    }

    GIVEN("Contract payment is selected") {
        message_2::PaymentServiceSelectionRequest req;
        req.selected_payment_option = dt::PaymentOption::Contract;
        req.selected_service_list.push_back(dt::SelectedService{1, std::nullopt});
        const auto res = d2::state::handle_request(req, id, 1, false);
        THEN("FAILED_PaymentSelectionInvalid") {
            REQUIRE(res.response_code == dt::ResponseCode::FAILED_PaymentSelectionInvalid);
        }
    }

    GIVEN("The charge service is not selected") {
        message_2::PaymentServiceSelectionRequest req;
        req.selected_payment_option = dt::PaymentOption::ExternalPayment;
        const auto res = d2::state::handle_request(req, id, 1, false);
        THEN("FAILED_NoChargeServiceSelected") {
            REQUIRE(res.response_code == dt::ResponseCode::FAILED_NoChargeServiceSelected);
        }
    }

    GIVEN("A selected service that was never offered") {
        // [V2G2-467]: a ServiceID not in the offered ServiceList is rejected.
        message_2::PaymentServiceSelectionRequest req;
        req.selected_payment_option = dt::PaymentOption::ExternalPayment;
        req.selected_service_list.push_back(dt::SelectedService{1, std::nullopt});
        req.selected_service_list.push_back(dt::SelectedService{99, std::nullopt});
        const auto res = d2::state::handle_request(req, id, 1, false, /*cert_service_offered=*/false);
        THEN("FAILED_ServiceSelectionInvalid") {
            REQUIRE(res.response_code == dt::ResponseCode::FAILED_ServiceSelectionInvalid);
        }
    }

    GIVEN("The Certificate service selected while offered (PnC over TLS)") {
        message_2::PaymentServiceSelectionRequest req;
        req.selected_payment_option = dt::PaymentOption::Contract;
        req.selected_service_list.push_back(dt::SelectedService{1, std::nullopt});
        req.selected_service_list.push_back(dt::SelectedService{dt::CERTIFICATE_SERVICE_ID, std::nullopt});
        const auto res = d2::state::handle_request(req, id, 1, /*pnc_enabled=*/true, /*cert_service_offered=*/true);
        THEN("OK") {
            REQUIRE(res.response_code == dt::ResponseCode::OK);
        }
    }
}
