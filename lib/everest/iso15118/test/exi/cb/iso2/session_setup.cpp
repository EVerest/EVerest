#include <catch2/catch_test_macros.hpp>

#include <iso15118/message/d2/session_setup.hpp>
#include <iso15118/message/d2/variant.hpp>

#include "helper.hpp"

using namespace iso15118;

SCENARIO("Ser/Deserialize d2 session setup messages") {
    GIVEN("Deserialize session setup req") {
        uint8_t doc_raw[] = {0x80, 0x98, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                             0x11, 0xd0, 0x18, 0x4b, 0x88, 0xf8, 0x43, 0x4d, 0x20, 0x00};

        const io::StreamInputView stream_view{doc_raw, sizeof(doc_raw)};

        msg::d2::Variant variant(stream_view);

        THEN("It should be deserialized successfully") {
            REQUIRE(variant.get_type() == msg::d2::Type::SessionSetupReq);

            const auto* msg = variant.get_if<msg::d2::SessionSetupRequest>();
            const auto& header = msg->header;

            REQUIRE(header.session_id == std::array<uint8_t, 8>{0});

            REQUIRE(msg->evcc_id == std::array<uint8_t, 6>{0x12, 0xE2, 0x3E, 0x10, 0xD3, 0x48});
        }
    }
    GIVEN("Serialize session_setup_res") {

        const auto header = msg::d2::Header{{0x06, 0xD0, 0x7F, 0xBF, 0x17, 0x4B, 0x5E, 0xFF}, std::nullopt};

        const auto res = msg::d2::SessionSetupResponse{
            header, msg::d2::data_types::ResponseCode::OK_NewSessionEstablished, "DE*PNX*E12345*1", std::nullopt};

        std::vector<uint8_t> expected = {0x80, 0x98, 0x02, 0x01, 0xB4, 0x1F, 0xEF, 0xC5, 0xD2, 0xD7, 0xBF,
                                         0xD1, 0xE0, 0x20, 0x45, 0x11, 0x14, 0xA9, 0x41, 0x39, 0x60, 0xA9,
                                         0x14, 0xC4, 0xC8, 0xCC, 0xD0, 0xD4, 0xA8, 0xC4, 0x80};

        THEN("It should be serialized successfully") {
            REQUIRE(serialize_helper(res) == expected); // FIXME: Failing right now
        }
    }
}
