#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <iso15118/message/ac_der_sae_charge_loop.hpp>
#include <iso15118/message/variant.hpp>

#include "helper.hpp"

using namespace iso15118;

namespace dt = message_20::datatypes;
namespace dtsae = message_20::datatypes::sae;

SCENARIO("Se/Deserialize ac der sae charge loop response messages") {

    GIVEN("cl_res_scheduled_min") {

        std::vector<uint8_t> bytes = {0x80, 0x0c, 0x04, 0x1e, 0xa6, 0x5f, 0xc9, 0x9b, 0xa7, 0x6c, 0x4d,
                                      0x8c, 0x4b, 0xfe, 0x1b, 0x60, 0x62, 0x00, 0x89, 0x89, 0xe8, 0x00};

        const io::StreamInputView stream_view{bytes.data(), bytes.size()};

        message_20::Variant variant(io::v2gtp::PayloadType::Part20DerSae, stream_view);

        THEN("It should be deserialized successfully") {
            REQUIRE(variant.get_type() == message_20::Type::DER_SAE_AC_ChargeLoopRes);

            const auto& msg = variant.get<message_20::DER_SAE_AC_ChargeLoopResponse>();
            const auto& header = msg.header;

            REQUIRE(header.session_id == std::array<uint8_t, 8>{0x3D, 0x4C, 0xBF, 0x93, 0x37, 0x4E, 0xD8, 0x9B});
            REQUIRE(header.timestamp == 1725456324);
            REQUIRE(msg.response_code == dt::ResponseCode::OK);

            REQUIRE(std::holds_alternative<dtsae::DER_Scheduled_AC_CLResControlMode>(msg.control_mode));
            const auto& mode = std::get<dtsae::DER_Scheduled_AC_CLResControlMode>(msg.control_mode);

            REQUIRE(mode.der_control_cl_res.enter_service_cl_res.permit_service == true);

            // absent optionals
            REQUIRE_FALSE(msg.status.has_value());
            REQUIRE_FALSE(msg.meter_info.has_value());
            REQUIRE_FALSE(msg.receipt.has_value());
            REQUIRE_FALSE(msg.target_frequency.has_value());
            REQUIRE_FALSE(mode.der_control_cl_res.voltage_trip.has_value());
            REQUIRE_FALSE(mode.der_control_cl_res.frequency_trip.has_value());
            REQUIRE_FALSE(mode.der_control_cl_res.reactive_power_support_cl_res.has_value());
            REQUIRE_FALSE(mode.der_control_cl_res.active_power_support_cl_res.has_value());
            REQUIRE_FALSE(mode.target_active_power.has_value());
            REQUIRE_FALSE(mode.evse_maximum_charge_power.has_value());
            REQUIRE_FALSE(mode.required_der_operating_mode.has_value());
        }

        THEN("It should serialize back to the same bytes") {
            message_20::DER_SAE_AC_ChargeLoopResponse res;
            res.header = message_20::Header{{0x3D, 0x4C, 0xBF, 0x93, 0x37, 0x4E, 0xD8, 0x9B}, 1725456324};
            res.response_code = dt::ResponseCode::OK;

            dtsae::DER_Scheduled_AC_CLResControlMode mode;
            mode.der_control_cl_res.enter_service_cl_res.permit_service = true;
            res.control_mode = mode;

            REQUIRE(serialize_helper(res) == bytes);
        }
    }

    GIVEN("cl_res_dynamic_min") {

        std::vector<uint8_t> bytes = {0x80, 0x0c, 0x04, 0x1e, 0xa6, 0x5f, 0xc9, 0x9b, 0xa7, 0x6c, 0x4d, 0x8c, 0x4b,
                                      0xfe, 0x1b, 0x60, 0x62, 0x00, 0x78, 0x41, 0x80, 0x51, 0x11, 0x3d, 0x00};

        const io::StreamInputView stream_view{bytes.data(), bytes.size()};

        message_20::Variant variant(io::v2gtp::PayloadType::Part20DerSae, stream_view);

        THEN("It should be deserialized successfully") {
            REQUIRE(variant.get_type() == message_20::Type::DER_SAE_AC_ChargeLoopRes);

            const auto& msg = variant.get<message_20::DER_SAE_AC_ChargeLoopResponse>();
            const auto& header = msg.header;

            REQUIRE(header.session_id == std::array<uint8_t, 8>{0x3D, 0x4C, 0xBF, 0x93, 0x37, 0x4E, 0xD8, 0x9B});
            REQUIRE(header.timestamp == 1725456324);
            REQUIRE(msg.response_code == dt::ResponseCode::OK);

            REQUIRE(std::holds_alternative<dtsae::DER_Dynamic_AC_CLResControlMode>(msg.control_mode));
            const auto& mode = std::get<dtsae::DER_Dynamic_AC_CLResControlMode>(msg.control_mode);

            // EVSETargetActivePower is mandatory (1,1) in Dynamic mode
            REQUIRE(dt::from_RationalNumber(mode.target_active_power) == 10000);
            REQUIRE(mode.der_control_cl_res.enter_service_cl_res.permit_service == true);

            // absent optionals
            REQUIRE_FALSE(msg.status.has_value());
            REQUIRE_FALSE(msg.meter_info.has_value());
            REQUIRE_FALSE(msg.receipt.has_value());
            REQUIRE_FALSE(msg.target_frequency.has_value());
            REQUIRE_FALSE(mode.departure_time.has_value());
            REQUIRE_FALSE(mode.minimum_soc.has_value());
            REQUIRE_FALSE(mode.target_soc.has_value());
            REQUIRE_FALSE(mode.ack_max_delay.has_value());
            REQUIRE_FALSE(mode.der_control_cl_res.voltage_trip.has_value());
            REQUIRE_FALSE(mode.der_control_cl_res.frequency_trip.has_value());
            REQUIRE_FALSE(mode.evse_maximum_charge_power.has_value());
            REQUIRE_FALSE(mode.required_der_operating_mode.has_value());
        }

        THEN("It should serialize back to the same bytes") {
            message_20::DER_SAE_AC_ChargeLoopResponse res;
            res.header = message_20::Header{{0x3D, 0x4C, 0xBF, 0x93, 0x37, 0x4E, 0xD8, 0x9B}, 1725456324};
            res.response_code = dt::ResponseCode::OK;

            dtsae::DER_Dynamic_AC_CLResControlMode mode;
            mode.target_active_power = {10, 3};
            mode.der_control_cl_res.enter_service_cl_res.permit_service = true;
            res.control_mode = mode;

            REQUIRE(serialize_helper(res) == bytes);
        }
    }

    GIVEN("cl_res_scheduled_optionals") {

        std::vector<uint8_t> bytes = {
            0x80, 0x0c, 0x04, 0x1e, 0xa6, 0x5f, 0xc9, 0x9b, 0xa7, 0x6c, 0x4d, 0x8c, 0x4b, 0xfe, 0x1b, 0x60, 0x62, 0x00,
            0x00, 0xf0, 0x00, 0x00, 0xf4, 0x55, 0x65, 0x34, 0x52, 0xd4, 0xd4, 0x55, 0x44, 0x55, 0x22, 0xd3, 0x03, 0x11,
            0xc0, 0xba, 0xc0, 0x62, 0x5f, 0xf0, 0xdb, 0x03, 0x28, 0x10, 0x00, 0x78, 0x40, 0x20, 0xc0, 0x28, 0x22, 0x00,
            0x00, 0x04, 0x41, 0x80, 0x49, 0x02, 0x40, 0x0c, 0x08, 0x00, 0x88, 0x02, 0x04, 0x00, 0x00, 0x80, 0x80, 0x0a,
            0x00, 0x20, 0x3f, 0x80, 0x10, 0x61, 0x20, 0x06, 0x04, 0x00, 0x62, 0x00, 0x82, 0x00, 0x00, 0x80, 0x40, 0x05,
            0x00, 0x08, 0x1f, 0xc0, 0x08, 0x34, 0x04, 0x84, 0x18, 0x10, 0x00, 0x7c, 0x08, 0x00, 0x01, 0x01, 0x00, 0x07,
            0xe0, 0x7f, 0x00, 0x50, 0xc2, 0x42, 0x0c, 0x08, 0x00, 0x3a, 0x04, 0x00, 0x00, 0x80, 0x80, 0x03, 0x90, 0x3f,
            0x80, 0x28, 0x68, 0x40, 0x40, 0x07, 0xe8, 0x08, 0x08, 0x00, 0xcf, 0x01, 0x01, 0x00, 0x07, 0xa0, 0x20, 0x00,
            0xec, 0x04, 0x00, 0x56, 0x01, 0x01, 0x00, 0x07, 0x80, 0x40, 0x03, 0xc0, 0x01, 0x00, 0x10, 0x7e, 0x05, 0xf2,
            0x13, 0x01, 0x00, 0x10, 0x0f, 0xa0, 0x48, 0x07, 0xe0, 0x05, 0x21, 0x10, 0x00, 0x0a, 0x14, 0x49, 0x40, 0xc0,
            0x20, 0xc0, 0x58, 0x22, 0x0c, 0x03, 0xc4, 0x00, 0x00};

        const io::StreamInputView stream_view{bytes.data(), bytes.size()};

        message_20::Variant variant(io::v2gtp::PayloadType::Part20DerSae, stream_view);

        THEN("It should be deserialized successfully") {
            REQUIRE(variant.get_type() == message_20::Type::DER_SAE_AC_ChargeLoopRes);

            const auto& msg = variant.get<message_20::DER_SAE_AC_ChargeLoopResponse>();
            const auto& header = msg.header;

            REQUIRE(header.session_id == std::array<uint8_t, 8>{0x3D, 0x4C, 0xBF, 0x93, 0x37, 0x4E, 0xD8, 0x9B});
            REQUIRE(header.timestamp == 1725456324);
            REQUIRE(msg.response_code == dt::ResponseCode::OK);

            // populated optionals
            REQUIRE(msg.status.has_value());
            REQUIRE(msg.status.value().notification == dt::EvseNotification::Pause);
            REQUIRE(msg.status.value().notification_max_delay == 60);
            REQUIRE(msg.meter_info.has_value());
            REQUIRE(msg.meter_info.value().meter_id == "EVSE-METER-01");
            REQUIRE(msg.meter_info.value().charged_energy_reading_wh == 12000);
            REQUIRE(msg.receipt.has_value());
            REQUIRE(msg.receipt.value().time_anchor == 1725456324);
            REQUIRE(msg.target_frequency.has_value());
            REQUIRE(dt::from_RationalNumber(msg.target_frequency.value()) == 60);

            REQUIRE(std::holds_alternative<dtsae::DER_Scheduled_AC_CLResControlMode>(msg.control_mode));
            const auto& mode = std::get<dtsae::DER_Scheduled_AC_CLResControlMode>(msg.control_mode);

            REQUIRE(dt::from_RationalNumber(mode.target_active_power.value()) == 10000);
            REQUIRE(dt::from_RationalNumber(mode.target_reactive_power.value()) == 0);
            REQUIRE(dt::from_RationalNumber(mode.present_active_power.value()) == 9000);

            const auto& der_control = mode.der_control_cl_res;
            REQUIRE(der_control.enter_service_cl_res.permit_service == true);
            REQUIRE(dt::from_RationalNumber(der_control.enter_service_cl_res.enter_service_voltage_high.value()) ==
                    253);
            REQUIRE(dt::from_RationalNumber(der_control.enter_service_cl_res.enter_service_voltage_low.value()) == 207);
            REQUIRE(dt::from_RationalNumber(der_control.enter_service_cl_res.enter_service_delay.value()) == 300);
            REQUIRE(dt::from_RationalNumber(der_control.enter_service_cl_res.enter_service_ramp_time.value()) == 120);

            // VoltageTrip curve coverage
            REQUIRE(der_control.voltage_trip.has_value());
            const auto& ov = der_control.voltage_trip.value().over_voltage_must_trip_curve;
            REQUIRE(ov.enable == true);
            REQUIRE(ov.x_unit == dtsae::DERUnit::V);
            REQUIRE(ov.y_unit == dtsae::DERUnit::s);
            REQUIRE(ov.curve_data_points.size() == 2);
            REQUIRE(dt::from_RationalNumber(ov.curve_data_points[0].x_value) == 264);
            REQUIRE(dt::from_RationalNumber(ov.curve_data_points[0].y_value) == 1);
            REQUIRE(dt::from_RationalNumber(ov.curve_data_points[1].x_value) == 288);
            REQUIRE_THAT(dt::from_RationalNumber(ov.curve_data_points[1].y_value),
                         Catch::Matchers::WithinRel(0.2, 0.001));
            const auto& uv = der_control.voltage_trip.value().under_voltage_must_trip_curve;
            REQUIRE(uv.curve_data_points.size() == 2);
            REQUIRE(dt::from_RationalNumber(uv.curve_data_points[0].x_value) == 196);

            // FrequencyTrip curve coverage
            REQUIRE(der_control.frequency_trip.has_value());
            const auto& of = der_control.frequency_trip.value().over_frequency_must_trip_curve;
            REQUIRE(of.enable == true);
            REQUIRE(of.x_unit == dtsae::DERUnit::Hz);
            REQUIRE(of.curve_data_points.size() == 2);
            REQUIRE(dt::from_RationalNumber(of.curve_data_points[0].x_value) == 62);

            // ReactivePowerSupport: ConstantPowerFactor
            REQUIRE(der_control.reactive_power_support_cl_res.has_value());
            const auto& rps = der_control.reactive_power_support_cl_res.value();
            REQUIRE(rps.constant_power_factor.has_value());
            REQUIRE(rps.constant_power_factor.value().enable == true);
            REQUIRE(rps.constant_power_factor.value().priority.value() == 1);
            REQUIRE(rps.constant_power_factor.value().power_factor_excitation ==
                    dtsae::PowerFactorExcitation::OverExcited);
            REQUIRE_FALSE(rps.volt_var.has_value());

            // ActivePowerSupport: FrequencyDroop + LimitMaxDischargePower
            REQUIRE(der_control.active_power_support_cl_res.has_value());
            const auto& aps = der_control.active_power_support_cl_res.value();
            REQUIRE(aps.frequency_droop.has_value());
            REQUIRE(aps.frequency_droop.value().enable == true);
            REQUIRE(aps.frequency_droop.value().priority.value() == 2);
            REQUIRE(aps.frequency_droop.value().over_frequency_droop.has_value());
            REQUIRE(aps.frequency_droop.value().over_frequency_droop.value().power_reference ==
                    dtsae::PowerReference::MaximumActivePower);
            REQUIRE(aps.limit_max_discharge_power.has_value());
            REQUIRE(aps.limit_max_discharge_power.value().percentage_value == 80);

            REQUIRE(dt::from_RationalNumber(mode.evse_maximum_charge_power.value()) == 22000);
            REQUIRE(dt::from_RationalNumber(mode.evse_maximum_discharge_power.value()) == 15000);
            REQUIRE(mode.required_der_operating_mode.value() == dtsae::RequiredDEROperatingMode::GridFollowing);
            REQUIRE(mode.grid_connection_mode.value() == dtsae::GridConnectionMode::GridConnected);
        }

        THEN("It should serialize back to the same bytes") {
            message_20::DER_SAE_AC_ChargeLoopResponse res;
            res.header = message_20::Header{{0x3D, 0x4C, 0xBF, 0x93, 0x37, 0x4E, 0xD8, 0x9B}, 1725456324};
            res.response_code = dt::ResponseCode::OK;

            res.status = dt::EvseStatus{60, dt::EvseNotification::Pause};

            dt::MeterInfo meter_info;
            meter_info.meter_id = "EVSE-METER-01";
            meter_info.charged_energy_reading_wh = 12000;
            res.meter_info = meter_info;

            dt::Receipt receipt;
            receipt.time_anchor = 1725456324;
            res.receipt = receipt;

            res.target_frequency = dt::RationalNumber{60, 0};

            dtsae::DER_Scheduled_AC_CLResControlMode mode;
            mode.target_active_power = {10, 3};
            mode.target_reactive_power = {0, 0};
            mode.present_active_power = {9, 3};

            auto& der_control = mode.der_control_cl_res;

            dtsae::VoltageTrip voltage_trip;
            auto& ov_must = voltage_trip.over_voltage_must_trip_curve;
            ov_must.enable = true;
            ov_must.x_unit = dtsae::DERUnit::V;
            ov_must.y_unit = dtsae::DERUnit::s;
            ov_must.curve_data_points.push_back(dtsae::DataTuple{{264, 0}, {1, 0}});
            ov_must.curve_data_points.push_back(dtsae::DataTuple{{288, 0}, {2, -1}});
            auto& uv_must = voltage_trip.under_voltage_must_trip_curve;
            uv_must.enable = true;
            uv_must.x_unit = dtsae::DERUnit::V;
            uv_must.y_unit = dtsae::DERUnit::s;
            uv_must.curve_data_points.push_back(dtsae::DataTuple{{196, 0}, {2, 0}});
            uv_must.curve_data_points.push_back(dtsae::DataTuple{{160, 0}, {2, -1}});
            der_control.voltage_trip = voltage_trip;

            dtsae::FrequencyTrip frequency_trip;
            auto& of_must = frequency_trip.over_frequency_must_trip_curve;
            of_must.enable = true;
            of_must.x_unit = dtsae::DERUnit::Hz;
            of_must.y_unit = dtsae::DERUnit::s;
            of_must.curve_data_points.push_back(dtsae::DataTuple{{62, 0}, {1, 0}});
            of_must.curve_data_points.push_back(dtsae::DataTuple{{63, 0}, {5, -1}});
            auto& uf_must = frequency_trip.under_frequency_must_trip_curve;
            uf_must.enable = true;
            uf_must.x_unit = dtsae::DERUnit::Hz;
            uf_must.y_unit = dtsae::DERUnit::s;
            uf_must.curve_data_points.push_back(dtsae::DataTuple{{58, 0}, {1, 0}});
            uf_must.curve_data_points.push_back(dtsae::DataTuple{{57, 0}, {5, -1}});
            der_control.frequency_trip = frequency_trip;

            auto& enter_service = der_control.enter_service_cl_res;
            enter_service.permit_service = true;
            enter_service.enter_service_voltage_high = {253, 0};
            enter_service.enter_service_voltage_low = {207, 0};
            enter_service.enter_service_frequency_high = {61, 0};
            enter_service.enter_service_frequency_low = {59, 0};
            enter_service.enter_service_delay = {300, 0};
            enter_service.enter_service_randomized_delay = {60, 0};
            enter_service.enter_service_ramp_time = {120, 0};

            dtsae::ReactivePowerSupportCLRes reactive_support;
            dtsae::ConstantPowerFactor cpf;
            cpf.enable = true;
            cpf.priority = 1;
            cpf.power_factor_value = {95, -2};
            cpf.power_factor_excitation = dtsae::PowerFactorExcitation::OverExcited;
            reactive_support.constant_power_factor = cpf;
            der_control.reactive_power_support_cl_res = reactive_support;

            dtsae::ActivePowerSupportCLRes active_support;
            dtsae::FrequencyDroop frequency_droop;
            frequency_droop.enable = true;
            frequency_droop.priority = 2;
            dtsae::FrequencyDroopSettings over_droop;
            over_droop.db = {36, -3};
            over_droop.droop_factor = {5, -2};
            over_droop.power_reference = dtsae::PowerReference::MaximumActivePower;
            over_droop.open_loop_response_time = {5, 0};
            frequency_droop.over_frequency_droop = over_droop;
            active_support.frequency_droop = frequency_droop;
            dtsae::LimitMaxDischargePower limit_max;
            limit_max.enable = true;
            limit_max.percentage_value = 80;
            active_support.limit_max_discharge_power = limit_max;
            der_control.active_power_support_cl_res = active_support;

            mode.evse_maximum_charge_power = {22, 3};
            mode.evse_maximum_discharge_power = {15, 3};
            mode.required_der_operating_mode = dtsae::RequiredDEROperatingMode::GridFollowing;
            mode.grid_connection_mode = dtsae::GridConnectionMode::GridConnected;

            res.control_mode = mode;

            REQUIRE(serialize_helper(res) == bytes);
        }
    }

    GIVEN("cl_res_dynamic_optionals") {

        std::vector<uint8_t> bytes = {
            0x80, 0x0c, 0x04, 0x1e, 0xa6, 0x5f, 0xc9, 0x9b, 0xa7, 0x6c, 0x4d, 0x8c, 0x4b, 0xfe, 0x1b, 0x60, 0x62,
            0x00, 0x00, 0xf0, 0x00, 0x00, 0xf4, 0x55, 0x65, 0x34, 0x52, 0xd4, 0xd4, 0x55, 0x44, 0x55, 0x22, 0xd3,
            0x03, 0x11, 0xc0, 0xba, 0xc0, 0x62, 0x5f, 0xf0, 0xdb, 0x03, 0x28, 0x10, 0x00, 0x78, 0x30, 0xa0, 0x38,
            0x01, 0xe0, 0xa0, 0x01, 0x41, 0x06, 0x01, 0x41, 0x10, 0x00, 0x00, 0x22, 0x0c, 0x02, 0x48, 0x12, 0x00,
            0x60, 0x40, 0x04, 0x40, 0x10, 0x20, 0x00, 0x04, 0x04, 0x00, 0x50, 0x01, 0x01, 0xfc, 0x00, 0x83, 0x09,
            0x00, 0x30, 0x20, 0x03, 0x10, 0x04, 0x10, 0x00, 0x04, 0x02, 0x00, 0x28, 0x00, 0x40, 0xfe, 0x00, 0x41,
            0xa0, 0x24, 0x20, 0xc0, 0x80, 0x03, 0xe0, 0x40, 0x00, 0x08, 0x08, 0x00, 0x3f, 0x03, 0xf8, 0x02, 0x86,
            0x12, 0x10, 0x60, 0x40, 0x01, 0xd0, 0x20, 0x00, 0x04, 0x04, 0x00, 0x1c, 0x81, 0xfc, 0x01, 0x43, 0x42,
            0x02, 0x00, 0x3f, 0x40, 0x40, 0x40, 0x06, 0x78, 0x08, 0x08, 0x00, 0x3d, 0x01, 0x00, 0x07, 0x63, 0x09,
            0x00, 0x10, 0x01, 0x00, 0x80, 0x0c, 0xf0, 0x10, 0x41, 0x80, 0x18, 0x08, 0x00, 0xfd, 0x01, 0x04, 0x18,
            0x81, 0x06, 0x20, 0x00, 0x14, 0x48, 0x00, 0xe6, 0x01, 0x00, 0xac, 0x02, 0x21, 0x10, 0x03, 0x08, 0x30,
            0x05, 0x21, 0x08, 0x08, 0x30, 0x16, 0x08, 0x83, 0x00, 0xf1, 0x21, 0x00};

        const io::StreamInputView stream_view{bytes.data(), bytes.size()};

        message_20::Variant variant(io::v2gtp::PayloadType::Part20DerSae, stream_view);

        THEN("It should be deserialized successfully") {
            REQUIRE(variant.get_type() == message_20::Type::DER_SAE_AC_ChargeLoopRes);

            const auto& msg = variant.get<message_20::DER_SAE_AC_ChargeLoopResponse>();
            const auto& header = msg.header;

            REQUIRE(header.session_id == std::array<uint8_t, 8>{0x3D, 0x4C, 0xBF, 0x93, 0x37, 0x4E, 0xD8, 0x9B});
            REQUIRE(header.timestamp == 1725456324);
            REQUIRE(msg.response_code == dt::ResponseCode::OK);

            REQUIRE(msg.status.has_value());
            REQUIRE(msg.status.value().notification == dt::EvseNotification::Pause);
            REQUIRE(msg.meter_info.has_value());
            REQUIRE(msg.meter_info.value().meter_id == "EVSE-METER-01");
            REQUIRE(msg.receipt.has_value());
            REQUIRE(msg.receipt.value().time_anchor == 1725456324);
            REQUIRE(msg.target_frequency.has_value());
            REQUIRE(dt::from_RationalNumber(msg.target_frequency.value()) == 60);

            REQUIRE(std::holds_alternative<dtsae::DER_Dynamic_AC_CLResControlMode>(msg.control_mode));
            const auto& mode = std::get<dtsae::DER_Dynamic_AC_CLResControlMode>(msg.control_mode);

            REQUIRE(mode.departure_time.value() == 7200);
            REQUIRE(mode.minimum_soc.value() == 30);
            REQUIRE(mode.target_soc.value() == 80);
            REQUIRE(mode.ack_max_delay.value() == 10);
            REQUIRE(dt::from_RationalNumber(mode.target_active_power) == 10000);
            REQUIRE(dt::from_RationalNumber(mode.target_reactive_power.value()) == 0);
            REQUIRE(dt::from_RationalNumber(mode.present_active_power.value()) == 9000);

            const auto& der_control = mode.der_control_cl_res;
            REQUIRE(der_control.enter_service_cl_res.permit_service == true);
            REQUIRE(dt::from_RationalNumber(der_control.enter_service_cl_res.enter_service_voltage_high.value()) ==
                    253);
            REQUIRE(dt::from_RationalNumber(der_control.enter_service_cl_res.enter_service_frequency_low.value()) ==
                    59);
            // not present in the dynamic case
            REQUIRE_FALSE(der_control.enter_service_cl_res.enter_service_delay.has_value());
            REQUIRE_FALSE(der_control.enter_service_cl_res.enter_service_ramp_time.has_value());

            // VoltageTrip curve coverage
            REQUIRE(der_control.voltage_trip.has_value());
            const auto& ov = der_control.voltage_trip.value().over_voltage_must_trip_curve;
            REQUIRE(ov.enable == true);
            REQUIRE(ov.curve_data_points.size() == 2);
            REQUIRE(dt::from_RationalNumber(ov.curve_data_points[0].x_value) == 264);
            REQUIRE(dt::from_RationalNumber(ov.curve_data_points[1].x_value) == 288);

            // FrequencyTrip curve coverage
            REQUIRE(der_control.frequency_trip.has_value());
            REQUIRE(der_control.frequency_trip.value().over_frequency_must_trip_curve.curve_data_points.size() == 2);

            // ReactivePowerSupport: VoltVar
            REQUIRE(der_control.reactive_power_support_cl_res.has_value());
            const auto& rps = der_control.reactive_power_support_cl_res.value();
            REQUIRE(rps.volt_var.has_value());
            const auto& vv = rps.volt_var.value();
            REQUIRE(vv.enable == true);
            REQUIRE(vv.priority.value() == 1);
            REQUIRE(vv.x_unit == dtsae::DERUnit::V);
            REQUIRE(vv.y_unit == dtsae::DERUnit::var);
            REQUIRE(vv.curve_data_points.size() == 2);
            REQUIRE(dt::from_RationalNumber(vv.curve_data_points[0].x_value) == 207);
            REQUIRE(dt::from_RationalNumber(vv.curve_data_points[0].y_value) == 3000);
            REQUIRE(dt::from_RationalNumber(vv.curve_data_points[1].y_value) == -3000);
            REQUIRE(dt::from_RationalNumber(vv.reference_voltage) == 230);
            REQUIRE(vv.autonomous_reference_voltage_adjustment_enable == false);
            REQUIRE(vv.reference_voltage_adjustment_time_constant == 300);
            REQUIRE_FALSE(rps.constant_power_factor.has_value());

            // ActivePowerSupport: ConstantWatt
            REQUIRE(der_control.active_power_support_cl_res.has_value());
            const auto& aps = der_control.active_power_support_cl_res.value();
            REQUIRE(aps.constant_watt.has_value());
            REQUIRE(aps.constant_watt.value().enable == true);
            REQUIRE(aps.constant_watt.value().priority.value() == 3);
            REQUIRE(dt::from_RationalNumber(aps.constant_watt.value().watt_setpoint) == 5000);
            REQUIRE(aps.constant_watt.value().unit == dtsae::DERUnit::W);
            REQUIRE_FALSE(aps.frequency_droop.has_value());

            REQUIRE(dt::from_RationalNumber(mode.evse_maximum_charge_power.value()) == 22000);
            REQUIRE(dt::from_RationalNumber(mode.evse_maximum_discharge_power.value()) == 15000);
            REQUIRE(mode.required_der_operating_mode.value() == dtsae::RequiredDEROperatingMode::GridForming);
            REQUIRE(mode.grid_connection_mode.value() == dtsae::GridConnectionMode::GridIslanded);
        }

        THEN("It should serialize back to the same bytes") {
            message_20::DER_SAE_AC_ChargeLoopResponse res;
            res.header = message_20::Header{{0x3D, 0x4C, 0xBF, 0x93, 0x37, 0x4E, 0xD8, 0x9B}, 1725456324};
            res.response_code = dt::ResponseCode::OK;

            res.status = dt::EvseStatus{60, dt::EvseNotification::Pause};

            dt::MeterInfo meter_info;
            meter_info.meter_id = "EVSE-METER-01";
            meter_info.charged_energy_reading_wh = 12000;
            res.meter_info = meter_info;

            dt::Receipt receipt;
            receipt.time_anchor = 1725456324;
            res.receipt = receipt;

            res.target_frequency = dt::RationalNumber{60, 0};

            dtsae::DER_Dynamic_AC_CLResControlMode mode;
            mode.departure_time = 7200;
            mode.minimum_soc = 30;
            mode.target_soc = 80;
            mode.ack_max_delay = 10;
            mode.target_active_power = {10, 3};
            mode.target_reactive_power = {0, 0};
            mode.present_active_power = {9, 3};

            auto& der_control = mode.der_control_cl_res;

            dtsae::VoltageTrip voltage_trip;
            auto& ov_must = voltage_trip.over_voltage_must_trip_curve;
            ov_must.enable = true;
            ov_must.x_unit = dtsae::DERUnit::V;
            ov_must.y_unit = dtsae::DERUnit::s;
            ov_must.curve_data_points.push_back(dtsae::DataTuple{{264, 0}, {1, 0}});
            ov_must.curve_data_points.push_back(dtsae::DataTuple{{288, 0}, {2, -1}});
            auto& uv_must = voltage_trip.under_voltage_must_trip_curve;
            uv_must.enable = true;
            uv_must.x_unit = dtsae::DERUnit::V;
            uv_must.y_unit = dtsae::DERUnit::s;
            uv_must.curve_data_points.push_back(dtsae::DataTuple{{196, 0}, {2, 0}});
            uv_must.curve_data_points.push_back(dtsae::DataTuple{{160, 0}, {2, -1}});
            der_control.voltage_trip = voltage_trip;

            dtsae::FrequencyTrip frequency_trip;
            auto& of_must = frequency_trip.over_frequency_must_trip_curve;
            of_must.enable = true;
            of_must.x_unit = dtsae::DERUnit::Hz;
            of_must.y_unit = dtsae::DERUnit::s;
            of_must.curve_data_points.push_back(dtsae::DataTuple{{62, 0}, {1, 0}});
            of_must.curve_data_points.push_back(dtsae::DataTuple{{63, 0}, {5, -1}});
            auto& uf_must = frequency_trip.under_frequency_must_trip_curve;
            uf_must.enable = true;
            uf_must.x_unit = dtsae::DERUnit::Hz;
            uf_must.y_unit = dtsae::DERUnit::s;
            uf_must.curve_data_points.push_back(dtsae::DataTuple{{58, 0}, {1, 0}});
            uf_must.curve_data_points.push_back(dtsae::DataTuple{{57, 0}, {5, -1}});
            der_control.frequency_trip = frequency_trip;

            auto& enter_service = der_control.enter_service_cl_res;
            enter_service.permit_service = true;
            enter_service.enter_service_voltage_high = {253, 0};
            enter_service.enter_service_voltage_low = {207, 0};
            enter_service.enter_service_frequency_high = {61, 0};
            enter_service.enter_service_frequency_low = {59, 0};

            dtsae::ReactivePowerSupportCLRes reactive_support;
            dtsae::VoltVar volt_var;
            volt_var.enable = true;
            volt_var.priority = 1;
            volt_var.x_unit = dtsae::DERUnit::V;
            volt_var.y_unit = dtsae::DERUnit::var;
            volt_var.curve_data_points.push_back(dtsae::DataTuple{{207, 0}, {3, 3}});
            volt_var.curve_data_points.push_back(dtsae::DataTuple{{253, 0}, {-3, 3}});
            volt_var.open_loop_response_time = {5, 0};
            volt_var.reference_voltage = {230, 0};
            volt_var.autonomous_reference_voltage_adjustment_enable = false;
            volt_var.reference_voltage_adjustment_time_constant = 300;
            reactive_support.volt_var = volt_var;
            der_control.reactive_power_support_cl_res = reactive_support;

            dtsae::ActivePowerSupportCLRes active_support;
            dtsae::ConstantWatt constant_watt;
            constant_watt.enable = true;
            constant_watt.priority = 3;
            constant_watt.watt_setpoint = {5, 3};
            constant_watt.unit = dtsae::DERUnit::W;
            active_support.constant_watt = constant_watt;
            der_control.active_power_support_cl_res = active_support;

            mode.evse_maximum_charge_power = {22, 3};
            mode.evse_maximum_discharge_power = {15, 3};
            mode.required_der_operating_mode = dtsae::RequiredDEROperatingMode::GridForming;
            mode.grid_connection_mode = dtsae::GridConnectionMode::GridIslanded;

            res.control_mode = mode;

            REQUIRE(serialize_helper(res) == bytes);
        }
    }
}

