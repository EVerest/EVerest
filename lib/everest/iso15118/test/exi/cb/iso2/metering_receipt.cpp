// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#include <catch2/catch_test_macros.hpp>

#include <iso15118/message_2/metering_receipt.hpp>
#include <iso15118/message_2/variant.hpp>

#include "helper.hpp"

using namespace iso15118;
using namespace iso15118::message_2::datatypes;

SCENARIO("Se/Deserialize ISO-2 metering receipt messages") {

    const SessionId session_id = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08};

    GIVEN("Round-trip metering_receipt_req with every MeterInfo element") {
        message_2::MeteringReceiptRequest req;
        req.header.session_id = session_id;
        req.session_id = session_id;
        req.sa_schedule_tuple_id = 1;
        req.meter_info.meter_id = "METER01";
        req.meter_info.meter_reading = 123456;
        MeterSignature signature{};
        for (size_t i = 0; i < signature.size(); ++i) {
            signature[i] = static_cast<uint8_t>(i);
        }
        req.meter_info.sig_meter_reading = signature;
        req.meter_info.meter_status = 7;
        req.meter_info.t_meter = 1700000000;

        const auto serialized = serialize_helper(req);
        const io::StreamInputView stream_view{serialized.data(), serialized.size()};
        message_2::Variant variant(stream_view);

        THEN("It should be deserialized successfully") {
            REQUIRE(variant.get_type() == message_2::Type::MeteringReceiptReq);
            REQUIRE(variant.get_session_id() == session_id);
            const auto& msg = variant.get<message_2::MeteringReceiptRequest>();
            REQUIRE(msg.session_id == session_id);
            REQUIRE(msg.sa_schedule_tuple_id.has_value());
            REQUIRE(msg.sa_schedule_tuple_id.value() == 1);
            REQUIRE(msg.meter_info.meter_id == "METER01");
            REQUIRE(msg.meter_info.meter_reading == 123456);
            REQUIRE(msg.meter_info.sig_meter_reading == signature);
            REQUIRE(msg.meter_info.meter_status == 7);
            REQUIRE(msg.meter_info.t_meter == 1700000000);
        }
    }

    GIVEN("Round-trip metering_receipt_req with the mandatory elements only") {
        message_2::MeteringReceiptRequest req;
        req.header.session_id = session_id;
        req.session_id = session_id;
        req.meter_info.meter_id = "METER01";

        const auto serialized = serialize_helper(req);
        const io::StreamInputView stream_view{serialized.data(), serialized.size()};
        message_2::Variant variant(stream_view);

        THEN("The optional elements stay absent") {
            REQUIRE(variant.get_type() == message_2::Type::MeteringReceiptReq);
            const auto& msg = variant.get<message_2::MeteringReceiptRequest>();
            REQUIRE_FALSE(msg.sa_schedule_tuple_id.has_value());
            REQUIRE_FALSE(msg.meter_info.meter_reading.has_value());
            REQUIRE_FALSE(msg.meter_info.sig_meter_reading.has_value());
            REQUIRE_FALSE(msg.meter_info.meter_status.has_value());
            REQUIRE_FALSE(msg.meter_info.t_meter.has_value());
        }
    }

    GIVEN("Round-trip metering_receipt_res (AC)") {
        message_2::MeteringReceiptResponse res;
        res.header.session_id = session_id;
        res.response_code = ResponseCode::OK;
        res.ac_evse_status = AC_EVSEStatus{0, EVSENotification::None, false};

        const auto serialized = serialize_helper(res);
        const auto doc = decode_helper(serialized);

        THEN("The encoded response converts back field for field") {
            REQUIRE(doc.V2G_Message.Body.MeteringReceiptRes_isUsed);
            const auto msg =
                to_response<message_2::MeteringReceiptResponse>(doc, doc.V2G_Message.Body.MeteringReceiptRes);
            REQUIRE(msg.response_code == ResponseCode::OK);
            REQUIRE(msg.ac_evse_status.has_value());
            REQUIRE_FALSE(msg.dc_evse_status.has_value());
            REQUIRE(msg.ac_evse_status->rcd == false);
        }
    }

    GIVEN("Round-trip metering_receipt_res (DC)") {
        message_2::MeteringReceiptResponse res;
        res.header.session_id = session_id;
        res.response_code = ResponseCode::FAILED_MeteringSignatureNotValid;
        res.dc_evse_status =
            DC_EVSEStatus{0, EVSENotification::None, IsolationLevel::Valid, DC_EVSEStatusCode::EVSE_Ready};

        const auto serialized = serialize_helper(res);
        const auto doc = decode_helper(serialized);

        THEN("The encoded response converts back field for field") {
            REQUIRE(doc.V2G_Message.Body.MeteringReceiptRes_isUsed);
            const auto msg =
                to_response<message_2::MeteringReceiptResponse>(doc, doc.V2G_Message.Body.MeteringReceiptRes);
            REQUIRE(msg.response_code == ResponseCode::FAILED_MeteringSignatureNotValid);
            REQUIRE(msg.dc_evse_status.has_value());
            REQUIRE_FALSE(msg.ac_evse_status.has_value());
            REQUIRE(msg.dc_evse_status->isolation_status == IsolationLevel::Valid);
            REQUIRE(msg.dc_evse_status->status_code == DC_EVSEStatusCode::EVSE_Ready);
        }
    }
}
