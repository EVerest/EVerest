// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Pionix GmbH and Contributors to EVerest
#include <catch2/catch_test_macros.hpp>

#include <iso15118/detail/d2/ev/state/payment_service_selection.hpp>

using namespace iso15118;
namespace dt = message_2::datatypes;

SCENARIO("EVCC ISO-2 PaymentServiceSelection request/response handling") {

    GIVEN("A request for the selected charge service") {
        const auto req = d2::ev::state::payment_service_selection::create_request(7);
        THEN("ExternalPayment (EIM) and the single charge service are selected") {
            REQUIRE(req.selected_payment_option == dt::PaymentOption::ExternalPayment);
            REQUIRE(req.selected_service_list.size() == 1);
            REQUIRE(req.selected_service_list[0].service_id == 7);
            REQUIRE(not req.selected_service_list[0].parameter_set_id.has_value());
        }
    }

    GIVEN("An OK response") {
        message_2::PaymentServiceSelectionResponse res;
        res.response_code = dt::ResponseCode::OK;
        THEN("It is valid") {
            REQUIRE(d2::ev::state::payment_service_selection::handle_response(res).valid);
        }
    }

    GIVEN("A failed response") {
        message_2::PaymentServiceSelectionResponse res;
        res.response_code = dt::ResponseCode::FAILED_PaymentSelectionInvalid;
        THEN("It is invalid") {
            REQUIRE(not d2::ev::state::payment_service_selection::handle_response(res).valid);
        }
    }
}
