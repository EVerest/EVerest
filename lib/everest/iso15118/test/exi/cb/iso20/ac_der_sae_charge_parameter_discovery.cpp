#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <iso15118/message/ac_der_sae_charge_parameter_discovery.hpp>
#include <iso15118/message/variant.hpp>

#include "helper.hpp"
#include "sae_trip_curves.hpp"

using namespace iso15118;

namespace dt = message_20::datatypes;
namespace dtsae = message_20::datatypes::sae;

namespace {
const std::vector<uint8_t> cpd_res_bytes = {
    0x80, 0x14, 0x04, 0x1e, 0xa6, 0x5f, 0xc9, 0x9b, 0xa7, 0x6c, 0x4d, 0x8c, 0x4b, 0xfe, 0x1b, 0x60, 0x62, 0x00, 0x84,
    0x18, 0x0b, 0x11, 0x00, 0x0c, 0x84, 0x40, 0x01, 0xe1, 0x41, 0x09, 0x18, 0x00, 0x1f, 0xc0, 0x08, 0x10, 0x01, 0x40,
    0x04, 0x02, 0x00, 0x00, 0x41, 0x00, 0x11, 0x00, 0x41, 0x84, 0x8c, 0x00, 0x0f, 0xe0, 0x04, 0x08, 0x00, 0xa0, 0x01,
    0x01, 0x00, 0x00, 0x40, 0x80, 0x0c, 0x40, 0x10, 0xd0, 0x24, 0x60, 0x40, 0x7f, 0x00, 0x50, 0x40, 0x01, 0xf8, 0x08,
    0x00, 0x01, 0x04, 0x00, 0x1f, 0x06, 0x12, 0x30, 0x20, 0x3f, 0x80, 0x28, 0x20, 0x00, 0xe4, 0x04, 0x00, 0x00, 0x82,
    0x00, 0x0e, 0x83, 0x42, 0x10, 0x01, 0xfa, 0x02, 0x08, 0x00, 0xcf, 0x01, 0x04, 0x00, 0x1e, 0x82, 0x00, 0x0e, 0xc6,
    0x12, 0x3f, 0x02, 0xf9, 0x08, 0x48, 0x02, 0x01, 0x00, 0x19, 0xe0, 0x20, 0x83, 0x00, 0x30, 0x10, 0x01, 0xfa, 0x02,
    0x08, 0x31, 0x02, 0x0c, 0x40, 0x00, 0x28, 0x90, 0x01, 0xcc, 0x02, 0x01, 0x58, 0x04, 0x09, 0x10, 0x40, 0x20, 0xc0,
    0x14, 0x10, 0x60, 0x02, 0x02, 0x0c, 0x02, 0xc1, 0x06, 0x00, 0x41, 0x82, 0x48, 0x30, 0x02, 0x22, 0x00, 0x98, 0x90,
    0x02, 0x02, 0x00, 0x3f, 0x40, 0x41, 0x06, 0x01, 0x60, 0x20, 0x02, 0x20, 0x08, 0x10, 0x60, 0x0a, 0x18, 0x80, 0x00,
    0x51, 0x12, 0x41, 0x80, 0x29, 0x08, 0x12, 0x50, 0x33, 0x10, 0x60, 0x1e, 0x42, 0x0c, 0x01, 0x48, 0x83, 0x00, 0x52,
    0x20, 0xc0, 0x14, 0x88, 0x30, 0x05, 0x20, 0x80, 0x03, 0xc0, 0x40, 0x07, 0x30, 0x08, 0x20, 0x00, 0x00, 0x88, 0x00,
    0x88, 0x02, 0x04, 0x00, 0x62, 0x00, 0x80, 0x03, 0x12, 0xff, 0x86, 0xd8, 0x18, 0x00};
} // namespace

