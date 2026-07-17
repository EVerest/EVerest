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
}
