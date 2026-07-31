// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Pionix GmbH and Contributors to EVerest
#include <catch2/catch_test_macros.hpp>

#include <iso15118/detail/din/ev/state/service_payment_selection.hpp>

using namespace iso15118;
namespace dt = message_din::datatypes;
using namespace din::ev::state::service_payment_selection;

SCENARIO("EVCC DIN ServicePaymentSelection request/response handling") {
    GIVEN("A request for charge service id 7") {
        const auto req = create_request(7);
        THEN("ExternalPayment is selected with exactly one selected service") {
            REQUIRE(req.selected_payment_option == dt::PaymentOption::ExternalPayment);
            REQUIRE(req.selected_service_list.size() == 1);
            REQUIRE(req.selected_service_list.front().service_id == 7);
            REQUIRE(not req.selected_service_list.front().parameter_set_id.has_value());
        }
    }

    GIVEN("An OK response") {
        message_din::ServicePaymentSelectionResponse res;
        res.response_code = dt::ResponseCode::OK;
        THEN("It is valid") {
            REQUIRE(handle_response(res).valid);
        }
    }

    GIVEN("A failed response") {
        message_din::ServicePaymentSelectionResponse res;
        res.response_code = dt::ResponseCode::FAILED_PaymentSelectionInvalid;
        THEN("It is invalid") {
            REQUIRE(not handle_response(res).valid);
        }
    }
}
