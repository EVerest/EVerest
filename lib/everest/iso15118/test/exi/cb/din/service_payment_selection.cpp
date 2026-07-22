#include <catch2/catch_test_macros.hpp>

#include <iso15118/message/din/service_payment_selection.hpp>
#include <iso15118/message/din/variant.hpp>

#include "helper.hpp"

using namespace iso15118;
namespace dt = din::msg::data_types;

SCENARIO("Ser/Deserialize din service payment selection messages") {
    GIVEN("Deserialize service payment selection req") {
        uint8_t doc_raw[] = {0x80, 0x9A, 0x02, 0x00, 0xB6, 0xC8, 0x81, 0xCE, 0xC2,
                             0x13, 0x4B, 0x51, 0xB2, 0x00, 0xA0, 0x02, 0x88};

        const io::StreamInputView stream_view{doc_raw, sizeof(doc_raw)};

        din::msg::Variant variant(io::v2gtp::PayloadType::SAP, stream_view, false);

        THEN("It should be deserialized successfully") {
            REQUIRE(variant.get_type() == din::msg::Type::ServicePaymentSelectionReq);

            const auto& msg = variant.get<din::msg::ServicePaymentSelectionRequest>();
            const auto& header = msg.header;

            REQUIRE(header.session_id == std::array<uint8_t, 8>{0x02, 0xDB, 0x22, 0x07, 0x3B, 0x08, 0x4D, 0x2D});

            REQUIRE(msg.selected_payment_option == dt::PaymentOption::ExternalPayment);
            REQUIRE(msg.selected_service_list.size() == 1);
            const auto& service = msg.selected_service_list.at(0);
            REQUIRE(service.service_id == 10);
        }
    }
    GIVEN("Serialize service payment selection res") {

        const auto header = din::msg::Header{{0x02, 0xDB, 0x22, 0x07, 0x3B, 0x08, 0x4D, 0x2D}, std::nullopt};

        auto res = din::msg::ServicePaymentSelectionResponse{};
        res.header = header;
        res.response_code = dt::ResponseCode::OK;

        std::vector<uint8_t> expected = {0x80, 0x9A, 0x02, 0x00, 0xB6, 0xC8, 0x81,
                                         0xCE, 0xC2, 0x13, 0x4B, 0x51, 0xC0, 0x00};

        THEN("It should be serialized successfully") {
            REQUIRE(serialize_helper(res) == expected);
        }
    }
}