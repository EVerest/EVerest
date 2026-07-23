// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Pionix GmbH and Contributors to EVerest
#include <catch2/catch_test_macros.hpp>

#include <iso15118/detail/din/state/service_payment_selection.hpp>

using namespace iso15118;
namespace dt = message_din::datatypes;

SCENARIO("DIN SECC ServicePaymentSelection state handling") {
    const dt::SessionId session{0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08};
    constexpr uint16_t charge_service_id = 1;

    GIVEN("ExternalPayment and the charge service selected") {
        message_din::ServicePaymentSelectionRequest req;
        req.header.session_id = session;
        req.selected_payment_option = dt::PaymentOption::ExternalPayment;
        req.selected_service_list = {{charge_service_id, std::nullopt}};

        const auto res = din::state::handle_request(req, charge_service_id, session);
        THEN("ResponseCode is OK") {
            REQUIRE(res.response_code == dt::ResponseCode::OK);
        }
    }

    GIVEN("A non-ExternalPayment option") {
        message_din::ServicePaymentSelectionRequest req;
        req.header.session_id = session;
        req.selected_payment_option = dt::PaymentOption::Contract;
        req.selected_service_list = {{charge_service_id, std::nullopt}};

        const auto res = din::state::handle_request(req, charge_service_id, session);
        THEN("ResponseCode is FAILED_PaymentSelectionInvalid") {
            REQUIRE(res.response_code == dt::ResponseCode::FAILED_PaymentSelectionInvalid);
        }
    }

    GIVEN("A wrong service in the selected list") {
        message_din::ServicePaymentSelectionRequest req;
        req.header.session_id = session;
        req.selected_payment_option = dt::PaymentOption::ExternalPayment;
        req.selected_service_list = {{static_cast<uint16_t>(charge_service_id + 1), std::nullopt}};

        const auto res = din::state::handle_request(req, charge_service_id, session);
        THEN("ResponseCode is FAILED_ServiceSelectionInvalid") {
            REQUIRE(res.response_code == dt::ResponseCode::FAILED_ServiceSelectionInvalid);
        }
    }
}
