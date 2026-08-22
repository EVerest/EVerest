// SPDX-License-Identifier: Apache-2.0
// Copyright 2024 Pionix GmbH and Contributors to EVerest
#include <catch2/catch_test_macros.hpp>

#include <iso15118/message_2/service_detail.hpp>
#include <iso15118/message_2/variant.hpp>

#include "helper.hpp"

using namespace iso15118;
using namespace iso15118::message_2::datatypes;

SCENARIO("Se/Deserialize ISO-2 service detail messages") {

    GIVEN("Round-trip service_detail_req") {
        message_2::ServiceDetailRequest req;
        req.header.session_id = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08};
        req.service_id = 42;

        const auto serialized = serialize_helper(req);
        const io::StreamInputView stream_view{serialized.data(), serialized.size()};
        message_2::Variant variant(stream_view);

        THEN("It should be deserialized successfully") {
            REQUIRE(variant.get_type() == message_2::Type::ServiceDetailReq);
            const auto& msg = variant.get<message_2::ServiceDetailRequest>();
            REQUIRE(msg.service_id == 42);
        }
    }

    GIVEN("Round-trip service_detail_res with parameter list") {
        message_2::ServiceDetailResponse res;
        res.header.session_id = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08};
        res.response_code = ResponseCode::OK;
        res.service_id = 42;

        auto& list = res.service_parameter_list.emplace();
        auto& set = list.emplace_back();
        set.parameter_set_id = 7;
        auto& param = set.parameter.emplace_back();
        param.name = "Protocol";
        param.string_value = "http";

        const auto serialized = serialize_helper(res);
        const io::StreamInputView stream_view{serialized.data(), serialized.size()};
        message_2::Variant variant(stream_view);

        THEN("It should be deserialized successfully") {
            REQUIRE(variant.get_type() == message_2::Type::ServiceDetailRes);
            const auto& msg = variant.get<message_2::ServiceDetailResponse>();
            REQUIRE(msg.service_id == 42);
            REQUIRE(msg.service_parameter_list.has_value());
            REQUIRE(msg.service_parameter_list->size() == 1);
            REQUIRE(msg.service_parameter_list->at(0).parameter_set_id == 7);
            REQUIRE(msg.service_parameter_list->at(0).parameter.size() == 1);
            REQUIRE(msg.service_parameter_list->at(0).parameter[0].name == "Protocol");
            REQUIRE(msg.service_parameter_list->at(0).parameter[0].string_value.has_value());
            REQUIRE(msg.service_parameter_list->at(0).parameter[0].string_value.value() == "http");
        }
    }
}
