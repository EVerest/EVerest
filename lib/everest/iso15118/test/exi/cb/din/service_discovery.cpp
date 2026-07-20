#include <catch2/catch_test_macros.hpp>

#include <iso15118/message/din/service_discovery.hpp>
#include <iso15118/message/din/variant.hpp>

#include "helper.hpp"

#include <cstdint>
#include <optional>

using namespace iso15118;
namespace dt = din::msg::data_types;

SCENARIO("Ser/Deserialize din service discovery messages") {
    GIVEN("Deserialize service discovery req") {
        // TODO(kd): I have put incorrect exi stream as the doc_raw and the code was randomly failing when trying to
        // decode the stream. When ran through valgrind it would always fail. The random failures could indicate a bug
        // in libcbv2g. For more details see PR #2449
        uint8_t doc_raw[] = {0x80, 0x9A, 0x02, 0x00, 0xB6, 0xC8, 0x81, 0xCE, 0xC2, 0x13,
                             0x4B, 0x51, 0x90, 0x0A, 0xCC, 0xDE, 0xDE, 0x08, 0x00};

        const io::StreamInputView stream_view{doc_raw, sizeof(doc_raw)};

        din::msg::Variant variant(io::v2gtp::PayloadType::SAP, stream_view, false);

        THEN("It should be deserialized successfully") {
            REQUIRE(variant.get_type() == din::msg::Type::ServiceDiscoveryReq);

            const auto& msg = variant.get<din::msg::ServiceDiscoveryRequest>();
            const auto& header = msg.header;

            REQUIRE(header.session_id == std::array<uint8_t, 8>{0x02, 0xDB, 0x22, 0x07, 0x3B, 0x08, 0x4D, 0x2D});

            REQUIRE(msg.service_scope.has_value());
            REQUIRE(msg.service_scope.value() == "foo");
            REQUIRE(msg.service_category.has_value());
            REQUIRE(msg.service_category.value() == dt::ServiceCategory::Internet);
        }
    }
    GIVEN("Serialize service discovery res") {

        const auto header = din::msg::Header{{0x02, 0xDB, 0x22, 0x07, 0x3B, 0x08, 0x4D, 0x2D}, std::nullopt};

        auto res = din::msg::ServiceDiscoveryResponse{};
        res.header = header;
        res.response_code = dt::ResponseCode::OK;
        res.payment_option_list = {
            dt::PaymentOption::ExternalPayment,
        };
        res.charge_service.service_tag.id = 1;
        res.charge_service.service_tag.category = dt::ServiceCategory::EVCharging;
        res.charge_service.energy_transfer_type = dt::EvseSupportedEnergyTransfer::DC_Core;
        res.charge_service.free_service = true;
        dt::Service service;
        service.service_tag.id = 10;
        service.service_tag.category = dt::ServiceCategory::OtherCustom;
        service.free_service = true;
        res.service_list.emplace({service});

        std::vector<uint8_t> expected = {0x80, 0x9A, 0x02, 0x00, 0xB6, 0xC8, 0x81, 0xCE, 0xC2, 0x13, 0x4B, 0x51,
                                         0xA0, 0x01, 0x20, 0x02, 0x41, 0x20, 0x80, 0x02, 0x8B, 0x24, 0x40};

        THEN("It should be serialized successfully") {
            REQUIRE(serialize_helper(res) == expected);
        }
    }
}