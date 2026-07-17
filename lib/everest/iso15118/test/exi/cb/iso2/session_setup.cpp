// SPDX-License-Identifier: Apache-2.0
// Copyright 2024 Pionix GmbH and Contributors to EVerest
#include <catch2/catch_test_macros.hpp>

#include <iso15118/message_2/session_setup.hpp>
#include <iso15118/message_2/variant.hpp>

#include <cbv2g/iso_2/iso2_msgDefDatatypes.h>
#include <cbv2g/iso_2/iso2_msgDefDecoder.h>

#include "helper.hpp"

using namespace iso15118;

SCENARIO("Se/Deserialize ISO-2 session setup messages") {

    GIVEN("Round-trip session_setup_req") {
        message_2::SessionSetupRequest req;
        req.header.session_id = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
        req.evcc_id = {0x00, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E};

        const auto serialized = serialize_helper(req);

        const io::StreamInputView stream_view{serialized.data(), serialized.size()};
        message_2::Variant variant(stream_view);

        THEN("It should be deserialized successfully") {
            REQUIRE(variant.get_type() == message_2::Type::SessionSetupReq);
            const auto& msg = variant.get<message_2::SessionSetupRequest>();
            REQUIRE(msg.evcc_id == req.evcc_id);
            REQUIRE(variant.get_session_id() == req.header.session_id);
        }
    }

    GIVEN("Round-trip session_setup_res") {
        message_2::SessionSetupResponse res;
        res.header.session_id = {0x2E, 0xFA, 0x18, 0x94, 0xDC, 0x7B, 0x90, 0x11};
        res.response_code = message_2::datatypes::ResponseCode::OK_NewSessionEstablished;
        res.evse_id = "DE*PNX*E12345*1";
        res.evse_timestamp = 1739635913;

        const auto serialized = serialize_helper(res);

        const io::StreamInputView stream_view{serialized.data(), serialized.size()};
        message_2::Variant variant(stream_view);

        THEN("It should be deserialized successfully") {
            REQUIRE(variant.get_type() == message_2::Type::SessionSetupRes);
            const auto& msg = variant.get<message_2::SessionSetupResponse>();
            REQUIRE(msg.response_code == message_2::datatypes::ResponseCode::OK_NewSessionEstablished);
            REQUIRE(msg.evse_id == "DE*PNX*E12345*1");
            REQUIRE(msg.evse_timestamp.has_value());
            REQUIRE(msg.evse_timestamp.value() == 1739635913);
            REQUIRE(variant.get_session_id() == res.header.session_id);
        }
    }

    GIVEN("Independent cross-check against raw cbv2g C API for session_setup_req") {
        message_2::SessionSetupRequest req;
        req.header.session_id = {0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88};
        req.evcc_id = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06};

        const auto serialized = serialize_helper(req);

        exi_bitstream_t stream;
        // NOLINTNEXTLINE(cppcoreguidelines-pro-type-const-cast)
        exi_bitstream_init(&stream, const_cast<uint8_t*>(serialized.data()), serialized.size(), 0, nullptr);

        iso2_exiDocument doc;
        const auto status = decode_iso2_exiDocument(&stream, &doc);

        THEN("The raw decoder should produce identical fields") {
            REQUIRE(status == 0);
            REQUIRE(doc.V2G_Message.Body.SessionSetupReq_isUsed);
            REQUIRE(doc.V2G_Message.Body.SessionSetupReq.EVCCID.bytesLen == 6);
            REQUIRE(doc.V2G_Message.Body.SessionSetupReq.EVCCID.bytes[0] == 0x01);
            REQUIRE(doc.V2G_Message.Body.SessionSetupReq.EVCCID.bytes[5] == 0x06);
            REQUIRE(doc.V2G_Message.Header.SessionID.bytes[0] == 0x11);
            REQUIRE(doc.V2G_Message.Header.SessionID.bytes[7] == 0x88);
        }
    }
}
