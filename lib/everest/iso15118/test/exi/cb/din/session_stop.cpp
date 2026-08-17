#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <iso15118/message/din/session_stop.hpp>
#include <iso15118/message/din/variant.hpp>

#include "helper.hpp"

using namespace iso15118;
namespace dt = din::msg::data_types;

SCENARIO("Ser/Deserialize din session stop messages") {
    GIVEN("Deserialize session stop req") {

        uint8_t doc_raw[] = {0x80, 0x9A, 0x02, 0x00, 0xB6, 0xC8, 0x81, 0xCE, 0xC2, 0x13, 0x4B, 0x51, 0xF0};

        const io::StreamInputView stream_view{doc_raw, sizeof(doc_raw)};

        din::msg::Variant variant(io::v2gtp::PayloadType::SAP, stream_view, false);

        THEN("It should be deserialized successfully") {
            REQUIRE(variant.get_type() == din::msg::Type::SessionStopReq);

            const auto& msg = variant.get<din::msg::SessionStopRequest>();
            const auto& header = msg.header;

            REQUIRE(header.session_id == std::array<uint8_t, 8>{0x02, 0xDB, 0x22, 0x07, 0x3B, 0x08, 0x4D, 0x2D});
        }
    }
    GIVEN("Serialize session stop res") {

        const auto header = din::msg::Header{{0x02, 0xDB, 0x22, 0x07, 0x3B, 0x08, 0x4D, 0x2D}, std::nullopt};

        const auto res = din::msg::SessionStopResponse{
            header,
            din::msg::data_types::ResponseCode::OK,
        };

        std::vector<uint8_t> expected = {0x80, 0x9A, 0x02, 0x00, 0xB6, 0xC8, 0x81,
                                         0xCE, 0xC2, 0x13, 0x4B, 0x52, 0x00, 0x00};

        THEN("It should be serialized successfully") {
            REQUIRE(serialize_helper(res) == expected);
        }
    }
}
