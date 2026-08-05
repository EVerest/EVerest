#include <catch2/catch_test_macros.hpp>

#include <iso15118/message/din/session_setup.hpp>
#include <iso15118/message/din/variant.hpp>

#include "helper.hpp"

#include <cstdint>
#include <optional>

using namespace iso15118;
namespace dt = din::msg::data_types;

SCENARIO("Ser/Deserialize din session setup messages") {
    GIVEN("Deserialize session setup req") {
        uint8_t doc_raw[] = {0x80, 0x9A, 0x02, 0x00, 0xB6, 0xC8, 0x81, 0xCE, 0xC2, 0x13, 0x4B,
                             0x51, 0xD0, 0x1A, 0xAA, 0xAA, 0xAA, 0xAA, 0xAA, 0xA8, 0x00};

        const io::StreamInputView stream_view{doc_raw, sizeof(doc_raw)};

        din::msg::Variant variant(io::v2gtp::PayloadType::SAP, stream_view, false);

        THEN("It should be deserialized successfully") {
            REQUIRE(variant.get_type() == din::msg::Type::SessionSetupReq);

            const auto& msg = variant.get<din::msg::SessionSetupRequest>();
            const auto& header = msg.header;

            REQUIRE(header.session_id == std::array<uint8_t, 8>{0x02, 0xDB, 0x22, 0x07, 0x3B, 0x08, 0x4D, 0x2D});

            REQUIRE(msg.evcc_id == dt::EVCCID{0xAA, 0xAA, 0xAA, 0xAA, 0xAA, 0xAA});
        }
    }
    GIVEN("Serialize session setup res") {

        const auto header = din::msg::Header{{0x02, 0xDB, 0x22, 0x07, 0x3B, 0x08, 0x4D, 0x2D}, std::nullopt};

        auto res = din::msg::SessionSetupResponse{};
        res.header = header;
        res.response_code = dt::ResponseCode::OK;
        res.date_time_now = 100;
        res.evse_id = dt::EVSEID{'m', 'y', '_', 'e', 'v', 's', 'e', '\0'};

        std::vector<uint8_t> expected = {0x80, 0x9A, 0x02, 0x00, 0xB6, 0xC8, 0x81, 0xCE, 0xC2, 0x13, 0x4B, 0x51, 0xE0,
                                         0x00, 0x21, 0xB5, 0xE5, 0x7D, 0x95, 0xD9, 0xCD, 0x94, 0x00, 0x0C, 0x80};

        THEN("It should be serialized successfully") {
            REQUIRE(serialize_helper(res) == expected);
        }
    }
}

