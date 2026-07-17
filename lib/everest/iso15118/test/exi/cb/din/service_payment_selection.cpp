// SPDX-License-Identifier: Apache-2.0
// Copyright 2024 Pionix GmbH and Contributors to EVerest
#include <catch2/catch_test_macros.hpp>

#include <iso15118/message_din/service_payment_selection.hpp>
#include <iso15118/message_din/variant.hpp>

#include "helper.hpp"

using namespace iso15118;
using namespace iso15118::message_din;

SCENARIO("Se/Deserialize DIN service payment selection messages") {

    const datatypes::SessionId session_id = {0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88};

    GIVEN("Serialize and deserialize service_payment_selection_req") {
        ServicePaymentSelectionRequest req;
        req.header.session_id = session_id;
        req.selected_payment_option = datatypes::PaymentOption::ExternalPayment;
        auto& service = req.selected_service_list.emplace_back();
        service.service_id = 1;

        const auto bytes = serialize_helper(req);

        THEN("It round-trips through the Variant") {
            const io::StreamInputView view{bytes.data(), bytes.size()};
            message_din::Variant variant(view);

            REQUIRE(variant.get_type() == Type::ServicePaymentSelectionReq);
            const auto& msg = variant.get<ServicePaymentSelectionRequest>();
            REQUIRE(msg.header.session_id == session_id);
            REQUIRE(msg.selected_payment_option == datatypes::PaymentOption::ExternalPayment);
            REQUIRE(msg.selected_service_list.size() == 1);
            REQUIRE(msg.selected_service_list[0].service_id == 1);
        }
    }

    GIVEN("Serialize and deserialize service_payment_selection_res") {
        ServicePaymentSelectionResponse res;
        res.header.session_id = session_id;
        res.response_code = datatypes::ResponseCode::OK;

        const auto bytes = serialize_helper(res);

        THEN("It round-trips through the Variant") {
            const io::StreamInputView view{bytes.data(), bytes.size()};
            message_din::Variant variant(view);

            REQUIRE(variant.get_type() == Type::ServicePaymentSelectionRes);
            const auto& msg = variant.get<ServicePaymentSelectionResponse>();
            REQUIRE(msg.header.session_id == session_id);
            REQUIRE(msg.response_code == datatypes::ResponseCode::OK);
        }
    }
}
