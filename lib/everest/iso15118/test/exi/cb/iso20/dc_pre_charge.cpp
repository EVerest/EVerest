#include <catch2/catch_test_macros.hpp>

#include <iso15118/message/dc_pre_charge.hpp>
#include <iso15118/message/variant.hpp>

#include "helper.hpp"

using namespace iso15118;

SCENARIO("Se/Deserialize dc pre charge messages") {

    GIVEN("Deserialize dc_pre_charge_req") {

        uint8_t doc_raw[] = {0x80, 0x44, 0x04, 0x1e, 0xa6, 0x5f, 0xc9, 0x9b, 0xa7, 0x6c, 0x4d, 0x8c, 0xbb,
                             0xfe, 0x1b, 0x60, 0x62, 0x21, 0x00, 0x12, 0x00, 0x60, 0x80, 0x09, 0x00, 0x30};

        const io::StreamInputView stream_view{doc_raw, sizeof(doc_raw)};

        msg::d20::Variant variant(io::v2gtp::PayloadType::Part20DC, stream_view);

        THEN("It should be deserialized successfully") {
            REQUIRE(variant.get_type() == msg::d20::Type::DC_PreChargeReq);

            const auto& msg = variant.get<msg::d20::DC_PreChargeRequest>();
            const auto& header = msg.header;

            REQUIRE(header.session_id == std::array<uint8_t, 8>{0x3D, 0x4C, 0xBF, 0x93, 0x37, 0x4E, 0xD8, 0x9B});
            REQUIRE(header.timestamp == 1725456331);
            REQUIRE(msg.processing == msg::d20::datatypes::Processing::Ongoing);
            REQUIRE(msg::d20::datatypes::from_RationalNumber(msg.present_voltage) == 400);
            REQUIRE(msg::d20::datatypes::from_RationalNumber(msg.target_voltage) == 400);
        }
    }

    GIVEN("Serialize dc_pre_charge_req") {

        msg::d20::DC_PreChargeRequest req;

        req.header = msg::d20::Header{{0x3D, 0x4C, 0xBF, 0x93, 0x37, 0x4E, 0xD8, 0x9B}, 1725456331};
        req.processing = msg::d20::datatypes::Processing::Ongoing;
        req.target_voltage = {400, 0};
        req.present_voltage = {400, 0};

        std::vector<uint8_t> expected = {0x80, 0x44, 0x04, 0x1e, 0xa6, 0x5f, 0xc9, 0x9b, 0xa7, 0x6c, 0x4d, 0x8c, 0xbb,
                                         0xfe, 0x1b, 0x60, 0x62, 0x21, 0x00, 0x12, 0x00, 0x60, 0x80, 0x09, 0x00, 0x30};

        THEN("It should be serialized successfully") {
            REQUIRE(serialize_helper(req) == expected);
        }
    }

    GIVEN("Serialize dc_pre_charge_res") {

        msg::d20::DC_PreChargeResponse res;

        res.header = msg::d20::Header{{0x3D, 0x4C, 0xBF, 0x93, 0x37, 0x4E, 0xD8, 0x9B}, 1725456332};
        res.response_code = msg::d20::datatypes::ResponseCode::OK;
        res.present_voltage = {4000, -1};

        std::vector<uint8_t> expected = {0x80, 0x48, 0x04, 0x1e, 0xa6, 0x5f, 0xc9, 0x9b, 0xa7, 0x6c, 0x4d, 0x8c,
                                         0xcb, 0xfe, 0x1b, 0x60, 0x62, 0x00, 0x0f, 0xe1, 0x40, 0x3e, 0x00};

        THEN("It should be serialized successfully") {
            REQUIRE(serialize_helper(res) == expected);
        }
    }

    GIVEN("Deserialize dc_pre_charge_res") {

        uint8_t doc_raw[] = {0x80, 0x48, 0x04, 0x1e, 0xa6, 0x5f, 0xc9, 0x9b, 0xa7, 0x6c, 0x4d, 0x8c,
                             0xcb, 0xfe, 0x1b, 0x60, 0x62, 0x00, 0x0f, 0xe1, 0x40, 0x3e, 0x00};

        const io::StreamInputView stream_view{doc_raw, sizeof(doc_raw)};

        msg::d20::Variant variant(io::v2gtp::PayloadType::Part20DC, stream_view);

        THEN("It should be deserialized successfully") {
            REQUIRE(variant.get_type() == msg::d20::Type::DC_PreChargeRes);

            const auto& msg = variant.get<msg::d20::DC_PreChargeResponse>();
            const auto& header = msg.header;

            REQUIRE(header.session_id == std::array<uint8_t, 8>{0x3D, 0x4C, 0xBF, 0x93, 0x37, 0x4E, 0xD8, 0x9B});
            REQUIRE(header.timestamp == 1725456332);
            REQUIRE(msg.response_code == msg::d20::datatypes::ResponseCode::OK);
            REQUIRE(msg::d20::datatypes::from_RationalNumber(msg.present_voltage) == 400);
        }
    }
}
