#include <catch2/catch_test_macros.hpp>

#include <iso15118/message/din/contract_authentication.hpp>
#include <iso15118/message/din/variant.hpp>

#include "helper.hpp"

using namespace iso15118;
namespace dt = din::msg::data_types;

SCENARIO("Ser/Deserialize din contract authentication messages") {
    GIVEN("Deserialize contract authentication req") {
        uint8_t doc_raw[] = {0x80, 0x9A, 0x02, 0x00, 0xB6, 0xC8, 0x81, 0xCE, 0xC2, 0x13, 0x4B, 0x50,
                             0xB0, 0x19, 0xB5, 0xE5, 0xA5, 0x90, 0x02, 0xB3, 0x37, 0xB7, 0x80};

        const io::StreamInputView stream_view{doc_raw, sizeof(doc_raw)};

        din::msg::Variant variant(io::v2gtp::PayloadType::SAP, stream_view, false);

        THEN("It should be deserialized successfully") {
            REQUIRE(variant.get_type() == din::msg::Type::ContractAuthenticationReq);

            const auto& msg = variant.get<din::msg::ContractAuthenticationRequest>();
            const auto& header = msg.header;

            REQUIRE(header.session_id == std::array<uint8_t, 8>{0x02, 0xDB, 0x22, 0x07, 0x3B, 0x08, 0x4D, 0x2D});

            REQUIRE(msg.id.has_value());
            REQUIRE(msg.id.value() == "myid");
            REQUIRE(msg.gen_challenge.has_value());
            REQUIRE(msg.gen_challenge.value() == "foo");
        }
    }
    GIVEN("Serialize contract authentication res") {

        const auto header = din::msg::Header{{0x02, 0xDB, 0x22, 0x07, 0x3B, 0x08, 0x4D, 0x2D}, std::nullopt};

        auto res = din::msg::ContractAuthenticationResponse{};
        res.header = header;
        res.response_code = dt::ResponseCode::OK;
        res.evse_processing = dt::EvseProcessing::Finished;

        std::vector<uint8_t> expected = {0x80, 0x9A, 0x02, 0x00, 0xB6, 0xC8, 0x81, 0xCE,
                                         0xC2, 0x13, 0x4B, 0x50, 0xC0, 0x00, 0x00};

        THEN("It should be serialized successfully") {
            REQUIRE(serialize_helper(res) == expected);
        }
    }
}