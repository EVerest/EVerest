// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#include <catch2/catch_test_macros.hpp>

#include <iso15118/message_2/metering_receipt.hpp>
#include <iso15118/message_2/variant.hpp>

#include "helper.hpp"

using namespace iso15118;
using namespace iso15118::message_2::datatypes;

SCENARIO("Se/Deserialize ISO-2 metering receipt messages") {

    GIVEN("Round-trip metering_receipt_res (DC)") {
        message_2::MeteringReceiptResponse res;
        res.header.session_id = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08};
        res.response_code = ResponseCode::OK;
        res.dc_evse_status.emplace();

        const auto serialized = serialize_helper(res);
        const io::StreamInputView stream_view{serialized.data(), serialized.size()};
        message_2::Variant variant(stream_view);

        THEN("It should be deserialized successfully") {
            REQUIRE(variant.get_type() == message_2::Type::MeteringReceiptRes);
            const auto& msg = variant.get<message_2::MeteringReceiptResponse>();
            REQUIRE(msg.response_code == ResponseCode::OK);
            REQUIRE(msg.dc_evse_status.has_value());
            REQUIRE_FALSE(msg.ac_evse_status.has_value());
            REQUIRE(msg.header.session_id == res.header.session_id);
        }
    }

    GIVEN("Round-trip metering_receipt_res (AC, FAILED)") {
        message_2::MeteringReceiptResponse res;
        res.header.session_id = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08};
        res.response_code = ResponseCode::FAILED_MeteringSignatureNotValid;
        res.ac_evse_status.emplace();

        const auto serialized = serialize_helper(res);
        const io::StreamInputView stream_view{serialized.data(), serialized.size()};
        message_2::Variant variant(stream_view);

        THEN("It should be deserialized successfully") {
            REQUIRE(variant.get_type() == message_2::Type::MeteringReceiptRes);
            const auto& msg = variant.get<message_2::MeteringReceiptResponse>();
            REQUIRE(msg.response_code == ResponseCode::FAILED_MeteringSignatureNotValid);
            REQUIRE(msg.ac_evse_status.has_value());
        }
    }
}
