#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <iso15118/message/power_delivery.hpp>
#include <iso15118/message/response_variant.hpp>
#include <iso15118/message/variant.hpp>

#include "helper.hpp"

using namespace iso15118;

SCENARIO("Se/Deserialize power delivery messages") {

    GIVEN("Deserialize power_delivery_req") {

        uint8_t doc_raw[] = {0x80, 0x54, 0x04, 0x1e, 0xa6, 0x5f, 0xc9, 0x9b, 0xa7, 0x6c, 0x4d, 0x8c, 0xdb, 0xfe,
                             0x1b, 0x60, 0x62, 0x00, 0x00, 0x01, 0x00, 0x42, 0x00, 0xb8, 0x41, 0x00, 0x51, 0x24};

        const io::StreamInputView stream_view{doc_raw, sizeof(doc_raw)};

        message_20::RequestVariant variant(v2gtp::PayloadType::Part20Main, stream_view);

        THEN("It should be deserialized successfully") {
            REQUIRE(variant.get_type() == message_20::Type::PowerDeliveryReq);

            const auto& msg = variant.get<message_20::PowerDeliveryRequest>();
            const auto& header = msg.header;

            REQUIRE(header.session_id == std::array<uint8_t, 8>{0x3D, 0x4C, 0xBF, 0x93, 0x37, 0x4E, 0xD8, 0x9B});
            REQUIRE(header.timestamp == 1725456333);
            REQUIRE(msg.processing == message_20::datatypes::Processing::Finished);
            REQUIRE(msg.charge_progress == message_20::datatypes::Progress::Start);

            REQUIRE(msg.power_profile.has_value() == true);
            auto& power_profile = msg.power_profile.value();
            REQUIRE(power_profile.time_anchor == 0);
            REQUIRE(power_profile.entries[0].duration == 23);
            REQUIRE(message_20::datatypes::from_RationalNumber(power_profile.entries[0].power) == 1000);

            REQUIRE(
                std::holds_alternative<message_20::datatypes::Scheduled_EVPPTControlMode>(power_profile.control_mode));
            const auto& mode = std::get<message_20::datatypes::Scheduled_EVPPTControlMode>(power_profile.control_mode);
            REQUIRE(mode.power_tolerance_acceptance == message_20::datatypes::PowerToleranceAcceptance::Confirmed);
            REQUIRE(mode.selected_schedule == 1);
        }
    }

    GIVEN("Serialize power_delivery_req") {

        message_20::PowerDeliveryRequest req;

        req.header = message_20::Header{{0x3D, 0x4C, 0xBF, 0x93, 0x37, 0x4E, 0xD8, 0x9B}, 1725456333};
        req.processing = message_20::datatypes::Processing::Finished;
        req.charge_progress = message_20::datatypes::Progress::Start;
        req.power_profile = message_20::datatypes::PowerProfile{};
        req.power_profile->time_anchor = 0;
        req.power_profile->entries.emplace_back();
        req.power_profile->entries[0].duration = 23;
        req.power_profile->entries[0].power = message_20::datatypes::RationalNumber{10, 2};
        req.power_profile->control_mode.emplace<message_20::datatypes::Scheduled_EVPPTControlMode>();
        auto& mode = std::get<message_20::datatypes::Scheduled_EVPPTControlMode>(req.power_profile->control_mode);
        mode.power_tolerance_acceptance = message_20::datatypes::PowerToleranceAcceptance::Confirmed;
        mode.selected_schedule = 1;

        std::vector<uint8_t> expected = {0x80, 0x54, 0x04, 0x1e, 0xa6, 0x5f, 0xc9, 0x9b, 0xa7, 0x6c,
                                         0x4d, 0x8c, 0xdb, 0xfe, 0x1b, 0x60, 0x62, 0x00, 0x00, 0x01,
                                         0x00, 0x42, 0x00, 0xb8, 0x41, 0x00, 0x51, 0x24};

        THEN("It should be serialized successfully") {
            REQUIRE(serialize_helper(req) == expected);
        }
    }

    GIVEN("Round-trip a power_delivery_req profile in either control mode on every progress the EV sends") {
        namespace dt = message_20::datatypes;
        const auto progress = GENERATE(dt::Progress::Start, dt::Progress::Stop, dt::Progress::Standby);
        const bool scheduled = GENERATE(false, true);

        message_20::PowerDeliveryRequest req;
        req.header = message_20::Header{{0x3D, 0x4C, 0xBF, 0x93, 0x37, 0x4E, 0xD8, 0x9B}, 1725456333};
        req.processing = dt::Processing::Finished;
        req.charge_progress = progress;
        auto& profile = req.power_profile.emplace();
        profile.time_anchor = 5000000;
        if (scheduled) {
            profile.control_mode = dt::Scheduled_EVPPTControlMode{3, std::nullopt};
        } else {
            profile.control_mode = dt::Dynamic_EVPPTControlMode{};
        }
        profile.entries.push_back({86400, dt::from_float(0.0f), dt::from_float(0.0f), dt::from_float(0.0f)});

        const auto raw = serialize_helper(req);
        const io::StreamInputView stream_view{raw.data(), raw.size()};
        message_20::RequestVariant variant(v2gtp::PayloadType::Part20Main, stream_view);

        THEN("It decodes to the same profile") {
            REQUIRE(variant.get_type() == message_20::Type::PowerDeliveryReq);
            const auto& msg = variant.get<message_20::PowerDeliveryRequest>();
            REQUIRE(msg.charge_progress == progress);
            REQUIRE(msg.power_profile.has_value());
            REQUIRE(msg.power_profile->time_anchor == 5000000);
            if (scheduled) {
                const auto* mode = std::get_if<dt::Scheduled_EVPPTControlMode>(&msg.power_profile->control_mode);
                REQUIRE(mode != nullptr);
                REQUIRE(mode->selected_schedule == 3);
                REQUIRE_FALSE(mode->power_tolerance_acceptance.has_value());
            } else {
                REQUIRE(std::holds_alternative<dt::Dynamic_EVPPTControlMode>(msg.power_profile->control_mode));
            }
            REQUIRE(msg.power_profile->entries.size() == 1);
            const auto& entry = msg.power_profile->entries[0];
            REQUIRE(entry.duration == 86400);
            REQUIRE(dt::from_RationalNumber(entry.power) == 0.0f);
            REQUIRE(entry.power_l2.has_value());
            REQUIRE(dt::from_RationalNumber(*entry.power_l2) == 0.0f);
            REQUIRE(entry.power_l3.has_value());
            REQUIRE(dt::from_RationalNumber(*entry.power_l3) == 0.0f);
        }
    }

    GIVEN("Serialize power_delivery_res") {

        message_20::PowerDeliveryResponse res;

        res.header = message_20::Header{{0x3D, 0x4C, 0xBF, 0x93, 0x37, 0x4E, 0xD8, 0x9B}, 1725456333};
        res.response_code = message_20::datatypes::ResponseCode::OK;

        std::vector<uint8_t> expected = {0x80, 0x58, 0x04, 0x1e, 0xa6, 0x5f, 0xc9, 0x9b, 0xa7, 0x6c,
                                         0x4d, 0x8c, 0xdb, 0xfe, 0x1b, 0x60, 0x62, 0x00, 0x40};

        THEN("It should be serialized successfully") {
            REQUIRE(serialize_helper(res) == expected);
        }
    }

    GIVEN("Deserialize power_delivery_res") {

        uint8_t doc_raw[] = {0x80, 0x58, 0x04, 0x1e, 0xa6, 0x5f, 0xc9, 0x9b, 0xa7, 0x6c,
                             0x4d, 0x8c, 0xdb, 0xfe, 0x1b, 0x60, 0x62, 0x00, 0x40};

        const io::StreamInputView stream_view{doc_raw, sizeof(doc_raw)};

        message_20::ResponseVariant variant(v2gtp::PayloadType::Part20Main, stream_view);

        THEN("It should be deserialized successfully") {
            REQUIRE(variant.get_type() == message_20::Type::PowerDeliveryRes);

            const auto& msg = variant.get<message_20::PowerDeliveryResponse>();
            const auto& header = msg.header;

            REQUIRE(header.session_id == std::array<uint8_t, 8>{0x3D, 0x4C, 0xBF, 0x93, 0x37, 0x4E, 0xD8, 0x9B});
            REQUIRE(header.timestamp == 1725456333);
            REQUIRE(msg.response_code == message_20::datatypes::ResponseCode::OK);
        }
    }
}
