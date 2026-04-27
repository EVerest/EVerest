#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <iso15118/message/ac_der_iec_charge_parameter_discovery.hpp>
#include <iso15118/message/variant.hpp>

#include "helper.hpp"

using namespace iso15118;

namespace dt = message_20::datatypes;

SCENARIO("Se/Deserialize ac der iec charge parameter discovery messages") {

    GIVEN("Deserialize ac_der_iec_charge_parameter_discovery_req") {

        uint8_t doc_raw[] = {0x80, 0x10, 0x04, 0x1e, 0xa6, 0x5f, 0xc9, 0x9b, 0xa7, 0x6c, 0x4d, 0x8c,
                             0x3b, 0xfe, 0x1b, 0x60, 0x63, 0x07, 0xe0, 0x80, 0x19, 0x02, 0x00, 0x00,
                             0x00, 0x80, 0x00, 0x00, 0x3f, 0x06, 0x80, 0x78, 0x10, 0x00, 0x00, 0x04,
                             0x00, 0x00, 0x02, 0x0f, 0xc1, 0x00, 0x32, 0x43, 0xf0, 0x68, 0x07, 0x90};

        const io::StreamInputView stream_view{doc_raw, sizeof(doc_raw)};

        message_20::Variant variant(io::v2gtp::PayloadType::Part20DerIec, stream_view);

        THEN("It should be deserialized successfully") {
            REQUIRE(variant.get_type() == message_20::Type::DER_AC_ChargeParameterDiscoveryReq);

            const auto& msg = variant.get<message_20::DER_AC_ChargeParameterDiscoveryRequest>();
            const auto& header = msg.header;

            REQUIRE(header.session_id == std::array<uint8_t, 8>{0x3D, 0x4C, 0xBF, 0x93, 0x37, 0x4E, 0xD8, 0x9B});
            REQUIRE(header.timestamp == 1725456323);

            REQUIRE(message_20::datatypes::from_RationalNumber(msg.transfer_mode.max_charge_power) == 32);
            REQUIRE(message_20::datatypes::from_RationalNumber(
                        msg.transfer_mode.max_charge_power_L2.value_or({1, 0})) == 0.0f);
            REQUIRE(message_20::datatypes::from_RationalNumber(
                        msg.transfer_mode.max_charge_power_L3.value_or({1, 0})) == 0.0f);
            REQUIRE(message_20::datatypes::from_RationalNumber(msg.transfer_mode.min_charge_power) == 20);
            REQUIRE(message_20::datatypes::from_RationalNumber(
                        msg.transfer_mode.min_charge_power_L2.value_or({1, 0})) == 0.0f);
            REQUIRE(message_20::datatypes::from_RationalNumber(
                        msg.transfer_mode.min_charge_power_L3.value_or({1, 0})) == 0.0f);
            REQUIRE(message_20::datatypes::from_RationalNumber(msg.transfer_mode.max_discharge_power) == 32);
            REQUIRE(message_20::datatypes::from_RationalNumber(msg.transfer_mode.min_discharge_power) == 20);
        }
    }

    GIVEN("Serialize ac_der_iec_charge_parameter_discovery_req") {

        message_20::DER_AC_ChargeParameterDiscoveryRequest req;

        req.header = message_20::Header{{0x3D, 0x4C, 0xBF, 0x93, 0x37, 0x4E, 0xD8, 0x9B}, 1725456323};
        auto& mode = req.transfer_mode;
        mode.processing = dt::Processing::Ongoing;
        mode.max_charge_power = {3200, -2};
        mode.max_charge_power_L2 = {0, 0};
        mode.max_charge_power_L3 = {0, 0};
        mode.min_charge_power = {2000, -2};
        mode.min_charge_power_L2 = {0, 0};
        mode.min_charge_power_L3 = {0, 0};
        mode.max_discharge_power = {3200, -2};
        mode.min_discharge_power = {2000, -2};

        std::vector<uint8_t> expected = {0x80, 0x10, 0x04, 0x1e, 0xa6, 0x5f, 0xc9, 0x9b, 0xa7, 0x6c, 0x4d, 0x8c,
                                         0x3b, 0xfe, 0x1b, 0x60, 0x63, 0x07, 0xe0, 0x80, 0x19, 0x02, 0x00, 0x00,
                                         0x00, 0x80, 0x00, 0x00, 0x3f, 0x06, 0x80, 0x78, 0x10, 0x00, 0x00, 0x04,
                                         0x00, 0x00, 0x02, 0x0f, 0xc1, 0x00, 0x32, 0x43, 0xf0, 0x68, 0x07, 0x90};

        THEN("It should be serialized successfully") {
            REQUIRE(serialize_helper(req) == expected);
        }
    }

    GIVEN("Deserialize ac_der_iec_charge_parameter_discovery_res") {

        uint8_t doc_raw[] = {0x80, 0x14, 0x04, 0x1e, 0xa6, 0x5f, 0xc9, 0x9b, 0xa7, 0x6c, 0x4d, 0x8c, 0x4b, 0xfe, 0x1b,
                             0x60, 0x62, 0x00, 0x83, 0xf0, 0x40, 0x0c, 0x90, 0xfc, 0x1a, 0x01, 0xe4, 0x40, 0x01, 0x91,
                             0x47, 0xe0, 0x80, 0x19, 0x21, 0xf8, 0x20, 0x06, 0x48, 0x7e, 0x08, 0x01, 0x92, 0x00, 0xc0};

        const io::StreamInputView stream_view{doc_raw, sizeof(doc_raw)};

        message_20::Variant variant(io::v2gtp::PayloadType::Part20DerIec, stream_view);

        THEN("It should be deserialized successfully") {
            REQUIRE(variant.get_type() == message_20::Type::DER_AC_ChargeParameterDiscoveryRes);

            const auto& msg = variant.get<message_20::DER_AC_ChargeParameterDiscoveryResponse>();
            const auto& header = msg.header;

            REQUIRE(header.session_id == std::array<uint8_t, 8>{0x3D, 0x4C, 0xBF, 0x93, 0x37, 0x4E, 0xD8, 0x9B});
            REQUIRE(header.timestamp == 1725456324);
            REQUIRE(msg.response_code == message_20::datatypes::ResponseCode::OK);

            const auto& transfer_mode = msg.transfer_mode;
            REQUIRE(message_20::datatypes::from_RationalNumber(transfer_mode.max_charge_power) == 32);
            REQUIRE(message_20::datatypes::from_RationalNumber(transfer_mode.min_charge_power) == 20);
            REQUIRE(dt::from_RationalNumber(transfer_mode.nominal_frequency) == 50);
            REQUIRE(dt::from_RationalNumber(transfer_mode.nominal_charge_power) == 32);
            REQUIRE(dt::from_RationalNumber(transfer_mode.nominal_discharge_power) == 32);
            REQUIRE(dt::from_RationalNumber(transfer_mode.max_discharge_power) == 32);
            REQUIRE(transfer_mode.operating_mode == dt::OperatingMode::GridFollowing);
            REQUIRE(transfer_mode.grid_connection_mode == dt::GridConnectionMode::GridConnected);

            const auto& der_control = transfer_mode.der_control;
            REQUIRE_FALSE(der_control.over_voltage_fault_ride_through.has_value());
            REQUIRE_FALSE(der_control.under_voltage_fault_ride_through.has_value());
            REQUIRE_FALSE(der_control.zero_current.has_value());
            REQUIRE_FALSE(der_control.reactive_power_support.has_value());
            REQUIRE_FALSE(der_control.active_power_support.has_value());
            REQUIRE_FALSE(der_control.max_level_dc_injection.has_value());
        }
    }

    GIVEN("Deserialize ac_der_iec_charge_parameter_discovery_res + der control") {

        uint8_t doc_raw[] = {0x80, 0x14, 0x04, 0x1e, 0xa6, 0x5f, 0xc9, 0x9b, 0xa7, 0x6c, 0x4d, 0x8c, 0x4b, 0xfe, 0x1b,
                             0x60, 0x62, 0x00, 0x83, 0xf0, 0x40, 0x0c, 0x90, 0xfc, 0x1a, 0x01, 0xe4, 0x40, 0x01, 0x91,
                             0x47, 0xe0, 0x80, 0x19, 0x21, 0xf8, 0x20, 0x06, 0x48, 0x7e, 0x08, 0x01, 0x92, 0x00, 0x01,
                             0xf8, 0x3e, 0x15, 0x48, 0x04, 0x00, 0x00, 0x80, 0x20, 0x00, 0x04, 0x20, 0x02, 0x01, 0x00,
                             0x19, 0xe0, 0x20, 0x41, 0x01, 0x00, 0x82, 0x00, 0x3f, 0x40, 0x40, 0x82, 0x11, 0xf1, 0x30,
                             0x08, 0x00, 0x02, 0x10, 0x43, 0xf0, 0x49, 0x13, 0x81, 0xf8, 0x34, 0x0a, 0x04, 0x7e, 0x02,
                             0x82, 0x00, 0x81, 0x00, 0x00, 0x21, 0x07, 0xd0, 0xf4, 0x03, 0x00};

        const io::StreamInputView stream_view{doc_raw, sizeof(doc_raw)};

        message_20::Variant variant(io::v2gtp::PayloadType::Part20DerIec, stream_view); // Getting an error right now

        THEN("It should be deserialized successfully") {
            REQUIRE(variant.get_type() == message_20::Type::DER_AC_ChargeParameterDiscoveryRes);

            const auto& msg = variant.get<message_20::DER_AC_ChargeParameterDiscoveryResponse>();
            const auto& header = msg.header;

            REQUIRE(header.session_id == std::array<uint8_t, 8>{0x3D, 0x4C, 0xBF, 0x93, 0x37, 0x4E, 0xD8, 0x9B});
            REQUIRE(header.timestamp == 1725456324);
            REQUIRE(msg.response_code == message_20::datatypes::ResponseCode::OK);

            const auto& transfer_mode = msg.transfer_mode;
            REQUIRE(message_20::datatypes::from_RationalNumber(transfer_mode.max_charge_power) == 32);
            REQUIRE(message_20::datatypes::from_RationalNumber(transfer_mode.min_charge_power) == 20);
            REQUIRE(dt::from_RationalNumber(transfer_mode.nominal_frequency) == 50);
            REQUIRE(dt::from_RationalNumber(transfer_mode.nominal_charge_power) == 32);
            REQUIRE(dt::from_RationalNumber(transfer_mode.nominal_discharge_power) == 32);
            REQUIRE(dt::from_RationalNumber(transfer_mode.max_discharge_power) == 32);
            REQUIRE(transfer_mode.operating_mode == dt::OperatingMode::GridFollowing);
            REQUIRE(transfer_mode.grid_connection_mode == dt::GridConnectionMode::GridConnected);

            const auto& der_control = transfer_mode.der_control;
            REQUIRE(der_control.over_voltage_fault_ride_through.has_value());
            REQUIRE_FALSE(der_control.under_voltage_fault_ride_through.has_value());
            REQUIRE_FALSE(der_control.zero_current.has_value());
            REQUIRE(der_control.reactive_power_support.has_value());
            REQUIRE(der_control.active_power_support.has_value());
            REQUIRE(der_control.max_level_dc_injection.has_value());
        }
    }

    GIVEN("Deserialize ac_der_iec_charge_parameter_discovery_res + der control 2") {

        uint8_t doc_raw[] = {0x80, 0x14, 0x04, 0x1e, 0xa6, 0x5f, 0xc9, 0x9b, 0xa7, 0x6c, 0x4d, 0x8c, 0x4b, 0xfe,
                             0x1b, 0x60, 0x62, 0x00, 0x83, 0xf0, 0x40, 0x0c, 0x90, 0xfc, 0x1a, 0x01, 0xe4, 0x40,
                             0x01, 0x91, 0x47, 0xe0, 0x80, 0x19, 0x21, 0xf8, 0x20, 0x06, 0x48, 0x7e, 0x08, 0x01,
                             0x92, 0x00, 0x01, 0xf8, 0x3e, 0x15, 0x48, 0x04, 0x00, 0x00, 0x80, 0x20, 0x00, 0x04,
                             0x32, 0x1f, 0x82, 0x48, 0x9c, 0x0f, 0xc1, 0xa0, 0x50, 0x23, 0xf0, 0x14, 0x10, 0x04,
                             0x08, 0x00, 0x01, 0x08, 0x3e, 0x87, 0xa0, 0x18, 0x00};

        const io::StreamInputView stream_view{doc_raw, sizeof(doc_raw)};

        message_20::Variant variant(io::v2gtp::PayloadType::Part20DerIec, stream_view);

        THEN("It should be deserialized successfully") {
            REQUIRE(variant.get_type() == message_20::Type::DER_AC_ChargeParameterDiscoveryRes);

            const auto& msg = variant.get<message_20::DER_AC_ChargeParameterDiscoveryResponse>();
            const auto& header = msg.header;

            REQUIRE(header.session_id == std::array<uint8_t, 8>{0x3D, 0x4C, 0xBF, 0x93, 0x37, 0x4E, 0xD8, 0x9B});
            REQUIRE(header.timestamp == 1725456324);
            REQUIRE(msg.response_code == message_20::datatypes::ResponseCode::OK);

            const auto& transfer_mode = msg.transfer_mode;
            REQUIRE(message_20::datatypes::from_RationalNumber(transfer_mode.max_charge_power) == 32);
            REQUIRE(message_20::datatypes::from_RationalNumber(transfer_mode.min_charge_power) == 20);
            REQUIRE(dt::from_RationalNumber(transfer_mode.nominal_frequency) == 50);
            REQUIRE(dt::from_RationalNumber(transfer_mode.nominal_charge_power) == 32);
            REQUIRE(dt::from_RationalNumber(transfer_mode.nominal_discharge_power) == 32);
            REQUIRE(dt::from_RationalNumber(transfer_mode.max_discharge_power) == 32);
            REQUIRE(transfer_mode.operating_mode == dt::OperatingMode::GridFollowing);
            REQUIRE(transfer_mode.grid_connection_mode == dt::GridConnectionMode::GridConnected);

            const auto& der_control = transfer_mode.der_control;
            REQUIRE(der_control.over_voltage_fault_ride_through.has_value());
            REQUIRE_FALSE(der_control.under_voltage_fault_ride_through.has_value());
            REQUIRE_FALSE(der_control.zero_current.has_value());
            REQUIRE_FALSE(der_control.reactive_power_support.has_value());
            REQUIRE(der_control.active_power_support.has_value());
            REQUIRE(der_control.max_level_dc_injection.has_value());

            const auto& ovfrt = der_control.over_voltage_fault_ride_through.value();
            REQUIRE(dt::from_RationalNumber(ovfrt.voltage_limit_start_frt) == 110);
            REQUIRE_FALSE(ovfrt.voltage_limit_stop_frt.has_value());
            REQUIRE_FALSE(ovfrt.voltage_recovery_limit.has_value());
            REQUIRE_FALSE(ovfrt.voltage_ride_through_positive_curve_k_factor.has_value());
            REQUIRE_FALSE(ovfrt.voltage_ride_through_negative_curve_k_factor.has_value());
            REQUIRE_FALSE(ovfrt.pt1_response_active_power);
            REQUIRE(dt::from_RationalNumber(ovfrt.step_response_time_constant_active_power) == 1);
            REQUIRE_FALSE(ovfrt.pt1_response_reactive_power);
            REQUIRE(dt::from_RationalNumber(ovfrt.step_response_time_constant_reactive_power) == 1);

            const auto& aps = der_control.active_power_support.value();
            REQUIRE(aps.over_frequency_watt.has_value());
            REQUIRE_FALSE(aps.under_frequency_watt.has_value());
            REQUIRE_FALSE(aps.volt_watt.has_value());

            const auto& aps_ofw = aps.over_frequency_watt.value();
            REQUIRE_THAT(dt::from_RationalNumber(aps_ofw.f_start), Catch::Matchers::WithinRel(50.1, 0.01));
            REQUIRE(dt::from_RationalNumber(aps_ofw.f_stop) == 52);
            REQUIRE_FALSE(aps_ofw.intentional_delay_f_stop.has_value());
            REQUIRE_THAT(dt::from_RationalNumber(aps_ofw.slope), Catch::Matchers::WithinRel(0.4, 0.01));
            REQUIRE_FALSE(aps_ofw.deactivation_time.has_value());
            REQUIRE_FALSE(aps_ofw.intentional_delay_power_control.has_value());
            REQUIRE(aps_ofw.power_reference == dt::PowerReference::MaximumDischargePower);
            REQUIRE_FALSE(aps_ofw.hysteresis_control);
            REQUIRE_FALSE(aps_ofw.power_up_ramp.has_value());
            REQUIRE_FALSE(aps_ofw.pt1_response_active_power);
            REQUIRE(dt::from_RationalNumber(aps_ofw.step_response_time_constant_active_power) == 1);

            const auto& mldci = der_control.max_level_dc_injection.value();
            REQUIRE(dt::from_RationalNumber(mldci) == 0.5);
        }
    }

    GIVEN("Deserialize ac_der_iec_charge_parameter_discovery_res + der control 3") {

        uint8_t doc_raw[] = {0x80, 0x14, 0x04, 0x1e, 0xa6, 0x5f, 0xc9, 0x9b, 0xa7, 0x6c, 0x4d, 0x8c, 0x4b, 0xfe,
                             0x1b, 0x60, 0x62, 0x00, 0x83, 0xf0, 0x40, 0x0c, 0x90, 0xfc, 0x1a, 0x01, 0xe4, 0x40,
                             0x01, 0x91, 0x47, 0xe0, 0x80, 0x19, 0x21, 0xf8, 0x20, 0x06, 0x48, 0x7e, 0x08, 0x01,
                             0x92, 0x00, 0x21, 0xf8, 0x31, 0x24, 0x00, 0x48, 0x04, 0x00, 0x00, 0x80, 0x20, 0x00,
                             0x04, 0x08, 0x04, 0x00, 0x00, 0x80, 0x20, 0x00, 0x04, 0x30};

        const io::StreamInputView stream_view{doc_raw, sizeof(doc_raw)};

        message_20::Variant variant(io::v2gtp::PayloadType::Part20DerIec, stream_view);

        THEN("It should be deserialized successfully") {
            REQUIRE(variant.get_type() == message_20::Type::DER_AC_ChargeParameterDiscoveryRes);

            const auto& msg = variant.get<message_20::DER_AC_ChargeParameterDiscoveryResponse>();
            const auto& header = msg.header;

            REQUIRE(header.session_id == std::array<uint8_t, 8>{0x3D, 0x4C, 0xBF, 0x93, 0x37, 0x4E, 0xD8, 0x9B});
            REQUIRE(header.timestamp == 1725456324);
            REQUIRE(msg.response_code == message_20::datatypes::ResponseCode::OK);

            const auto& transfer_mode = msg.transfer_mode;
            REQUIRE(message_20::datatypes::from_RationalNumber(transfer_mode.max_charge_power) == 32);
            REQUIRE(message_20::datatypes::from_RationalNumber(transfer_mode.min_charge_power) == 20);
            REQUIRE(dt::from_RationalNumber(transfer_mode.nominal_frequency) == 50);
            REQUIRE(dt::from_RationalNumber(transfer_mode.nominal_charge_power) == 32);
            REQUIRE(dt::from_RationalNumber(transfer_mode.nominal_discharge_power) == 32);
            REQUIRE(dt::from_RationalNumber(transfer_mode.max_discharge_power) == 32);
            REQUIRE(transfer_mode.operating_mode == dt::OperatingMode::GridFollowing);
            REQUIRE(transfer_mode.grid_connection_mode == dt::GridConnectionMode::GridConnected);

            const auto& der_control = transfer_mode.der_control;
            REQUIRE_FALSE(der_control.over_voltage_fault_ride_through.has_value());
            REQUIRE(der_control.under_voltage_fault_ride_through.has_value());
            REQUIRE(der_control.zero_current.has_value());
            REQUIRE_FALSE(der_control.reactive_power_support.has_value());
            REQUIRE_FALSE(der_control.active_power_support.has_value());
            REQUIRE_FALSE(der_control.max_level_dc_injection.has_value());

            const auto& uvfrt = der_control.under_voltage_fault_ride_through.value();
            REQUIRE(dt::from_RationalNumber(uvfrt.voltage_limit_start_frt) == 185);
            REQUIRE_FALSE(uvfrt.voltage_limit_stop_frt.has_value());
            REQUIRE_FALSE(uvfrt.voltage_recovery_limit.has_value());
            REQUIRE_FALSE(uvfrt.voltage_ride_through_positive_curve_k_factor.has_value());
            REQUIRE_FALSE(uvfrt.voltage_ride_through_negative_curve_k_factor.has_value());
            REQUIRE_FALSE(uvfrt.pt1_response_active_power);
            REQUIRE(dt::from_RationalNumber(uvfrt.step_response_time_constant_active_power) == 1);
            REQUIRE_FALSE(uvfrt.pt1_response_reactive_power);
            REQUIRE(dt::from_RationalNumber(uvfrt.step_response_time_constant_reactive_power) == 1);

            const auto& zero = der_control.zero_current.value();
            REQUIRE_FALSE(zero.over_voltage_limit.has_value());
            REQUIRE_FALSE(zero.under_voltage_limit.has_value());
            REQUIRE_FALSE(zero.over_voltage_recovery_limit.has_value());
            REQUIRE_FALSE(zero.under_voltage_recovery_limit.has_value());
            REQUIRE_FALSE(zero.pt1_response_active_power);
            REQUIRE(dt::from_RationalNumber(zero.step_response_time_constant_active_power) == 1.0);
            REQUIRE_FALSE(zero.pt1_response_reactive_power);
            REQUIRE(dt::from_RationalNumber(zero.step_response_time_constant_reactive_power) == 1.0);
        }
    }

    GIVEN("Serialize ac_charge_parameter_discovery_res") {

        message_20::DER_AC_ChargeParameterDiscoveryResponse res;

        res.header = message_20::Header{{0x3D, 0x4C, 0xBF, 0x93, 0x37, 0x4E, 0xD8, 0x9B}, 1725456324};
        res.response_code = dt::ResponseCode::OK;
        auto& mode = res.transfer_mode;
        mode.max_charge_power = {3200, -2};
        mode.min_charge_power = {2000, -2};
        mode.nominal_frequency = {50, 0};
        mode.nominal_charge_power = {3200, -2};
        mode.nominal_discharge_power = {3200, -2};
        mode.max_discharge_power = {3200, -2};
        mode.operating_mode = dt::OperatingMode::GridFollowing;
        mode.grid_connection_mode = dt::GridConnectionMode::GridConnected;
        mode.der_control = {};

        std::vector<uint8_t> expected = {0x80, 0x14, 0x04, 0x1e, 0xa6, 0x5f, 0xc9, 0x9b, 0xa7, 0x6c, 0x4d, 0x8c,
                                         0x4b, 0xfe, 0x1b, 0x60, 0x62, 0x00, 0x83, 0xf0, 0x40, 0x0c, 0x90, 0xfc,
                                         0x1a, 0x01, 0xe4, 0x40, 0x01, 0x91, 0x47, 0xe0, 0x80, 0x19, 0x21, 0xf8,
                                         0x20, 0x06, 0x48, 0x7e, 0x08, 0x01, 0x92, 0x00, 0xc0};

        THEN("It should be serialized successfully") {
            REQUIRE(serialize_helper(res) == expected);
        }
    }

    GIVEN("Serialize ac_charge_parameter_discovery_res 2") {

        message_20::DER_AC_ChargeParameterDiscoveryResponse res;

        res.header = message_20::Header{{0x3D, 0x4C, 0xBF, 0x93, 0x37, 0x4E, 0xD8, 0x9B}, 1725456324};
        res.response_code = dt::ResponseCode::OK;
        auto& mode = res.transfer_mode;
        mode.max_charge_power = {3200, -2};
        mode.min_charge_power = {2000, -2};
        mode.nominal_frequency = {50, 0};
        mode.nominal_charge_power = {3200, -2};
        mode.nominal_discharge_power = {3200, -2};
        mode.max_discharge_power = {3200, -2};
        mode.operating_mode = dt::OperatingMode::GridFollowing;
        mode.grid_connection_mode = dt::GridConnectionMode::GridConnected;
        mode.der_control = {};

        auto& rps = mode.der_control.reactive_power_support.emplace();
        rps.name = dt::ReactivePowerSupport::ReactivePowerSupportName::VoltVar;
        rps.curve.x_unit = dt::CurveDataPointsUnit::V;
        rps.curve.y_unit = dt::CurveDataPointsUnit::var;
        rps.curve.pt1_response_reactive_power = false;
        rps.curve.step_response_time_constant_reactive_power = {2, 0};
        rps.curve.curve_data_points.clear();
        rps.curve.curve_data_points.push_back(dt::DataTuple{{207, 0}, {{32, 2}, std::nullopt}});
        rps.curve.curve_data_points.push_back(dt::DataTuple{{253, 0}, {{-32, 2}, std::nullopt}});

        std::vector<uint8_t> expected = {
            0x80, 0x14, 0x04, 0x1e, 0xa6, 0x5f, 0xc9, 0x9b, 0xa7, 0x6c, 0x4d, 0x8c, 0x4b, 0xfe, 0x1b, 0x60, 0x62,
            0x00, 0x83, 0xf0, 0x40, 0x0c, 0x90, 0xfc, 0x1a, 0x01, 0xe4, 0x40, 0x01, 0x91, 0x47, 0xe0, 0x80, 0x19,
            0x21, 0xf8, 0x20, 0x06, 0x48, 0x7e, 0x08, 0x01, 0x92, 0x00, 0x60, 0x04, 0x02, 0x00, 0x33, 0xc0, 0x40,
            0x82, 0x02, 0x01, 0x04, 0x00, 0x7e, 0x80, 0x81, 0x04, 0x23, 0xe2, 0x60, 0x10, 0x00, 0x04, 0x28};

        THEN("It should be serialized successfully") {
            REQUIRE(serialize_helper(res) == expected);
        }
    }
}
