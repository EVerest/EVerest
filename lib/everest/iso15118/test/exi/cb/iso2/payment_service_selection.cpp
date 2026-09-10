// SPDX-License-Identifier: Apache-2.0
// Copyright 2024 Pionix GmbH and Contributors to EVerest
#include <catch2/catch_test_macros.hpp>

#include <iso15118/message_2/payment_service_selection.hpp>
#include <iso15118/message_2/variant.hpp>

#include "helper.hpp"

using namespace iso15118;
using namespace iso15118::message_2::datatypes;

SCENARIO("Se/Deserialize ISO-2 payment service selection messages") {

    GIVEN("Round-trip payment_service_selection_req") {
        message_2::PaymentServiceSelectionRequest req;
        req.header.session_id = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08};
        req.selected_payment_option = PaymentOption::ExternalPayment;
        auto& service = req.selected_service_list.emplace_back();
        service.service_id = 1;
        service.parameter_set_id = 3;

        const auto serialized = serialize_helper(req);
        const io::StreamInputView stream_view{serialized.data(), serialized.size()};
        message_2::Variant variant(stream_view);

        THEN("It should be deserialized successfully") {
            REQUIRE(variant.get_type() == message_2::Type::PaymentServiceSelectionReq);
            const auto& msg = variant.get<message_2::PaymentServiceSelectionRequest>();
            REQUIRE(msg.selected_payment_option == PaymentOption::ExternalPayment);
            REQUIRE(msg.selected_service_list.size() == 1);
            REQUIRE(msg.selected_service_list[0].service_id == 1);
            REQUIRE(msg.selected_service_list[0].parameter_set_id.has_value());
            REQUIRE(msg.selected_service_list[0].parameter_set_id.value() == 3);
        }
    }

    GIVEN("Round-trip payment_service_selection_res") {
        message_2::PaymentServiceSelectionResponse res;
        res.header.session_id = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08};
        res.response_code = ResponseCode::OK;

        const auto serialized = serialize_helper(res);
        const io::StreamInputView stream_view{serialized.data(), serialized.size()};
        message_2::Variant variant(stream_view);

        THEN("It should be deserialized successfully") {
            REQUIRE(variant.get_type() == message_2::Type::PaymentServiceSelectionRes);
            const auto& msg = variant.get<message_2::PaymentServiceSelectionResponse>();
            REQUIRE(msg.response_code == ResponseCode::OK);
        }
    }
}
