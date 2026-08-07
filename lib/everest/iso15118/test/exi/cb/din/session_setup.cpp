// SPDX-License-Identifier: Apache-2.0
// Copyright 2024 Pionix GmbH and Contributors to EVerest
#include <catch2/catch_test_macros.hpp>

#include <iso15118/message_din/session_setup.hpp>
#include <iso15118/message_din/variant.hpp>

#include <cbv2g/din/din_msgDefDatatypes.h>
#include <cbv2g/din/din_msgDefEncoder.h>

#include "helper.hpp"

using namespace iso15118;
using namespace iso15118::message_din;

SCENARIO("Se/Deserialize DIN session setup messages") {

    const datatypes::SessionId session_id = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};

    GIVEN("Serialize and deserialize session_setup_req") {
        SessionSetupRequest req;
        req.header.session_id = session_id;
        req.evcc_id = {0x00, 0x11, 0x22, 0x33, 0x44, 0x55};

        const auto bytes = serialize_helper(req);

        // independent known-good byte cross-check using the raw cbv2g C API
        din_exiDocument doc;
        init_din_exiDocument(&doc);
        init_din_BodyType(&doc.V2G_Message.Body);
        init_din_MessageHeaderType(&doc.V2G_Message.Header);
        std::copy(session_id.begin(), session_id.end(), doc.V2G_Message.Header.SessionID.bytes);
        doc.V2G_Message.Header.SessionID.bytesLen = 8;
        doc.V2G_Message.Body.SessionSetupReq_isUsed = 1;
        init_din_SessionSetupReqType(&doc.V2G_Message.Body.SessionSetupReq);
        const uint8_t evcc[] = {0x00, 0x11, 0x22, 0x33, 0x44, 0x55};
        std::copy(std::begin(evcc), std::end(evcc), doc.V2G_Message.Body.SessionSetupReq.EVCCID.bytes);
        doc.V2G_Message.Body.SessionSetupReq.EVCCID.bytesLen = sizeof(evcc);

        uint8_t raw_buffer[256];
        exi_bitstream_t raw_stream;
        size_t pos = 0;
        exi_bitstream_init(&raw_stream, raw_buffer, sizeof(raw_buffer), pos, nullptr);
        REQUIRE(encode_din_exiDocument(&raw_stream, &doc) == 0);
        const std::vector<uint8_t> expected(raw_buffer, raw_buffer + exi_bitstream_get_length(&raw_stream));

        THEN("The serialized bytes match the raw cbv2g encode") {
            REQUIRE(bytes == expected);
        }

        THEN("It round-trips through the Variant") {
            const io::StreamInputView view{bytes.data(), bytes.size()};
            message_din::Variant variant(view);

            REQUIRE(variant.get_type() == Type::SessionSetupReq);
            const auto& msg = variant.get<SessionSetupRequest>();
            REQUIRE(msg.header.session_id == session_id);
            REQUIRE(msg.evcc_id == req.evcc_id);
        }
    }

    GIVEN("Serialize and deserialize session_setup_res") {
        SessionSetupResponse res;
        res.header.session_id = {0x2E, 0xFA, 0x18, 0x94, 0xDC, 0x7B, 0x90, 0x11};
        res.response_code = datatypes::ResponseCode::OK_NewSessionEstablished;
        res.evse_id = {0x01, 0x02, 0x03, 0x04};
        res.datetime_now = 1739635913;

        const auto bytes = serialize_helper(res);

        THEN("It round-trips through the Variant") {
            const io::StreamInputView view{bytes.data(), bytes.size()};
            message_din::Variant variant(view);

            REQUIRE(variant.get_type() == Type::SessionSetupRes);
            const auto& msg = variant.get<SessionSetupResponse>();
            REQUIRE(msg.header.session_id == res.header.session_id);
            REQUIRE(msg.response_code == datatypes::ResponseCode::OK_NewSessionEstablished);
            REQUIRE(msg.evse_id == res.evse_id);
            REQUIRE(msg.datetime_now.has_value());
            REQUIRE(msg.datetime_now.value() == 1739635913);
        }
    }
}