SCENARIO("Se/Deserialize ac der sae charge parameter discovery messages") {

    GIVEN("Deserialize ac_der_sae_charge_parameter_discovery_req") {

        uint8_t doc_raw[] = {
            0x80, 0x10, 0x04, 0x1e, 0xa6, 0x5f, 0xc9, 0x9b, 0xa7, 0x6c, 0x4d, 0x8c, 0x3b, 0xfe, 0x1b, 0x60, 0x63, 0x08,
            0x30, 0x0b, 0x22, 0x00, 0x19, 0x08, 0x04, 0x18, 0x05, 0x98, 0x41, 0x80, 0x61, 0x10, 0x60, 0x18, 0x44, 0x18,
            0x06, 0x11, 0x06, 0x01, 0x84, 0x10, 0x60, 0x0a, 0x52, 0x0c, 0x01, 0x4a, 0x41, 0x80, 0x29, 0x48, 0x30, 0x05,
            0x28, 0xfa, 0x00, 0x24, 0x0f, 0xc0, 0xb4, 0x44, 0x18, 0x04, 0x10, 0xfc, 0x0b, 0x44, 0x41, 0x80, 0x41, 0x00,
            0x75, 0x35, 0x73, 0x12, 0xe3, 0x02, 0x08, 0x50, 0x69, 0x6f, 0x6e, 0x69, 0x78, 0x01, 0x49, 0x29, 0xca, 0xc5,
            0xa6, 0x26, 0x06, 0x06, 0x00, 0x25, 0x4d, 0x38, 0xb4, 0xc0, 0xc0, 0xc0, 0xc4, 0x22, 0x08, 0x00, 0xe6, 0x01,
            0x04, 0x00, 0x42, 0x01, 0x02, 0x00, 0x32, 0x00, 0x41, 0x00, 0x00, 0x01, 0x10, 0x17, 0xbc, 0x36, 0xc0, 0xc3,
            0xe3, 0x24, 0x10, 0x61, 0xdf, 0xf0, 0xdb, 0x03, 0x00, 0xf0, 0x06, 0x00};

        const io::StreamInputView stream_view{doc_raw, sizeof(doc_raw)};

        message_20::Variant variant(io::v2gtp::PayloadType::Part20DerSae, stream_view);

        THEN("It should be deserialized successfully") {
            REQUIRE(variant.get_type() == message_20::Type::DER_SAE_AC_ChargeParameterDiscoveryReq);

            const auto& msg = variant.get<message_20::DER_SAE_AC_ChargeParameterDiscoveryRequest>();
            const auto& header = msg.header;

            REQUIRE(header.session_id == std::array<uint8_t, 8>{0x3D, 0x4C, 0xBF, 0x93, 0x37, 0x4E, 0xD8, 0x9B});
            REQUIRE(header.timestamp == 1725456323);

            const auto& transfer_mode = msg.transfer_mode;

            REQUIRE(dt::from_RationalNumber(transfer_mode.max_charge_power) == 11000);
            REQUIRE(dt::from_RationalNumber(transfer_mode.min_charge_power) == 100);
            REQUIRE(transfer_mode.processing == dt::Processing::Finished);
            REQUIRE(dt::from_RationalNumber(transfer_mode.maximum_discharge_power) == 11000);

            const auto& apparent = transfer_mode.apparent_power_limits;
            REQUIRE(dt::from_RationalNumber(apparent.maximum_apparent_power_during_charging_and_var_absorption) ==
                    12000);
            REQUIRE(dt::from_RationalNumber(apparent.maximum_apparent_power_during_charging_and_var_injection) ==
                    12000);
            REQUIRE(dt::from_RationalNumber(apparent.maximum_apparent_power_during_discharging_and_var_absorption) ==
                    12000);
            REQUIRE(dt::from_RationalNumber(apparent.maximum_apparent_power_during_discharging_and_var_injection) ==
                    12000);

            const auto& reactive = transfer_mode.reactive_power_limits;
            REQUIRE(dt::from_RationalNumber(reactive.maximum_var_absorption_during_charging) == 5000);
            REQUIRE(dt::from_RationalNumber(reactive.maximum_var_injection_during_charging) == 5000);
            REQUIRE(dt::from_RationalNumber(reactive.maximum_var_absorption_during_discharging) == 5000);
            REQUIRE(dt::from_RationalNumber(reactive.maximum_var_injection_during_discharging) == 5000);
            REQUIRE_THAT(dt::from_RationalNumber(reactive.reactive_susceptance),
                         Catch::Matchers::WithinRel(0.001, 0.001));

            const auto& excitation = transfer_mode.excitation_limits;
            REQUIRE_THAT(dt::from_RationalNumber(excitation.specified_over_excited_power_factor),
                         Catch::Matchers::WithinRel(0.9, 0.001));
            REQUIRE(dt::from_RationalNumber(excitation.specified_over_excited_discharge_power) == 8000);
            REQUIRE_THAT(dt::from_RationalNumber(excitation.specified_under_excited_power_factor),
                         Catch::Matchers::WithinRel(0.9, 0.001));
            REQUIRE(dt::from_RationalNumber(excitation.specified_under_excited_discharge_power) == 8000);

            const auto& inverter = transfer_mode.inverter_details;
            REQUIRE(inverter.inverter_sw_version == "SW1.0");
            REQUIRE(inverter.inverter_manufacturer == "Pionix");
            REQUIRE(inverter.inverter_model == "INV-1000");
            REQUIRE(inverter.inverter_serial_number == "SN-0001");

            REQUIRE(transfer_mode.ieee1547_normal_category == dtsae::IEEE1547NormalCategory::CategoryB);
            REQUIRE(transfer_mode.ieee1547_abnormal_category == dtsae::IEEE1547AbnormalCategory::CategoryIII);

            REQUIRE(dt::from_RationalNumber(transfer_mode.nominal_voltage) == 230);
            REQUIRE(dt::from_RationalNumber(transfer_mode.maximum_voltage) == 260);
            REQUIRE(dt::from_RationalNumber(transfer_mode.minimum_voltage) == 200);
            REQUIRE(dt::from_RationalNumber(transfer_mode.nominal_voltage_offset) == 0);

            REQUIRE(transfer_mode.j3072_certified == true);
            REQUIRE(transfer_mode.j3072_certification_date == 1725456000);
            REQUIRE(transfer_mode.useable_watt_hours == 75000);
            REQUIRE(transfer_mode.update_time == 1725456323);
            REQUIRE(transfer_mode.supported_modes == 15);
            REQUIRE(transfer_mode.enabled_modes == 3);

            // absent optionals
            REQUIRE_FALSE(transfer_mode.max_charge_power_L2.has_value());
            REQUIRE_FALSE(transfer_mode.max_charge_power_L3.has_value());
            REQUIRE_FALSE(transfer_mode.maximum_discharge_power_L2.has_value());
            REQUIRE_FALSE(transfer_mode.minimum_discharge_power.has_value());
            REQUIRE_FALSE(transfer_mode.session_total_discharge_energy_available.has_value());
            REQUIRE_FALSE(inverter.inverter_hw_version.has_value());
        }
    }

    GIVEN("Serialize ac_der_sae_charge_parameter_discovery_req") {

        message_20::DER_SAE_AC_ChargeParameterDiscoveryRequest req;

        req.header = message_20::Header{{0x3D, 0x4C, 0xBF, 0x93, 0x37, 0x4E, 0xD8, 0x9B}, 1725456323};
        auto& mode = req.transfer_mode;
        mode.max_charge_power = {11, 3};
        mode.min_charge_power = {100, 0};
        mode.processing = dt::Processing::Finished;
        mode.maximum_discharge_power = {11, 3};

        auto& apparent = mode.apparent_power_limits;
        apparent.maximum_apparent_power_during_charging_and_var_absorption = {12, 3};
        apparent.maximum_apparent_power_during_charging_and_var_injection = {12, 3};
        apparent.maximum_apparent_power_during_discharging_and_var_absorption = {12, 3};
        apparent.maximum_apparent_power_during_discharging_and_var_injection = {12, 3};

        auto& reactive = mode.reactive_power_limits;
        reactive.maximum_var_absorption_during_charging = {5, 3};
        reactive.maximum_var_injection_during_charging = {5, 3};
        reactive.maximum_var_absorption_during_discharging = {5, 3};
        reactive.maximum_var_injection_during_discharging = {5, 3};
        reactive.reactive_susceptance = {1, -3};

        auto& excitation = mode.excitation_limits;
        excitation.specified_over_excited_power_factor = {90, -2};
        excitation.specified_over_excited_discharge_power = {8, 3};
        excitation.specified_under_excited_power_factor = {90, -2};
        excitation.specified_under_excited_discharge_power = {8, 3};

        auto& inverter = mode.inverter_details;
        inverter.inverter_sw_version = "SW1.0";
        inverter.inverter_manufacturer = "Pionix";
        inverter.inverter_model = "INV-1000";
        inverter.inverter_serial_number = "SN-0001";

        mode.ieee1547_normal_category = dtsae::IEEE1547NormalCategory::CategoryB;
        mode.ieee1547_abnormal_category = dtsae::IEEE1547AbnormalCategory::CategoryIII;
        mode.nominal_voltage = {230, 0};
        mode.maximum_voltage = {260, 0};
        mode.minimum_voltage = {200, 0};
        mode.nominal_voltage_offset = {0, 0};
        mode.j3072_certified = true;
        mode.j3072_certification_date = 1725456000;
        mode.useable_watt_hours = 75000;
        mode.update_time = 1725456323;
        mode.supported_modes = 15;
        mode.enabled_modes = 3;

        std::vector<uint8_t> expected = {
            0x80, 0x10, 0x04, 0x1e, 0xa6, 0x5f, 0xc9, 0x9b, 0xa7, 0x6c, 0x4d, 0x8c, 0x3b, 0xfe, 0x1b, 0x60, 0x63, 0x08,
            0x30, 0x0b, 0x22, 0x00, 0x19, 0x08, 0x04, 0x18, 0x05, 0x98, 0x41, 0x80, 0x61, 0x10, 0x60, 0x18, 0x44, 0x18,
            0x06, 0x11, 0x06, 0x01, 0x84, 0x10, 0x60, 0x0a, 0x52, 0x0c, 0x01, 0x4a, 0x41, 0x80, 0x29, 0x48, 0x30, 0x05,
            0x28, 0xfa, 0x00, 0x24, 0x0f, 0xc0, 0xb4, 0x44, 0x18, 0x04, 0x10, 0xfc, 0x0b, 0x44, 0x41, 0x80, 0x41, 0x00,
            0x75, 0x35, 0x73, 0x12, 0xe3, 0x02, 0x08, 0x50, 0x69, 0x6f, 0x6e, 0x69, 0x78, 0x01, 0x49, 0x29, 0xca, 0xc5,
            0xa6, 0x26, 0x06, 0x06, 0x00, 0x25, 0x4d, 0x38, 0xb4, 0xc0, 0xc0, 0xc0, 0xc4, 0x22, 0x08, 0x00, 0xe6, 0x01,
            0x04, 0x00, 0x42, 0x01, 0x02, 0x00, 0x32, 0x00, 0x41, 0x00, 0x00, 0x01, 0x10, 0x17, 0xbc, 0x36, 0xc0, 0xc3,
            0xe3, 0x24, 0x10, 0x61, 0xdf, 0xf0, 0xdb, 0x03, 0x00, 0xf0, 0x06, 0x00};

        THEN("It should be serialized successfully") {
            REQUIRE(serialize_helper(req) == expected);
        }
    }

    GIVEN("Deserialize ac_der_sae_charge_parameter_discovery_res") {

        const io::StreamInputView stream_view{cpd_res_bytes.data(), cpd_res_bytes.size()};

        message_20::Variant variant(io::v2gtp::PayloadType::Part20DerSae, stream_view);

        THEN("It should be deserialized successfully") {
            REQUIRE(variant.get_type() == message_20::Type::DER_SAE_AC_ChargeParameterDiscoveryRes);

            const auto& msg = variant.get<message_20::DER_SAE_AC_ChargeParameterDiscoveryResponse>();
            const auto& header = msg.header;

            REQUIRE(header.session_id == std::array<uint8_t, 8>{0x3D, 0x4C, 0xBF, 0x93, 0x37, 0x4E, 0xD8, 0x9B});
            REQUIRE(header.timestamp == 1725456324);
            REQUIRE(msg.response_code == dt::ResponseCode::OK);

            const auto& transfer_mode = msg.transfer_mode;

            REQUIRE(dt::from_RationalNumber(transfer_mode.max_charge_power) == 22000);
            REQUIRE(dt::from_RationalNumber(transfer_mode.min_charge_power) == 100);
            REQUIRE(dt::from_RationalNumber(transfer_mode.nominal_frequency) == 60);
            REQUIRE(transfer_mode.processing == dt::Processing::Finished);
            REQUIRE(dt::from_RationalNumber(transfer_mode.maximum_discharge_power) == 15000);
            REQUIRE(transfer_mode.required_der_operating_mode == dtsae::RequiredDEROperatingMode::GridFollowing);
            REQUIRE(transfer_mode.grid_connection_mode == dtsae::GridConnectionMode::GridConnected);
            REQUIRE(transfer_mode.update_time == 1725456324);

            const auto& der_control = transfer_mode.der_control_cpd_res;
            REQUIRE(der_control.enter_service_cpd_res.permit_service == true);
            REQUIRE(dt::from_RationalNumber(der_control.enter_service_cpd_res.enter_service_voltage_high) == 253);
            REQUIRE(dt::from_RationalNumber(der_control.enter_service_cpd_res.enter_service_voltage_low) == 207);
            REQUIRE(dt::from_RationalNumber(der_control.enter_service_cpd_res.enter_service_frequency_high) == 61);
            REQUIRE(dt::from_RationalNumber(der_control.enter_service_cpd_res.enter_service_frequency_low) == 59);

            // mandatory VoltageTrip / FrequencyTrip must-trip curves decoded
            REQUIRE(der_control.voltage_trip.over_voltage_must_trip_curve.enable == true);
            require_trip_curve(der_control.voltage_trip.over_voltage_must_trip_curve, dtsae::DERUnit::V);
            REQUIRE(der_control.voltage_trip.over_voltage_must_trip_curve.curve_data_points.size() == 2);
            require_trip_curve(der_control.voltage_trip.under_voltage_must_trip_curve, dtsae::DERUnit::V);
            REQUIRE(der_control.frequency_trip.over_frequency_must_trip_curve.enable == true);
            require_trip_curve(der_control.frequency_trip.over_frequency_must_trip_curve, dtsae::DERUnit::Hz);
            require_trip_curve(der_control.frequency_trip.under_frequency_must_trip_curve, dtsae::DERUnit::Hz);

            // mandatory ReactivePowerSupport / ActivePowerSupport sub-members decoded
            REQUIRE(der_control.reactive_power_support_cpd_res.constant_power_factor.enable == true);
            REQUIRE(der_control.reactive_power_support_cpd_res.constant_power_factor.power_factor_excitation ==
                    dtsae::PowerFactorExcitation::OverExcited);
            REQUIRE(der_control.active_power_support_cpd_res.frequency_droop.enable == true);
            REQUIRE(der_control.active_power_support_cpd_res.limit_max_discharge_power.percentage_value == 80);

            const auto& reactive = transfer_mode.reactive_power_limits;
            REQUIRE(dt::from_RationalNumber(reactive.maximum_var_absorption_during_charging) == 5000);

            const auto& grid = transfer_mode.grid_limits;
            REQUIRE(dt::from_RationalNumber(grid.nominal_frequency) == 60);
            REQUIRE(dt::from_RationalNumber(grid.nominal_voltage) == 230);
            REQUIRE(dt::from_RationalNumber(grid.maximum_voltage) == 264);
            REQUIRE(dt::from_RationalNumber(grid.minimum_voltage) == 196);

            // absent optionals in the min case
            REQUIRE_FALSE(transfer_mode.status.has_value());
            REQUIRE_FALSE(transfer_mode.nominal_charge_power.has_value());
            REQUIRE_FALSE(transfer_mode.nominal_discharge_power.has_value());
            REQUIRE_FALSE(transfer_mode.max_charge_power_L2.has_value());
            REQUIRE_FALSE(grid.min_frequency.has_value());
            REQUIRE_FALSE(grid.max_frequency.has_value());
            REQUIRE_FALSE(der_control.voltage_trip.over_voltage_momentary_cessation_trip_curve.has_value());
        }
    }

    GIVEN("Deserialize ac_der_sae_charge_parameter_discovery_res with optionals") {

        uint8_t doc_raw[] = {
            0x80, 0x14, 0x04, 0x1e, 0xa6, 0x5f, 0xc9, 0x9b, 0xa7, 0x6c, 0x4d, 0x8c, 0x4b, 0xfe, 0x1b, 0x60, 0x62, 0x00,
            0x84, 0x18, 0x0b, 0x01, 0x06, 0x02, 0xc0, 0x41, 0x80, 0xb0, 0x20, 0x01, 0x90, 0x88, 0x00, 0x3c, 0x01, 0x06,
            0x00, 0x40, 0x20, 0x00, 0xc8, 0x04, 0x18, 0x04, 0x81, 0x06, 0x01, 0x20, 0x41, 0x80, 0x48, 0x00, 0x3c, 0x00,
            0x04, 0x00, 0x01, 0x80, 0x01, 0xfc, 0x00, 0x81, 0x00, 0x14, 0x00, 0x40, 0x20, 0x00, 0x04, 0x10, 0x01, 0x10,
            0x04, 0x18, 0x48, 0xc0, 0x00, 0xfe, 0x00, 0x40, 0x80, 0x0a, 0x00, 0x10, 0x10, 0x00, 0x04, 0x08, 0x00, 0xc4,
            0x01, 0x0c, 0x09, 0x18, 0x00, 0x1f, 0xc0, 0x04, 0x10, 0x01, 0x40, 0x04, 0x01, 0xfc, 0x01, 0x41, 0x00, 0x11,
            0xc0, 0x41, 0x89, 0x23, 0x00, 0x04, 0x00, 0x00, 0x82, 0x00, 0x23, 0x80, 0x80, 0x40, 0x00, 0x18, 0x20, 0x02,
            0x08, 0x08, 0x32, 0x12, 0x30, 0x20, 0x3f, 0x80, 0x28, 0x20, 0x00, 0xfc, 0x04, 0x00, 0x00, 0x82, 0x00, 0x0f,
            0x83, 0x09, 0x18, 0x10, 0x1f, 0xc0, 0x14, 0x10, 0x00, 0x72, 0x02, 0x00, 0x00, 0x41, 0x00, 0x07, 0x41, 0x82,
            0x46, 0x04, 0x08, 0x00, 0x02, 0x04, 0x00, 0x1f, 0x00, 0x80, 0x00, 0xa0, 0x3f, 0x86, 0xe8, 0x20, 0x64, 0x42,
            0x00, 0x3f, 0x40, 0x41, 0x00, 0x19, 0xe0, 0x20, 0x80, 0x03, 0xd0, 0x40, 0x01, 0xd8, 0x08, 0x00, 0xac, 0x02,
            0x02, 0x00, 0x0f, 0x00, 0x80, 0x07, 0x80, 0x10, 0x01, 0x07, 0xe0, 0x5f, 0x21, 0x08, 0x01, 0x00, 0x08, 0x04,
            0x00, 0x67, 0x80, 0x82, 0x0c, 0x00, 0xc0, 0x40, 0x07, 0xe8, 0x08, 0x20, 0xc4, 0x08, 0x31, 0x00, 0x00, 0xa0,
            0x0a, 0x08, 0x00, 0xe6, 0x01, 0x00, 0xac, 0x02, 0x04, 0x00, 0xc1, 0x04, 0x02, 0x0c, 0x01, 0x41, 0x06, 0x00,
            0x20, 0x20, 0xc0, 0x2c, 0x10, 0x60, 0x04, 0x14, 0x40, 0x00, 0x28, 0x02, 0x81, 0x00, 0x40, 0x83, 0x00, 0x22,
            0x20, 0x08, 0x00, 0x40, 0x7d, 0x02, 0x40, 0x3f, 0x00, 0x29, 0x08, 0x80, 0x00, 0x50, 0x07, 0xd0, 0x24, 0x03,
            0xf0, 0x02, 0x90, 0x88, 0x00, 0x05, 0x01, 0x00, 0x20, 0x00, 0x80, 0x80, 0x0f, 0xd0, 0x10, 0x41, 0x80, 0x58,
            0x08, 0x00, 0x88, 0x02, 0x04, 0x18, 0x02, 0x86, 0x20, 0x00, 0x14, 0x01, 0x40, 0x80, 0x18, 0x41, 0x80, 0x29,
            0x08, 0x10, 0x04, 0x0a, 0x04, 0x40, 0x00, 0x28, 0x01, 0x06, 0x01, 0x62, 0x20, 0xc0, 0x2c, 0x88, 0x30, 0x0f,
            0x02, 0x0c, 0x03, 0xc0, 0x83, 0x00, 0xf0, 0x20, 0xc0, 0x14, 0x88, 0x30, 0x05, 0x22, 0x0c, 0x01, 0x48, 0x83,
            0x00, 0x52, 0x08, 0x00, 0x3c, 0x04, 0x00, 0x73, 0x00, 0x82, 0x00, 0x00, 0x00, 0x80, 0x03, 0x90, 0x20, 0x00,
            0xfc, 0x10, 0x01, 0x10, 0x04, 0x08, 0x00, 0xc4, 0x01, 0x00, 0x06, 0x25, 0xff, 0x0d, 0xb0, 0x30};

        const io::StreamInputView stream_view{doc_raw, sizeof(doc_raw)};

        message_20::Variant variant(io::v2gtp::PayloadType::Part20DerSae, stream_view);

        THEN("It should be deserialized successfully") {
            REQUIRE(variant.get_type() == message_20::Type::DER_SAE_AC_ChargeParameterDiscoveryRes);

            const auto& msg = variant.get<message_20::DER_SAE_AC_ChargeParameterDiscoveryResponse>();
            const auto& header = msg.header;

            REQUIRE(header.session_id == std::array<uint8_t, 8>{0x3D, 0x4C, 0xBF, 0x93, 0x37, 0x4E, 0xD8, 0x9B});
            REQUIRE(header.timestamp == 1725456324);
            REQUIRE(msg.response_code == dt::ResponseCode::OK);

            const auto& transfer_mode = msg.transfer_mode;

            // optionals that ARE present in this variant
            REQUIRE(transfer_mode.status.has_value());
            REQUIRE(transfer_mode.status.value().notification == dt::EvseNotification::Pause);
            REQUIRE(transfer_mode.status.value().notification_max_delay == 60);
            REQUIRE(transfer_mode.nominal_charge_power.has_value());
            REQUIRE(dt::from_RationalNumber(transfer_mode.nominal_charge_power.value()) == 11000);
            REQUIRE(transfer_mode.nominal_discharge_power.has_value());
            REQUIRE(dt::from_RationalNumber(transfer_mode.nominal_discharge_power.value()) == 11000);

            REQUIRE(transfer_mode.max_charge_power_L2.has_value());
            REQUIRE(transfer_mode.max_charge_power_L3.has_value());
            REQUIRE(transfer_mode.grid_limits.min_frequency.has_value());
            REQUIRE(transfer_mode.grid_limits.max_frequency.has_value());
            const auto& voltage_trip = transfer_mode.der_control_cpd_res.voltage_trip;
            const auto& frequency_trip = transfer_mode.der_control_cpd_res.frequency_trip;
            REQUIRE(voltage_trip.over_voltage_momentary_cessation_trip_curve.has_value());
            REQUIRE(voltage_trip.over_voltage_may_trip_curve.has_value());
            REQUIRE(frequency_trip.over_frequency_may_trip_curve.has_value());

            require_trip_curve(voltage_trip.over_voltage_must_trip_curve, dtsae::DERUnit::V);
            require_trip_curve(voltage_trip.under_voltage_must_trip_curve, dtsae::DERUnit::V);
            require_trip_curve(voltage_trip.over_voltage_may_trip_curve.value(), dtsae::DERUnit::V);
            require_trip_curve(frequency_trip.over_frequency_must_trip_curve, dtsae::DERUnit::Hz);
            require_trip_curve(frequency_trip.under_frequency_must_trip_curve, dtsae::DERUnit::Hz);

            const auto& ov_cessation = voltage_trip.over_voltage_momentary_cessation_trip_curve.value();
            require_trip_curve(ov_cessation, dtsae::DERUnit::V);
            REQUIRE_THAT(dt::from_RationalNumber(ov_cessation.curve_data_points[0].x_value),
                         Catch::Matchers::WithinRel(0.1, 0.001));
            REQUIRE(dt::from_RationalNumber(ov_cessation.curve_data_points[0].y_value) == 288);
            const auto& of_may = frequency_trip.over_frequency_may_trip_curve.value();
            require_trip_curve(of_may, dtsae::DERUnit::Hz);
            REQUIRE(dt::from_RationalNumber(of_may.curve_data_points[1].x_value) == 10);
            REQUIRE_THAT(dt::from_RationalNumber(of_may.curve_data_points[1].y_value),
                         Catch::Matchers::WithinRel(60.5, 0.001));
        }
    }

    GIVEN("Serialize ac_der_sae_charge_parameter_discovery_res") {

        message_20::DER_SAE_AC_ChargeParameterDiscoveryResponse res;

        res.header = message_20::Header{{0x3D, 0x4C, 0xBF, 0x93, 0x37, 0x4E, 0xD8, 0x9B}, 1725456324};
        res.response_code = dt::ResponseCode::OK;
        auto& mode = res.transfer_mode;

        mode.max_charge_power = {22, 3};
        mode.min_charge_power = {100, 0};
        mode.nominal_frequency = {60, 0};
        mode.processing = dt::Processing::Finished;

        auto& der_control = mode.der_control_cpd_res;
        der_control.voltage_trip = make_voltage_trip();
        der_control.frequency_trip = make_frequency_trip();

        // EnterServiceCPDRes
        auto& enter_service = der_control.enter_service_cpd_res;
        enter_service.permit_service = true;
        enter_service.enter_service_voltage_high = {253, 0};
        enter_service.enter_service_voltage_low = {207, 0};
        enter_service.enter_service_frequency_high = {61, 0};
        enter_service.enter_service_frequency_low = {59, 0};

        // ReactivePowerSupportCPDRes
        auto& reactive_support = der_control.reactive_power_support_cpd_res;
        auto& cpf = reactive_support.constant_power_factor;
        cpf.enable = true;
        cpf.power_factor_value = {95, -2};
        cpf.power_factor_excitation = dtsae::PowerFactorExcitation::OverExcited;

        auto& volt_var = reactive_support.volt_var;
        volt_var.enable = true;
        volt_var.x_unit = dtsae::DERUnit::V;
        volt_var.y_unit = dtsae::DERUnit::var;
        volt_var.curve_data_points.push_back(dtsae::DataTuple{{207, 0}, {3, 3}});
        volt_var.curve_data_points.push_back(dtsae::DataTuple{{253, 0}, {-3, 3}});
        volt_var.open_loop_response_time = {5, 0};
        volt_var.reference_voltage = {230, 0};
        volt_var.autonomous_reference_voltage_adjustment_enable = false;
        volt_var.reference_voltage_adjustment_time_constant = 300;

        auto& watt_var = reactive_support.watt_var;
        watt_var.enable = true;
        watt_var.x_unit = dtsae::DERUnit::W;
        watt_var.y_unit = dtsae::DERUnit::var;
        watt_var.curve_data_points.push_back(dtsae::DataTuple{{5, 3}, {1, 3}});
        watt_var.curve_data_points.push_back(dtsae::DataTuple{{11, 3}, {2, 3}});

        auto& constant_var = reactive_support.constant_var;
        constant_var.enable = true;
        constant_var.var_setpoint = {2, 3};
        constant_var.unit = dtsae::DERUnit::var;

        // ActivePowerSupportCPDRes
        auto& active_support = der_control.active_power_support_cpd_res;
        active_support.frequency_droop.enable = true;

        auto& volt_watt = active_support.volt_watt;
        volt_watt.enable = true;
        volt_watt.x_unit = dtsae::DERUnit::V;
        volt_watt.y_unit = dtsae::DERUnit::W;
        volt_watt.curve_data_points.push_back(dtsae::DataTuple{{253, 0}, {11, 3}});
        volt_watt.curve_data_points.push_back(dtsae::DataTuple{{264, 0}, {5, 3}});
        volt_watt.open_loop_response_time = {5, 0};

        auto& constant_watt = active_support.constant_watt;
        constant_watt.enable = true;
        constant_watt.watt_setpoint = {5, 3};
        constant_watt.unit = dtsae::DERUnit::W;

        auto& limit_max = active_support.limit_max_discharge_power;
        limit_max.enable = true;
        limit_max.percentage_value = 80;

        mode.maximum_discharge_power = {15, 3};

        auto& reactive_limits = mode.reactive_power_limits;
        reactive_limits.maximum_var_absorption_during_charging = {5, 3};
        reactive_limits.maximum_var_injection_during_charging = {5, 3};
        reactive_limits.maximum_var_absorption_during_discharging = {5, 3};
        reactive_limits.maximum_var_injection_during_discharging = {5, 3};

        auto& grid = mode.grid_limits;
        grid.nominal_frequency = {60, 0};
        grid.nominal_voltage = {230, 0};
        grid.nominal_voltage_offset = {0, 0};
        grid.maximum_voltage = {264, 0};
        grid.minimum_voltage = {196, 0};

        mode.required_der_operating_mode = dtsae::RequiredDEROperatingMode::GridFollowing;
        mode.grid_connection_mode = dtsae::GridConnectionMode::GridConnected;
        mode.update_time = 1725456324;

        THEN("It should be serialized successfully") {
            REQUIRE(serialize_helper(res) == cpd_res_bytes);
        }
    }
}
