#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <iso15118/message/ac_der_iec_charge_loop.hpp>
#include <iso15118/message/variant.hpp>

#include "helper.hpp"

using namespace iso15118;

namespace dt = message_20::datatypes;

SCENARIO("Se/Deserialize ac der iec charge loop messages") {

    GIVEN("Deserialize minimum ac_der_charge_loop_req + dynamic mode") {
        uint8_t doc_raw[] = {0x80, 0x08, 0x04, 0x1e, 0xa6, 0x5f, 0xc9, 0x9b, 0xa7, 0x6c, 0x4d, 0x8c, 0x3b, 0xfe,
                             0x1b, 0x60, 0x62, 0x86, 0x90, 0x60, 0x32, 0x08, 0x30, 0x1e, 0x04, 0x18, 0x02, 0x81,
                             0xf8, 0x20, 0x06, 0x48, 0x7e, 0x0d, 0x00, 0xf2, 0x1f, 0x83, 0x70, 0x2c, 0x88, 0x00,
                             0x00, 0x21, 0xf8, 0x20, 0x06, 0x48, 0x7e, 0x0d, 0x00, 0xf2, 0x00, 0x06};

        const io::StreamInputView stream_view{doc_raw, sizeof(doc_raw)};

        message_20::Variant variant(io::v2gtp::PayloadType::Part20DerIec, stream_view);

        THEN("It should be deserialized successfully") {
            REQUIRE(variant.get_type() == message_20::Type::DER_AC_ChargeLoopReq);

            const auto& msg = variant.get<message_20::DER_AC_ChargeLoopRequest>();
            const auto& header = msg.header;

            REQUIRE(header.session_id == std::array<uint8_t, 8>{0x3D, 0x4C, 0xBF, 0x93, 0x37, 0x4E, 0xD8, 0x9B});
            REQUIRE(header.timestamp == 1725456323);
            REQUIRE(msg.meter_info_requested == false);

            using DynamicMode = dt::DER_Dynamic_AC_CLReqControlMode;

            REQUIRE(std::holds_alternative<DynamicMode>(msg.control_mode));
            const auto& control_mode = std::get<DynamicMode>(msg.control_mode);
            REQUIRE(dt::from_RationalNumber(control_mode.target_energy_request) == 25000);
            REQUIRE(dt::from_RationalNumber(control_mode.max_energy_request) == 30000);
            REQUIRE(dt::from_RationalNumber(control_mode.min_energy_request) == 5000);
            REQUIRE(dt::from_RationalNumber(control_mode.max_charge_power) == 32);
            REQUIRE(dt::from_RationalNumber(control_mode.min_charge_power) == 20);
            REQUIRE(dt::from_RationalNumber(control_mode.present_active_power) == 15);
            REQUIRE(dt::from_RationalNumber(control_mode.present_reactive_power) == 0);

            REQUIRE(dt::from_RationalNumber(control_mode.max_discharge_power) == 32);
            REQUIRE(dt::from_RationalNumber(control_mode.min_discharge_power) == 20);
            REQUIRE(control_mode.grid_event_condition == 0);
        }
    }

    GIVEN("Deserialize ac_der_charge_loop_req + dynamic mode") {
        uint8_t doc_raw[] = {0x80, 0x08, 0x04, 0x1e, 0xa6, 0x5f, 0xc9, 0x9b, 0xa7, 0x6c, 0x4d, 0x89, 0x0c, 0x0e, 0x1b,
                             0x60, 0x62, 0xa6, 0x2c, 0x2a, 0xb8, 0xad, 0x81, 0x82, 0x0c, 0x06, 0x41, 0x06, 0x03, 0xc0,
                             0x83, 0x00, 0x50, 0x3f, 0x04, 0x00, 0xc9, 0x0f, 0xc1, 0xa0, 0x1e, 0x43, 0xf0, 0x6e, 0x05,
                             0x91, 0x00, 0x00, 0x04, 0x3f, 0x04, 0x00, 0xc9, 0x0f, 0xc1, 0xa0, 0x1e, 0x10, 0xfc, 0x10,
                             0x03, 0x22, 0x1f, 0x82, 0x00, 0x64, 0x80, 0x04, 0x41, 0x80, 0xa0, 0x00};

        const io::StreamInputView stream_view{doc_raw, sizeof(doc_raw)};

        message_20::Variant variant(io::v2gtp::PayloadType::Part20DerIec, stream_view);

        THEN("It should be deserialized successfully") {
            REQUIRE(variant.get_type() == message_20::Type::DER_AC_ChargeLoopReq);

            const auto& msg = variant.get<message_20::DER_AC_ChargeLoopRequest>();
            const auto& header = msg.header;

            REQUIRE(header.session_id == std::array<uint8_t, 8>{0x3D, 0x4C, 0xBF, 0x93, 0x37, 0x4E, 0xD8, 0x9B});
            REQUIRE(header.timestamp == 1725456400);
            REQUIRE(msg.meter_info_requested);

            using DynamicMode = dt::DER_Dynamic_AC_CLReqControlMode;

            REQUIRE(std::holds_alternative<DynamicMode>(msg.control_mode));
            const auto& control_mode = std::get<DynamicMode>(msg.control_mode);
            REQUIRE(control_mode.departure_time.value_or(0) == 1725470000);
            REQUIRE(dt::from_RationalNumber(control_mode.target_energy_request) == 25000);
            REQUIRE(dt::from_RationalNumber(control_mode.max_energy_request) == 30000);
            REQUIRE(dt::from_RationalNumber(control_mode.min_energy_request) == 5000);
            REQUIRE(dt::from_RationalNumber(control_mode.max_charge_power) == 32);
            REQUIRE(dt::from_RationalNumber(control_mode.min_charge_power) == 20);
            REQUIRE(dt::from_RationalNumber(control_mode.present_active_power) == 15);
            REQUIRE(dt::from_RationalNumber(control_mode.present_reactive_power) == 0);

            REQUIRE(dt::from_RationalNumber(control_mode.max_discharge_power) == 32);
            REQUIRE(dt::from_RationalNumber(control_mode.min_discharge_power) == 20);
            REQUIRE(dt::from_RationalNumber(control_mode.max_charge_reactive_power.value_or({})) == 32);
            REQUIRE(dt::from_RationalNumber(control_mode.max_discharge_reactive_power.value_or({})) == 32);
            REQUIRE(control_mode.grid_event_condition == 0);
            REQUIRE(dt::from_RationalNumber(control_mode.session_total_discharge_energy_available.value_or({})) ==
                    20000);
        }
    }

    GIVEN("Deserialize minimum ac_der_charge_loop_req + scheduled mode") {
        uint8_t doc_raw[] = {0x80, 0x08, 0x04, 0x1e, 0xa6, 0x5f, 0xc9, 0x9b, 0xa7, 0x6c, 0x4d,
                             0x8c, 0x3b, 0xfe, 0x1b, 0x60, 0x62, 0x89, 0x23, 0xf0, 0x6e, 0x05,
                             0x94, 0x7e, 0x08, 0x01, 0x92, 0x1f, 0x83, 0x40, 0x3c, 0x80, 0x00};

        const io::StreamInputView stream_view{doc_raw, sizeof(doc_raw)};

        message_20::Variant variant(io::v2gtp::PayloadType::Part20DerIec, stream_view);

        THEN("It should be deserialized successfully") {
            REQUIRE(variant.get_type() == message_20::Type::DER_AC_ChargeLoopReq);

            const auto& msg = variant.get<message_20::DER_AC_ChargeLoopRequest>();
            const auto& header = msg.header;

            REQUIRE(header.session_id == std::array<uint8_t, 8>{0x3D, 0x4C, 0xBF, 0x93, 0x37, 0x4E, 0xD8, 0x9B});
            REQUIRE(header.timestamp == 1725456323);
            REQUIRE(msg.meter_info_requested == false);

            using ScheduledMode = dt::DER_Scheduled_AC_CLReqControlMode;

            REQUIRE(std::holds_alternative<ScheduledMode>(msg.control_mode));
            const auto& control_mode = std::get<ScheduledMode>(msg.control_mode);
            REQUIRE(dt::from_RationalNumber(control_mode.present_active_power) == 15);
            REQUIRE(dt::from_RationalNumber(control_mode.max_discharge_power) == 32);
            REQUIRE(dt::from_RationalNumber(control_mode.min_discharge_power) == 20);
            REQUIRE(control_mode.grid_event_condition == 0);
        }
    }

    GIVEN("Deserialize ac_der_charge_loop_req + scheduled mode") {
        uint8_t doc_raw[] = {0x80, 0x08, 0x04, 0x1e, 0xa6, 0x5f, 0xc9, 0x9b, 0xa7, 0x6c, 0x4d, 0x8c, 0x3b,
                             0xfe, 0x1b, 0x60, 0x62, 0x88, 0x04, 0x18, 0x0c, 0x80, 0x41, 0x80, 0xf0, 0x23,
                             0xf0, 0x40, 0x0c, 0x88, 0x7e, 0x0d, 0x00, 0xf2, 0x1f, 0x83, 0x70, 0x2c, 0x44,
                             0x00, 0x00, 0x10, 0xfc, 0x10, 0x03, 0x24, 0x3f, 0x06, 0x80, 0x78, 0x43, 0xf0,
                             0x40, 0x0c, 0x88, 0x7e, 0x08, 0x01, 0x92, 0x00, 0x00};

        const io::StreamInputView stream_view{doc_raw, sizeof(doc_raw)};

        message_20::Variant variant(io::v2gtp::PayloadType::Part20DerIec, stream_view);

        THEN("It should be deserialized successfully") {
            REQUIRE(variant.get_type() == message_20::Type::DER_AC_ChargeLoopReq);

            const auto& msg = variant.get<message_20::DER_AC_ChargeLoopRequest>();
            const auto& header = msg.header;

            REQUIRE(header.session_id == std::array<uint8_t, 8>{0x3D, 0x4C, 0xBF, 0x93, 0x37, 0x4E, 0xD8, 0x9B});
            REQUIRE(header.timestamp == 1725456323);
            REQUIRE(msg.meter_info_requested == false);

            using ScheduledMode = dt::DER_Scheduled_AC_CLReqControlMode;

            REQUIRE(std::holds_alternative<ScheduledMode>(msg.control_mode));
            const auto& control_mode = std::get<ScheduledMode>(msg.control_mode);
            REQUIRE(dt::from_RationalNumber(control_mode.target_energy_request.value_or({})) == 25000);
            REQUIRE(dt::from_RationalNumber(control_mode.max_energy_request.value_or({})) == 30000);
            REQUIRE(dt::from_RationalNumber(control_mode.max_charge_power.value_or({})) == 32);
            REQUIRE(dt::from_RationalNumber(control_mode.min_charge_power.value_or({})) == 20);
            REQUIRE(dt::from_RationalNumber(control_mode.present_reactive_power.value_or({})) == 0);
            REQUIRE(dt::from_RationalNumber(control_mode.present_active_power) == 15);
            REQUIRE(dt::from_RationalNumber(control_mode.max_discharge_power) == 32);
            REQUIRE(dt::from_RationalNumber(control_mode.min_discharge_power) == 20);
            REQUIRE(dt::from_RationalNumber(control_mode.max_charge_reactive_power.value_or({})) == 32);
            REQUIRE(dt::from_RationalNumber(control_mode.max_discharge_reactive_power.value_or({})) == 32);
            REQUIRE(control_mode.grid_event_condition == 0);
        }
    }

    GIVEN("Serialize ac_der_charge_loop_req + dynamic mode") {
        message_20::DER_AC_ChargeLoopRequest req;

        req.header = message_20::Header{{0x3D, 0x4C, 0xBF, 0x93, 0x37, 0x4E, 0xD8, 0x9B}, 1725456400};
        req.meter_info_requested = true;
        auto& control_mode = req.control_mode.emplace<dt::DER_Dynamic_AC_CLReqControlMode>();

        control_mode.departure_time = 1725470000;
        control_mode.target_energy_request = {25, 3};
        control_mode.max_energy_request = {30, 3};
        control_mode.min_energy_request = {5, 3};
        control_mode.max_charge_power = {3200, -2};
        control_mode.min_charge_power = {2000, -2};
        control_mode.present_active_power = {1500, -2};
        control_mode.present_reactive_power = {0, 0};

        control_mode.max_discharge_power = {3200, -2};
        control_mode.min_discharge_power = {2000, -2};
        control_mode.max_charge_reactive_power.emplace<dt::RationalNumber>({3200, -2});
        control_mode.max_discharge_reactive_power.emplace<dt::RationalNumber>({3200, -2});
        control_mode.grid_event_condition = 0;
        control_mode.session_total_discharge_energy_available.emplace<dt::RationalNumber>({20, 3});

        std::vector<uint8_t> expected = {
            0x80, 0x08, 0x04, 0x1e, 0xa6, 0x5f, 0xc9, 0x9b, 0xa7, 0x6c, 0x4d, 0x89, 0x0c, 0x0e, 0x1b, 0x60, 0x62, 0xa6,
            0x2c, 0x2a, 0xb8, 0xad, 0x81, 0x82, 0x0c, 0x06, 0x41, 0x06, 0x03, 0xc0, 0x83, 0x00, 0x50, 0x3f, 0x04, 0x00,
            0xc9, 0x0f, 0xc1, 0xa0, 0x1e, 0x43, 0xf0, 0x6e, 0x05, 0x91, 0x00, 0x00, 0x04, 0x3f, 0x04, 0x00, 0xc9, 0x0f,
            0xc1, 0xa0, 0x1e, 0x10, 0xfc, 0x10, 0x03, 0x22, 0x1f, 0x82, 0x00, 0x64, 0x80, 0x04, 0x41, 0x80, 0xa0, 0x00};

        THEN("It should be serialized successfully") {
            REQUIRE(serialize_helper(req) == expected);
        }
    }

    GIVEN("Serialize ac_der_charge_loop_req + scheduled mode") {

        message_20::DER_AC_ChargeLoopRequest req;

        req.header = message_20::Header{{0x3D, 0x4C, 0xBF, 0x93, 0x37, 0x4E, 0xD8, 0x9B}, 1725456323};
        req.meter_info_requested = false;
        auto& control_mode = req.control_mode.emplace<dt::DER_Scheduled_AC_CLReqControlMode>();

        control_mode.target_energy_request = {25, 3};
        control_mode.max_energy_request = {30, 3};
        control_mode.max_charge_power = {3200, -2};
        control_mode.min_charge_power = {2000, -2};
        control_mode.present_active_power = {1500, -2};
        control_mode.present_reactive_power = {0, 0};

        control_mode.max_discharge_power = {3200, -2};
        control_mode.min_discharge_power = {2000, -2};
        control_mode.max_charge_reactive_power.emplace<dt::RationalNumber>({3200, -2});
        control_mode.max_discharge_reactive_power.emplace<dt::RationalNumber>({3200, -2});
        control_mode.grid_event_condition = 0;

        std::vector<uint8_t> expected = {0x80, 0x08, 0x04, 0x1e, 0xa6, 0x5f, 0xc9, 0x9b, 0xa7, 0x6c, 0x4d, 0x8c, 0x3b,
                                         0xfe, 0x1b, 0x60, 0x62, 0x88, 0x04, 0x18, 0x0c, 0x80, 0x41, 0x80, 0xf0, 0x23,
                                         0xf0, 0x40, 0x0c, 0x88, 0x7e, 0x0d, 0x00, 0xf2, 0x1f, 0x83, 0x70, 0x2c, 0x44,
                                         0x00, 0x00, 0x10, 0xfc, 0x10, 0x03, 0x24, 0x3f, 0x06, 0x80, 0x78, 0x43, 0xf0,
                                         0x40, 0x0c, 0x88, 0x7e, 0x08, 0x01, 0x92, 0x00, 0x00};

        THEN("It should be serialized successfully") {
            REQUIRE(serialize_helper(req) == expected);
        }
    }

    GIVEN("Deserialize minimum ac_der_charge_loop_res + dynamic mode") {
        uint8_t doc_raw[] = {0x80, 0x0c, 0x04, 0x1e, 0xa6, 0x5f, 0xc9, 0x9b, 0xa7, 0x6c, 0x4d,
                             0x89, 0x1c, 0x0e, 0x1b, 0x60, 0x62, 0x00, 0x78, 0x3f, 0x04, 0x00,
                             0xc9, 0x03, 0xf0, 0x40, 0x0c, 0x90, 0xfc, 0x10, 0x03, 0x23, 0x80};

        const io::StreamInputView stream_view{doc_raw, sizeof(doc_raw)};

        message_20::Variant variant(io::v2gtp::PayloadType::Part20DerIec, stream_view);

        THEN("It should be deserialized successfully") {
            REQUIRE(variant.get_type() == message_20::Type::DER_AC_ChargeLoopRes);

            const auto& msg = variant.get<message_20::DER_AC_ChargeLoopResponse>();
            const auto& header = msg.header;

            REQUIRE(header.session_id == std::array<uint8_t, 8>{0x3D, 0x4C, 0xBF, 0x93, 0x37, 0x4E, 0xD8, 0x9B});
            REQUIRE(header.timestamp == 1725456401);
            REQUIRE(msg.response_code == dt::ResponseCode::OK);

            using DynamicMode = dt::DER_Dynamic_AC_CLResControlMode;

            REQUIRE(std::holds_alternative<DynamicMode>(msg.control_mode));
            const auto& control_mode = std::get<DynamicMode>(msg.control_mode);
            REQUIRE(dt::from_RationalNumber(control_mode.target_active_power) == 32);
            REQUIRE(dt::from_RationalNumber(control_mode.max_charge_power) == 32);
            REQUIRE(dt::from_RationalNumber(control_mode.max_discharge_power) == 32);
        }
    }

    GIVEN("Deserialize ac_der_charge_loop_res + dynamic mode") {
        uint8_t doc_raw[] = {0x80, 0x0c, 0x04, 0x1e, 0xa6, 0x5f, 0xc9, 0x9b, 0xa7, 0x6c, 0x4d, 0x89, 0x1c, 0x0e, 0x1b,
                             0x60, 0x62, 0x00, 0x70, 0xb0, 0xaa, 0xe2, 0xb6, 0x06, 0x15, 0x02, 0x3f, 0x04, 0x00, 0xc8,
                             0x44, 0x00, 0x00, 0x08, 0x7e, 0x0d, 0xc0, 0xb2, 0x1f, 0x82, 0x00, 0x64, 0x87, 0xe0, 0x80,
                             0x19, 0x08, 0x7e, 0x08, 0x01, 0x91, 0x08, 0x20, 0x0f, 0x20, 0x20, 0x00, 0x08, 0x20};

        const io::StreamInputView stream_view{doc_raw, sizeof(doc_raw)};

        message_20::Variant variant(io::v2gtp::PayloadType::Part20DerIec, stream_view);

        THEN("It should be deserialized successfully") {
            REQUIRE(variant.get_type() == message_20::Type::DER_AC_ChargeLoopRes);

            const auto& msg = variant.get<message_20::DER_AC_ChargeLoopResponse>();
            const auto& header = msg.header;

            REQUIRE(header.session_id == std::array<uint8_t, 8>{0x3D, 0x4C, 0xBF, 0x93, 0x37, 0x4E, 0xD8, 0x9B});
            REQUIRE(header.timestamp == 1725456401);

            using DynamicMode = dt::DER_Dynamic_AC_CLResControlMode;

            REQUIRE(std::holds_alternative<DynamicMode>(msg.control_mode));
            const auto& control_mode = std::get<DynamicMode>(msg.control_mode);
            REQUIRE(control_mode.departure_time.value_or(0) == 1725470000);
            REQUIRE(control_mode.target_soc.value_or(0) == 80);
            REQUIRE(dt::from_RationalNumber(control_mode.target_active_power) == 32);
            REQUIRE(dt::from_RationalNumber(control_mode.target_reactive_power.value_or({1, 0})) == 0);
            REQUIRE(dt::from_RationalNumber(control_mode.present_active_power.value_or({})) == 15);
            REQUIRE(dt::from_RationalNumber(control_mode.max_charge_power) == 32);
            REQUIRE(dt::from_RationalNumber(control_mode.max_discharge_power) == 32);
            REQUIRE(dt::from_RationalNumber(control_mode.dso_discharge_power.value_or({})) == 32);

            REQUIRE(control_mode.dso_q_setpoint.has_value());
            const auto& dsoq = control_mode.dso_q_setpoint.value();
            REQUIRE(dt::from_RationalNumber(dsoq.dso_q_setpoint_value) == 1500);
            REQUIRE_FALSE(dsoq.pt1_response_reactive_power);
            REQUIRE(dt::from_RationalNumber(dsoq.step_response_time_constant_reactive_power) == 2);
        }
    }

    GIVEN("Deserialize minimum ac_der_charge_loop_req + scheduled mode") {
        uint8_t doc_raw[] = {0x80, 0x0c, 0x04, 0x1e, 0xa6, 0x5f, 0xc9, 0x9b, 0xa7, 0x6c, 0x4d, 0x89, 0x1c, 0x0e,
                             0x1b, 0x60, 0x62, 0x00, 0x89, 0x1f, 0x82, 0x00, 0x64, 0x87, 0xe0, 0x80, 0x19, 0x1c};

        const io::StreamInputView stream_view{doc_raw, sizeof(doc_raw)};

        message_20::Variant variant(io::v2gtp::PayloadType::Part20DerIec, stream_view);

        THEN("It should be deserialized successfully") {
            REQUIRE(variant.get_type() == message_20::Type::DER_AC_ChargeLoopRes);

            const auto& msg = variant.get<message_20::DER_AC_ChargeLoopResponse>();
            const auto& header = msg.header;

            REQUIRE(header.session_id == std::array<uint8_t, 8>{0x3D, 0x4C, 0xBF, 0x93, 0x37, 0x4E, 0xD8, 0x9B});
            REQUIRE(header.timestamp == 1725456401);

            using ScheduledMode = dt::DER_Scheduled_AC_CLResControlMode;

            REQUIRE(std::holds_alternative<ScheduledMode>(msg.control_mode));
            const auto& control_mode = std::get<ScheduledMode>(msg.control_mode);
            REQUIRE(dt::from_RationalNumber(control_mode.max_charge_power) == 32);
            REQUIRE(dt::from_RationalNumber(control_mode.max_discharge_power) == 32);
        }
    }

    GIVEN("Deserialize ac_der_charge_loop_req + scheduled mode") {
        uint8_t doc_raw[] = {0x80, 0x0c, 0x04, 0x1e, 0xa6, 0x5f, 0xc9, 0x9b, 0xa7, 0x6c, 0x4d, 0x89, 0x1c,
                             0x0e, 0x1b, 0x60, 0x62, 0x00, 0x80, 0x1f, 0x82, 0x00, 0x64, 0x22, 0x00, 0x00,
                             0x04, 0x3f, 0x06, 0xe0, 0x59, 0x0f, 0xc1, 0x00, 0x32, 0x43, 0xf0, 0x40, 0x0c,
                             0x84, 0x3f, 0x06, 0x80, 0x78, 0xc3, 0xf0, 0x2f, 0x90, 0x01, 0x00, 0x00, 0x40};

        const io::StreamInputView stream_view{doc_raw, sizeof(doc_raw)};

        message_20::Variant variant(io::v2gtp::PayloadType::Part20DerIec, stream_view);

        THEN("It should be deserialized successfully") {
            REQUIRE(variant.get_type() == message_20::Type::DER_AC_ChargeLoopRes);

            const auto& msg = variant.get<message_20::DER_AC_ChargeLoopResponse>();
            const auto& header = msg.header;

            REQUIRE(header.session_id == std::array<uint8_t, 8>{0x3D, 0x4C, 0xBF, 0x93, 0x37, 0x4E, 0xD8, 0x9B});
            REQUIRE(header.timestamp == 1725456401);

            using ScheduledMode = dt::DER_Scheduled_AC_CLResControlMode;

            REQUIRE(std::holds_alternative<ScheduledMode>(msg.control_mode));
            const auto& control_mode = std::get<ScheduledMode>(msg.control_mode);
            REQUIRE(dt::from_RationalNumber(control_mode.target_active_power.value_or({})) == 32);
            REQUIRE(dt::from_RationalNumber(control_mode.target_reactive_power.value_or({1, 0})) == 0);
            REQUIRE(dt::from_RationalNumber(control_mode.present_active_power.value_or({})) == 15);
            REQUIRE(dt::from_RationalNumber(control_mode.max_charge_power) == 32);
            REQUIRE(dt::from_RationalNumber(control_mode.max_discharge_power) == 32);
            REQUIRE(dt::from_RationalNumber(control_mode.dso_discharge_power.value_or({})) == 20);

            REQUIRE(control_mode.dso_cos_phi_setpoint.has_value());
            const auto& dso_cos_phi = control_mode.dso_cos_phi_setpoint.value();
            REQUIRE_THAT(dt::from_RationalNumber(dso_cos_phi.dso_cos_phi_setpoint_value),
                         Catch::Matchers::WithinRel(0.95, 0.001));
            REQUIRE(dso_cos_phi.excitation == dt::PowerFactorExcitation::OverExcited);
            REQUIRE_FALSE(dso_cos_phi.pt1_response_reactive_power);
            REQUIRE(dt::from_RationalNumber(dso_cos_phi.step_response_time_constant_reactive_power) == 2);
        }
    }

    GIVEN("Serialize ac_der_charge_loop_res + dynamic mode") {
        message_20::DER_AC_ChargeLoopResponse res;

        res.header = message_20::Header{{0x3D, 0x4C, 0xBF, 0x93, 0x37, 0x4E, 0xD8, 0x9B}, 1725456401};
        res.response_code = dt::ResponseCode::OK;
        auto& control_mode = res.control_mode.emplace<dt::DER_Dynamic_AC_CLResControlMode>();

        control_mode.departure_time = 1725470000;
        control_mode.target_soc = 80;
        control_mode.target_active_power = {3200, -2};
        control_mode.target_reactive_power = {0, 0};
        control_mode.max_charge_power = {3200, -2};
        control_mode.present_active_power = {1500, -2};
        control_mode.max_discharge_power = {3200, -2};
        control_mode.dso_discharge_power = {3200, -2};

        auto& dsoq = control_mode.dso_q_setpoint.emplace();
        dsoq.dso_q_setpoint_value = {15, 2};
        dsoq.pt1_response_reactive_power = false;
        dsoq.step_response_time_constant_reactive_power = {2, 0};

        std::vector<uint8_t> expected = {0x80, 0x0c, 0x04, 0x1e, 0xa6, 0x5f, 0xc9, 0x9b, 0xa7, 0x6c, 0x4d, 0x89,
                                         0x1c, 0x0e, 0x1b, 0x60, 0x62, 0x00, 0x70, 0xb0, 0xaa, 0xe2, 0xb6, 0x06,
                                         0x15, 0x02, 0x3f, 0x04, 0x00, 0xc8, 0x44, 0x00, 0x00, 0x08, 0x7e, 0x0d,
                                         0xc0, 0xb2, 0x1f, 0x82, 0x00, 0x64, 0x87, 0xe0, 0x80, 0x19, 0x08, 0x7e,
                                         0x08, 0x01, 0x91, 0x08, 0x20, 0x0f, 0x20, 0x20, 0x00, 0x08, 0x20};

        THEN("It should be serialized successfully") {
            REQUIRE(serialize_helper(res) == expected);
        }
    }

    GIVEN("Serialize ac_der_charge_loop_req + scheduled mode") {
        message_20::DER_AC_ChargeLoopResponse res;

        res.header = message_20::Header{{0x3D, 0x4C, 0xBF, 0x93, 0x37, 0x4E, 0xD8, 0x9B}, 1725456401};
        res.response_code = dt::ResponseCode::OK;
        auto& control_mode = res.control_mode.emplace<dt::DER_Scheduled_AC_CLResControlMode>();

        control_mode.target_active_power = {3200, -2};
        control_mode.target_reactive_power = {0, 0};
        control_mode.max_charge_power = {3200, -2};
        control_mode.present_active_power = {1500, -2};
        control_mode.max_discharge_power = {3200, -2};
        control_mode.dso_discharge_power = {2000, -2};

        auto& dso_cos_phi = control_mode.dso_cos_phi_setpoint.emplace();
        dso_cos_phi.dso_cos_phi_setpoint_value = {95, -2};
        dso_cos_phi.excitation = dt::PowerFactorExcitation::OverExcited;
        dso_cos_phi.pt1_response_reactive_power = false;
        dso_cos_phi.step_response_time_constant_reactive_power = {2, 0};

        std::vector<uint8_t> expected = {0x80, 0x0c, 0x04, 0x1e, 0xa6, 0x5f, 0xc9, 0x9b, 0xa7, 0x6c, 0x4d, 0x89, 0x1c,
                                         0x0e, 0x1b, 0x60, 0x62, 0x00, 0x80, 0x1f, 0x82, 0x00, 0x64, 0x22, 0x00, 0x00,
                                         0x04, 0x3f, 0x06, 0xe0, 0x59, 0x0f, 0xc1, 0x00, 0x32, 0x43, 0xf0, 0x40, 0x0c,
                                         0x84, 0x3f, 0x06, 0x80, 0x78, 0xc3, 0xf0, 0x2f, 0x90, 0x01, 0x00, 0x00, 0x40};

        THEN("It should be serialized successfully") {
            REQUIRE(serialize_helper(res) == expected);
        }
    }
}