SCENARIO("Se/Deserialize ac der sae charge loop request messages") {

    GIVEN("cl_req_scheduled_min") {

        std::vector<uint8_t> bytes = {0x80, 0x08, 0x04, 0x1e, 0xa6, 0x5f, 0xc9, 0x9b, 0xa7, 0x6c, 0x4d, 0x8c, 0x3b,
                                      0xfe, 0x1b, 0x60, 0x62, 0x89, 0x24, 0x18, 0x04, 0x94, 0x80, 0x0e, 0x60, 0x10,
                                      0x40, 0x01, 0xe1, 0x81, 0x36, 0x1d, 0xff, 0x0d, 0xb0, 0x31, 0x80, 0x00, 0x18};

        const io::StreamInputView stream_view{bytes.data(), bytes.size()};

        message_20::Variant variant(io::v2gtp::PayloadType::Part20DerSae, stream_view);

        THEN("It should be deserialized successfully") {
            REQUIRE(variant.get_type() == message_20::Type::DER_SAE_AC_ChargeLoopReq);

            const auto& msg = variant.get<message_20::DER_SAE_AC_ChargeLoopRequest>();
            const auto& header = msg.header;

            REQUIRE(header.session_id == std::array<uint8_t, 8>{0x3D, 0x4C, 0xBF, 0x93, 0x37, 0x4E, 0xD8, 0x9B});
            REQUIRE(header.timestamp == 1725456323);
            REQUIRE(msg.meter_info_requested == false);

            REQUIRE(std::holds_alternative<dtsae::DER_Scheduled_AC_CLReqControlMode>(msg.control_mode));
            const auto& mode = std::get<dtsae::DER_Scheduled_AC_CLReqControlMode>(msg.control_mode);

            REQUIRE(dt::from_RationalNumber(mode.present_active_power) == 9000);
            REQUIRE(dt::from_RationalNumber(mode.present_voltage) == 230);
            REQUIRE(dt::from_RationalNumber(mode.present_frequency) == 60);
            REQUIRE(mode.der_operational_state == dtsae::DEROperationalState::On);
            REQUIRE(mode.der_connection_status == dtsae::DERConnectionStatus::Connected);
            REQUIRE(mode.update_time == 1725456323);
            REQUIRE(mode.der_alarm_status == 0);
            REQUIRE(mode.enabled_modes == 3);

            // absent optionals
            REQUIRE_FALSE(msg.display_parameters.has_value());
            REQUIRE_FALSE(mode.target_energy_request.has_value());
            REQUIRE_FALSE(mode.max_charge_power.has_value());
            REQUIRE_FALSE(mode.present_active_power_L2.has_value());
            REQUIRE_FALSE(mode.maximum_discharge_power.has_value());
            REQUIRE_FALSE(mode.apparent_power.has_value());
            REQUIRE_FALSE(mode.reactive_power.has_value());
            REQUIRE_FALSE(mode.excitation.has_value());
            REQUIRE_FALSE(mode.minimum_charging_duration.has_value());
            REQUIRE_FALSE(mode.duration_maximum_charge_rate.has_value());
            REQUIRE_FALSE(mode.duration_maximum_discharge_rate.has_value());
        }

        THEN("It should serialize back to the same bytes") {
            message_20::DER_SAE_AC_ChargeLoopRequest req;
            req.header = message_20::Header{{0x3D, 0x4C, 0xBF, 0x93, 0x37, 0x4E, 0xD8, 0x9B}, 1725456323};
            req.meter_info_requested = false;

            dtsae::DER_Scheduled_AC_CLReqControlMode mode;
            mode.present_active_power = {9, 3};
            mode.present_voltage = {230, 0};
            mode.present_frequency = {60, 0};
            mode.der_operational_state = dtsae::DEROperationalState::On;
            mode.der_connection_status = dtsae::DERConnectionStatus::Connected;
            mode.update_time = 1725456323;
            mode.der_alarm_status = 0;
            mode.enabled_modes = 3;
            req.control_mode = mode;

            REQUIRE(serialize_helper(req) == bytes);
        }
    }

    GIVEN("cl_req_dynamic_min") {

        std::vector<uint8_t> bytes = {
            0x80, 0x08, 0x04, 0x1e, 0xa6, 0x5f, 0xc9, 0x9b, 0xa7, 0x6c, 0x4d, 0x8c, 0x3b, 0xfe, 0x1b, 0x60, 0x62, 0x86,
            0x90, 0x60, 0x50, 0x08, 0x30, 0x3c, 0x04, 0x18, 0x05, 0x02, 0x0c, 0x02, 0xc8, 0x80, 0x06, 0x42, 0x20, 0xc0,
            0x24, 0x88, 0x00, 0x00, 0x22, 0x0c, 0x02, 0xc8, 0x80, 0x06, 0x42, 0x20, 0x03, 0x98, 0x04, 0x10, 0x00, 0x78,
            0x60, 0x46, 0x1d, 0xff, 0x0d, 0xb0, 0x30, 0xd8, 0x04, 0x11, 0x01, 0xc2, 0x20, 0x38, 0x00, 0x00, 0x30};

        const io::StreamInputView stream_view{bytes.data(), bytes.size()};

        message_20::Variant variant(io::v2gtp::PayloadType::Part20DerSae, stream_view);

        THEN("It should be deserialized successfully") {
            REQUIRE(variant.get_type() == message_20::Type::DER_SAE_AC_ChargeLoopReq);

            const auto& msg = variant.get<message_20::DER_SAE_AC_ChargeLoopRequest>();
            const auto& header = msg.header;

            REQUIRE(header.session_id == std::array<uint8_t, 8>{0x3D, 0x4C, 0xBF, 0x93, 0x37, 0x4E, 0xD8, 0x9B});
            REQUIRE(header.timestamp == 1725456323);
            REQUIRE(msg.meter_info_requested == false);

            REQUIRE(std::holds_alternative<dtsae::DER_Dynamic_AC_CLReqControlMode>(msg.control_mode));
            const auto& mode = std::get<dtsae::DER_Dynamic_AC_CLReqControlMode>(msg.control_mode);

            // Dynamic mode mandatory (1,1) fields
            REQUIRE(dt::from_RationalNumber(mode.target_energy_request) == 40000);
            REQUIRE(dt::from_RationalNumber(mode.max_energy_request) == 60000);
            REQUIRE(dt::from_RationalNumber(mode.min_energy_request) == 10000);
            REQUIRE(dt::from_RationalNumber(mode.max_charge_power) == 11000);
            REQUIRE(dt::from_RationalNumber(mode.min_charge_power) == 100);
            REQUIRE(dt::from_RationalNumber(mode.present_active_power) == 9000);
            REQUIRE(dt::from_RationalNumber(mode.present_reactive_power) == 0);
            REQUIRE(dt::from_RationalNumber(mode.maximum_discharge_power) == 11000);
            REQUIRE(dt::from_RationalNumber(mode.minimum_discharge_power) == 100);
            REQUIRE(dt::from_RationalNumber(mode.present_voltage) == 230);
            REQUIRE(dt::from_RationalNumber(mode.present_frequency) == 60);
            REQUIRE(mode.der_operational_state == dtsae::DEROperationalState::On);
            REQUIRE(mode.der_connection_status == dtsae::DERConnectionStatus::Connected);
            REQUIRE(mode.update_time == 1725456323);
            REQUIRE(mode.minimum_charging_duration == 600);
            REQUIRE(mode.duration_maximum_charge_rate == 1800);
            REQUIRE(mode.duration_maximum_discharge_rate == 1800);
            REQUIRE(mode.der_alarm_status == 0);
            REQUIRE(mode.enabled_modes == 3);

            // absent optionals
            REQUIRE_FALSE(msg.display_parameters.has_value());
            REQUIRE_FALSE(mode.departure_time.has_value());
            REQUIRE_FALSE(mode.max_charge_power_L2.has_value());
            REQUIRE_FALSE(mode.session_total_discharge_energy_available.has_value());
            REQUIRE_FALSE(mode.apparent_power.has_value());
            REQUIRE_FALSE(mode.reactive_power.has_value());
            REQUIRE_FALSE(mode.excitation.has_value());
            REQUIRE_FALSE(mode.maximum_v2x_energy_request.has_value());
            REQUIRE_FALSE(mode.minimum_v2x_energy_request.has_value());
        }

        THEN("It should serialize back to the same bytes") {
            message_20::DER_SAE_AC_ChargeLoopRequest req;
            req.header = message_20::Header{{0x3D, 0x4C, 0xBF, 0x93, 0x37, 0x4E, 0xD8, 0x9B}, 1725456323};
            req.meter_info_requested = false;

            dtsae::DER_Dynamic_AC_CLReqControlMode mode;
            mode.target_energy_request = {40, 3};
            mode.max_energy_request = {60, 3};
            mode.min_energy_request = {10, 3};
            mode.max_charge_power = {11, 3};
            mode.min_charge_power = {100, 0};
            mode.present_active_power = {9, 3};
            mode.present_reactive_power = {0, 0};
            mode.maximum_discharge_power = {11, 3};
            mode.minimum_discharge_power = {100, 0};
            mode.present_voltage = {230, 0};
            mode.present_frequency = {60, 0};
            mode.der_operational_state = dtsae::DEROperationalState::On;
            mode.der_connection_status = dtsae::DERConnectionStatus::Connected;
            mode.update_time = 1725456323;
            mode.minimum_charging_duration = 600;
            mode.duration_maximum_charge_rate = 1800;
            mode.duration_maximum_discharge_rate = 1800;
            mode.der_alarm_status = 0;
            mode.enabled_modes = 3;
            req.control_mode = mode;

            REQUIRE(serialize_helper(req) == bytes);
        }
    }

    GIVEN("cl_req_scheduled_optionals") {

        std::vector<uint8_t> bytes = {
            0x80, 0x08, 0x04, 0x1e, 0xa6, 0x5f, 0xc9, 0x9b, 0xa7, 0x6c, 0x4d, 0x8c, 0x3b, 0xfe, 0x1b, 0x60,
            0x62, 0x01, 0xb8, 0x07, 0x80, 0xa0, 0x41, 0x14, 0x02, 0x0c, 0x0a, 0x00, 0x20, 0xc0, 0xf0, 0x02,
            0x0c, 0x02, 0x80, 0x41, 0x80, 0x58, 0x08, 0x30, 0x0b, 0x01, 0x06, 0x01, 0x60, 0x20, 0x01, 0x90,
            0x88, 0x30, 0x09, 0x01, 0x06, 0x01, 0x20, 0x20, 0xc0, 0x24, 0x04, 0x00, 0x00, 0x11, 0x00, 0x1c,
            0xc0, 0x20, 0x80, 0x03, 0xc0, 0x10, 0x60, 0x16, 0x22, 0x00, 0x19, 0x08, 0x10, 0x10, 0x60, 0x18,
            0x44, 0x18, 0x06, 0x11, 0x06, 0x01, 0x84, 0x41, 0x80, 0x61, 0x02, 0x0c, 0x01, 0x4a, 0x41, 0x80,
            0x29, 0x48, 0x30, 0x05, 0x29, 0x06, 0x00, 0xa5, 0x19, 0x06, 0x01, 0x05, 0x20, 0xc0, 0x20, 0x8c,
            0x3b, 0xfe, 0x1b, 0x60, 0x60, 0x6c, 0x02, 0x04, 0x40, 0x70, 0x44, 0x07, 0x00, 0x00, 0x06, 0x00};

        const io::StreamInputView stream_view{bytes.data(), bytes.size()};

        message_20::Variant variant(io::v2gtp::PayloadType::Part20DerSae, stream_view);

        THEN("It should be deserialized successfully") {
            REQUIRE(variant.get_type() == message_20::Type::DER_SAE_AC_ChargeLoopReq);

            const auto& msg = variant.get<message_20::DER_SAE_AC_ChargeLoopRequest>();
            const auto& header = msg.header;

            REQUIRE(header.session_id == std::array<uint8_t, 8>{0x3D, 0x4C, 0xBF, 0x93, 0x37, 0x4E, 0xD8, 0x9B});
            REQUIRE(header.timestamp == 1725456323);
            REQUIRE(msg.meter_info_requested == true);

            REQUIRE(msg.display_parameters.has_value());
            REQUIRE(msg.display_parameters.value().present_soc.value() == 55);
            REQUIRE(msg.display_parameters.value().min_soc.value() == 30);
            REQUIRE(msg.display_parameters.value().target_soc.value() == 80);
            REQUIRE(msg.display_parameters.value().charging_complete.value() == false);

            REQUIRE(std::holds_alternative<dtsae::DER_Scheduled_AC_CLReqControlMode>(msg.control_mode));
            const auto& mode = std::get<dtsae::DER_Scheduled_AC_CLReqControlMode>(msg.control_mode);

            REQUIRE(dt::from_RationalNumber(mode.target_energy_request.value()) == 40000);
            REQUIRE(dt::from_RationalNumber(mode.max_energy_request.value()) == 60000);
            REQUIRE(dt::from_RationalNumber(mode.min_energy_request.value()) == 10000);
            REQUIRE(dt::from_RationalNumber(mode.max_charge_power.value()) == 11000);
            REQUIRE(dt::from_RationalNumber(mode.max_charge_power_L2.value()) == 11000);
            REQUIRE(dt::from_RationalNumber(mode.max_charge_power_L3.value()) == 11000);
            REQUIRE(dt::from_RationalNumber(mode.min_charge_power.value()) == 100);
            REQUIRE(dt::from_RationalNumber(mode.present_active_power) == 9000);
            REQUIRE(dt::from_RationalNumber(mode.present_active_power_L2.value()) == 9000);
            REQUIRE(dt::from_RationalNumber(mode.present_active_power_L3.value()) == 9000);
            REQUIRE(dt::from_RationalNumber(mode.present_reactive_power.value()) == 0);
            REQUIRE(dt::from_RationalNumber(mode.present_voltage) == 230);
            REQUIRE(dt::from_RationalNumber(mode.present_frequency) == 60);
            REQUIRE(dt::from_RationalNumber(mode.maximum_discharge_power.value()) == 11000);
            REQUIRE(dt::from_RationalNumber(mode.minimum_discharge_power.value()) == 100);
            REQUIRE(mode.der_operational_state == dtsae::DEROperationalState::On);
            REQUIRE(mode.der_connection_status == dtsae::DERConnectionStatus::Connected);

            REQUIRE(mode.apparent_power.has_value());
            const auto& ap = mode.apparent_power.value();
            REQUIRE(dt::from_RationalNumber(ap.maximum_apparent_power_during_charging_and_var_absorption) == 12000);
            REQUIRE(dt::from_RationalNumber(ap.maximum_apparent_power_during_charging_and_var_injection) == 12000);
            REQUIRE(dt::from_RationalNumber(ap.maximum_apparent_power_during_discharging_and_var_absorption) == 12000);
            REQUIRE(dt::from_RationalNumber(ap.maximum_apparent_power_during_discharging_and_var_injection) == 12000);
            REQUIRE_FALSE(ap.maximum_apparent_power_during_charging_and_var_absorption_L2.has_value());

            REQUIRE(mode.reactive_power.has_value());
            const auto& rp = mode.reactive_power.value();
            REQUIRE(dt::from_RationalNumber(rp.maximum_var_absorption_during_charging) == 5000);
            REQUIRE(dt::from_RationalNumber(rp.maximum_var_injection_during_charging) == 5000);
            REQUIRE(dt::from_RationalNumber(rp.maximum_var_absorption_during_discharging) == 5000);
            REQUIRE(dt::from_RationalNumber(rp.maximum_var_injection_during_discharging) == 5000);
            REQUIRE_FALSE(rp.minimum_var_absorption_during_charging.has_value());

            REQUIRE(mode.excitation.has_value());
            const auto& ex = mode.excitation.value();
            REQUIRE(dt::from_RationalNumber(ex.specified_over_excited_discharge_power) == 8000);
            REQUIRE(dt::from_RationalNumber(ex.specified_under_excited_discharge_power) == 8000);
            REQUIRE_FALSE(ex.specified_over_excited_power_factor.has_value());

            REQUIRE(mode.update_time == 1725456323);
            REQUIRE(mode.minimum_charging_duration == 600);
            REQUIRE(mode.duration_maximum_charge_rate == 1800);
            REQUIRE(mode.duration_maximum_discharge_rate == 1800);
            REQUIRE(mode.der_alarm_status == 0);
            REQUIRE(mode.enabled_modes == 3);
        }

        THEN("It should serialize back to the same bytes") {
            message_20::DER_SAE_AC_ChargeLoopRequest req;
            req.header = message_20::Header{{0x3D, 0x4C, 0xBF, 0x93, 0x37, 0x4E, 0xD8, 0x9B}, 1725456323};
            req.meter_info_requested = true;

            dt::DisplayParameters display_parameters;
            display_parameters.present_soc = 55;
            display_parameters.min_soc = 30;
            display_parameters.target_soc = 80;
            display_parameters.charging_complete = false;
            req.display_parameters = display_parameters;

            dtsae::DER_Scheduled_AC_CLReqControlMode mode;
            mode.target_energy_request = dt::RationalNumber{40, 3};
            mode.max_energy_request = dt::RationalNumber{60, 3};
            mode.min_energy_request = dt::RationalNumber{10, 3};
            mode.max_charge_power = dt::RationalNumber{11, 3};
            mode.max_charge_power_L2 = dt::RationalNumber{11, 3};
            mode.max_charge_power_L3 = dt::RationalNumber{11, 3};
            mode.min_charge_power = dt::RationalNumber{100, 0};
            mode.present_active_power = {9, 3};
            mode.present_active_power_L2 = dt::RationalNumber{9, 3};
            mode.present_active_power_L3 = dt::RationalNumber{9, 3};
            mode.present_reactive_power = dt::RationalNumber{0, 0};
            mode.present_voltage = {230, 0};
            mode.present_frequency = {60, 0};
            mode.maximum_discharge_power = dt::RationalNumber{11, 3};
            mode.minimum_discharge_power = dt::RationalNumber{100, 0};
            mode.der_operational_state = dtsae::DEROperationalState::On;
            mode.der_connection_status = dtsae::DERConnectionStatus::Connected;

            dtsae::EVApparentPower apparent_power;
            apparent_power.maximum_apparent_power_during_charging_and_var_absorption = {12, 3};
            apparent_power.maximum_apparent_power_during_charging_and_var_injection = {12, 3};
            apparent_power.maximum_apparent_power_during_discharging_and_var_absorption = {12, 3};
            apparent_power.maximum_apparent_power_during_discharging_and_var_injection = {12, 3};
            mode.apparent_power = apparent_power;

            dtsae::EVReactivePower reactive_power;
            reactive_power.maximum_var_absorption_during_charging = {5, 3};
            reactive_power.maximum_var_injection_during_charging = {5, 3};
            reactive_power.maximum_var_absorption_during_discharging = {5, 3};
            reactive_power.maximum_var_injection_during_discharging = {5, 3};
            mode.reactive_power = reactive_power;

            dtsae::EVExcitation excitation;
            excitation.specified_over_excited_discharge_power = {8, 3};
            excitation.specified_under_excited_discharge_power = {8, 3};
            mode.excitation = excitation;

            mode.update_time = 1725456323;
            mode.minimum_charging_duration = 600;
            mode.duration_maximum_charge_rate = 1800;
            mode.duration_maximum_discharge_rate = 1800;
            mode.der_alarm_status = 0;
            mode.enabled_modes = 3;
            req.control_mode = mode;

            REQUIRE(serialize_helper(req) == bytes);
        }
    }

    GIVEN("cl_req_dynamic_optionals") {

        std::vector<uint8_t> bytes = {
            0x80, 0x08, 0x04, 0x1e, 0xa6, 0x5f, 0xc9, 0x9b, 0xa7, 0x6c, 0x4d, 0x8c, 0x3b, 0xfe, 0x1b, 0x60, 0x62,
            0x01, 0xb8, 0x54, 0x0e, 0x4c, 0x50, 0x1c, 0x04, 0x18, 0x14, 0x02, 0x0c, 0x0f, 0x01, 0x06, 0x01, 0x40,
            0x83, 0x00, 0xb0, 0x20, 0xc0, 0x2c, 0x08, 0x30, 0x0b, 0x04, 0x00, 0x32, 0x11, 0x06, 0x01, 0x24, 0x40,
            0x00, 0x01, 0x10, 0x60, 0x16, 0x44, 0x00, 0x32, 0x11, 0x00, 0x1c, 0xc0, 0x20, 0x80, 0x03, 0xc0, 0x10,
            0x60, 0x3c, 0x01, 0x06, 0x01, 0x84, 0x41, 0x80, 0x61, 0x10, 0x60, 0x18, 0x44, 0x18, 0x06, 0x11, 0x64,
            0x18, 0x04, 0x14, 0x83, 0x00, 0x82, 0x08, 0x30, 0x14, 0x02, 0x0c, 0x01, 0x40, 0x23, 0x0e, 0xff, 0x86,
            0xd8, 0x18, 0x6c, 0x02, 0x08, 0x80, 0xe1, 0x10, 0x1c, 0x00, 0x00, 0x18};

        const io::StreamInputView stream_view{bytes.data(), bytes.size()};

        message_20::Variant variant(io::v2gtp::PayloadType::Part20DerSae, stream_view);

        THEN("It should be deserialized successfully") {
            REQUIRE(variant.get_type() == message_20::Type::DER_SAE_AC_ChargeLoopReq);

            const auto& msg = variant.get<message_20::DER_SAE_AC_ChargeLoopRequest>();
            const auto& header = msg.header;

            REQUIRE(header.session_id == std::array<uint8_t, 8>{0x3D, 0x4C, 0xBF, 0x93, 0x37, 0x4E, 0xD8, 0x9B});
            REQUIRE(header.timestamp == 1725456323);
            REQUIRE(msg.meter_info_requested == true);

            REQUIRE(msg.display_parameters.has_value());
            REQUIRE(msg.display_parameters.value().present_soc.value() == 55);
            REQUIRE(msg.display_parameters.value().target_soc.value() == 80);
            REQUIRE_FALSE(msg.display_parameters.value().min_soc.has_value());

            REQUIRE(std::holds_alternative<dtsae::DER_Dynamic_AC_CLReqControlMode>(msg.control_mode));
            const auto& mode = std::get<dtsae::DER_Dynamic_AC_CLReqControlMode>(msg.control_mode);

            REQUIRE(mode.departure_time.value() == 7200);
            REQUIRE(dt::from_RationalNumber(mode.target_energy_request) == 40000);
            REQUIRE(dt::from_RationalNumber(mode.max_energy_request) == 60000);
            REQUIRE(dt::from_RationalNumber(mode.min_energy_request) == 10000);
            REQUIRE(dt::from_RationalNumber(mode.max_charge_power) == 11000);
            REQUIRE(dt::from_RationalNumber(mode.max_charge_power_L2.value()) == 11000);
            REQUIRE(dt::from_RationalNumber(mode.max_charge_power_L3.value()) == 11000);
            REQUIRE(dt::from_RationalNumber(mode.min_charge_power) == 100);
            REQUIRE(dt::from_RationalNumber(mode.present_active_power) == 9000);
            REQUIRE(dt::from_RationalNumber(mode.present_reactive_power) == 0);
            REQUIRE(dt::from_RationalNumber(mode.maximum_discharge_power) == 11000);
            REQUIRE(dt::from_RationalNumber(mode.minimum_discharge_power) == 100);
            REQUIRE(dt::from_RationalNumber(mode.present_voltage) == 230);
            REQUIRE(dt::from_RationalNumber(mode.present_frequency) == 60);
            REQUIRE(dt::from_RationalNumber(mode.session_total_discharge_energy_available.value()) == 30000);

            REQUIRE(mode.apparent_power.has_value());
            const auto& ap = mode.apparent_power.value();
            REQUIRE(dt::from_RationalNumber(ap.maximum_apparent_power_during_charging_and_var_absorption) == 12000);
            REQUIRE(dt::from_RationalNumber(ap.maximum_apparent_power_during_discharging_and_var_injection) == 12000);

            REQUIRE_FALSE(mode.reactive_power.has_value());

            REQUIRE(mode.excitation.has_value());
            const auto& ex = mode.excitation.value();
            REQUIRE(dt::from_RationalNumber(ex.specified_over_excited_discharge_power) == 8000);
            REQUIRE(dt::from_RationalNumber(ex.specified_under_excited_discharge_power) == 8000);

            REQUIRE(dt::from_RationalNumber(mode.maximum_v2x_energy_request.value()) == 20000);
            REQUIRE(dt::from_RationalNumber(mode.minimum_v2x_energy_request.value()) == 5000);

            REQUIRE(mode.der_operational_state == dtsae::DEROperationalState::On);
            REQUIRE(mode.der_connection_status == dtsae::DERConnectionStatus::Connected);
            REQUIRE(mode.update_time == 1725456323);
            REQUIRE(mode.minimum_charging_duration == 600);
            REQUIRE(mode.duration_maximum_charge_rate == 1800);
            REQUIRE(mode.duration_maximum_discharge_rate == 1800);
            REQUIRE(mode.der_alarm_status == 0);
            REQUIRE(mode.enabled_modes == 3);
        }

        THEN("It should serialize back to the same bytes") {
            message_20::DER_SAE_AC_ChargeLoopRequest req;
            req.header = message_20::Header{{0x3D, 0x4C, 0xBF, 0x93, 0x37, 0x4E, 0xD8, 0x9B}, 1725456323};
            req.meter_info_requested = true;

            dt::DisplayParameters display_parameters;
            display_parameters.present_soc = 55;
            display_parameters.target_soc = 80;
            req.display_parameters = display_parameters;

            dtsae::DER_Dynamic_AC_CLReqControlMode mode;
            mode.departure_time = 7200;
            mode.target_energy_request = {40, 3};
            mode.max_energy_request = {60, 3};
            mode.min_energy_request = {10, 3};
            mode.max_charge_power = {11, 3};
            mode.max_charge_power_L2 = dt::RationalNumber{11, 3};
            mode.max_charge_power_L3 = dt::RationalNumber{11, 3};
            mode.min_charge_power = {100, 0};
            mode.present_active_power = {9, 3};
            mode.present_reactive_power = {0, 0};
            mode.maximum_discharge_power = {11, 3};
            mode.minimum_discharge_power = {100, 0};
            mode.present_voltage = {230, 0};
            mode.present_frequency = {60, 0};
            mode.session_total_discharge_energy_available = dt::RationalNumber{30, 3};

            dtsae::EVApparentPower apparent_power;
            apparent_power.maximum_apparent_power_during_charging_and_var_absorption = {12, 3};
            apparent_power.maximum_apparent_power_during_charging_and_var_injection = {12, 3};
            apparent_power.maximum_apparent_power_during_discharging_and_var_absorption = {12, 3};
            apparent_power.maximum_apparent_power_during_discharging_and_var_injection = {12, 3};
            mode.apparent_power = apparent_power;

            dtsae::EVExcitation excitation;
            excitation.specified_over_excited_discharge_power = {8, 3};
            excitation.specified_under_excited_discharge_power = {8, 3};
            mode.excitation = excitation;

            mode.maximum_v2x_energy_request = dt::RationalNumber{20, 3};
            mode.minimum_v2x_energy_request = dt::RationalNumber{5, 3};

            mode.der_operational_state = dtsae::DEROperationalState::On;
            mode.der_connection_status = dtsae::DERConnectionStatus::Connected;
            mode.update_time = 1725456323;
            mode.minimum_charging_duration = 600;
            mode.duration_maximum_charge_rate = 1800;
            mode.duration_maximum_discharge_rate = 1800;
            mode.der_alarm_status = 0;
            mode.enabled_modes = 3;
            req.control_mode = mode;

            REQUIRE(serialize_helper(req) == bytes);
        }
    }
}
