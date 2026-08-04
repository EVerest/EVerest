// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <iso15118/d20/config.hpp>
#include <iso15118/detail/d20/state/ac_der_sae_convert.hpp>

using Catch::Matchers::WithinAbs;
using Catch::Matchers::WithinRel;

namespace dt = iso15118::message_20::datatypes;

namespace {

constexpr auto TOLERANCE = 0.01f;
constexpr float NOMINAL_VOLTAGE_V = 230.0f;
// from_float keeps four significant digits and truncates, so volt magnitude values need a
// relative tolerance rather than an absolute one.
constexpr auto RELATIVE_TOLERANCE = 1e-3f;
// The rational number encoding keeps four significant digits, so single decimal inputs land one digit short.
constexpr auto COARSE_TOLERANCE = 0.05f;

float value_of(const dt::RationalNumber& in) {
    return dt::from_RationalNumber(in);
}

} // namespace

SCENARIO("The default SAE DER control converts onto the wire") {

    GIVEN("The default SAE DER control fed through convert") {
        const auto der_control = iso15118::d20::get_default_sae_der_control(NOMINAL_VOLTAGE_V);

        dt::sae::DERControlCPDRes out{};
        iso15118::d20::state::convert(out, der_control);

        THEN("Each mandatory trip curve carries the schema minimum of two data points") {
            REQUIRE(out.voltage_trip.over_voltage_must_trip_curve.curve_data_points.size() == 2);
            REQUIRE(out.voltage_trip.under_voltage_must_trip_curve.curve_data_points.size() == 2);
            REQUIRE(out.frequency_trip.over_frequency_must_trip_curve.curve_data_points.size() == 2);
            REQUIRE(out.frequency_trip.under_frequency_must_trip_curve.curve_data_points.size() == 2);
        }

        THEN("The over voltage trip curve points survive the rational number conversion") {
            const auto& points = out.voltage_trip.over_voltage_must_trip_curve.curve_data_points;
            REQUIRE_THAT(value_of(points[0].x_value), WithinAbs(110.0f, TOLERANCE));
            REQUIRE_THAT(value_of(points[0].y_value), WithinAbs(2.0f, TOLERANCE));
            REQUIRE_THAT(value_of(points[1].x_value), WithinAbs(120.0f, TOLERANCE));
            REQUIRE_THAT(value_of(points[1].y_value), WithinAbs(0.16f, TOLERANCE));
        }

        THEN("The over frequency trip curve points survive the rational number conversion") {
            const auto& points = out.frequency_trip.over_frequency_must_trip_curve.curve_data_points;
            REQUIRE_THAT(value_of(points[0].x_value), WithinAbs(51.5f, TOLERANCE));
            REQUIRE_THAT(value_of(points[0].y_value), WithinAbs(300.0f, TOLERANCE));
            REQUIRE_THAT(value_of(points[1].x_value), WithinAbs(52.0f, TOLERANCE));
            REQUIRE_THAT(value_of(points[1].y_value), WithinAbs(0.16f, TOLERANCE));
        }

        THEN("The curve units survive the conversion") {
            REQUIRE(out.voltage_trip.over_voltage_must_trip_curve.x_unit == dt::sae::DERUnit::PercentageV);
            REQUIRE(out.voltage_trip.over_voltage_must_trip_curve.y_unit == dt::sae::DERUnit::s);
            REQUIRE(out.voltage_trip.under_voltage_must_trip_curve.x_unit == dt::sae::DERUnit::PercentageV);
            REQUIRE(out.voltage_trip.under_voltage_must_trip_curve.y_unit == dt::sae::DERUnit::s);
            REQUIRE(out.frequency_trip.over_frequency_must_trip_curve.x_unit == dt::sae::DERUnit::Hz);
            REQUIRE(out.frequency_trip.over_frequency_must_trip_curve.y_unit == dt::sae::DERUnit::s);
            REQUIRE(out.frequency_trip.under_frequency_must_trip_curve.x_unit == dt::sae::DERUnit::Hz);

            REQUIRE(out.reactive_power_support_cpd_res.volt_var.x_unit == dt::sae::DERUnit::PercentageV);
            REQUIRE(out.reactive_power_support_cpd_res.volt_var.y_unit ==
                    dt::sae::DERUnit::PercentageEVMaximumConfiguredReactivePower);
            REQUIRE(out.reactive_power_support_cpd_res.watt_var.x_unit ==
                    dt::sae::DERUnit::PercentageEVMaximumConfiguredActivePower);
            REQUIRE(out.reactive_power_support_cpd_res.watt_var.y_unit ==
                    dt::sae::DERUnit::PercentageEVMaximumConfiguredReactivePower);
            REQUIRE(out.reactive_power_support_cpd_res.constant_var.unit ==
                    dt::sae::DERUnit::PercentageEVMaximumConfiguredReactivePower);

            REQUIRE(out.active_power_support_cpd_res.volt_watt.x_unit == dt::sae::DERUnit::PercentageV);
            REQUIRE(out.active_power_support_cpd_res.volt_watt.y_unit ==
                    dt::sae::DERUnit::PercentageEVMaximumConfiguredActivePower);
            REQUIRE(out.active_power_support_cpd_res.constant_watt.unit ==
                    dt::sae::DERUnit::PercentageEVMaximumConfiguredActivePower);
        }

        THEN("The conversion does not invent an enable") {
            REQUIRE(out.voltage_trip.over_voltage_must_trip_curve.enable == false);
            REQUIRE(out.voltage_trip.under_voltage_must_trip_curve.enable == false);
            REQUIRE(out.frequency_trip.over_frequency_must_trip_curve.enable == false);
            REQUIRE(out.frequency_trip.under_frequency_must_trip_curve.enable == false);

            REQUIRE(out.enter_service_cpd_res.permit_service == false);

            REQUIRE(out.reactive_power_support_cpd_res.constant_power_factor.enable == false);
            REQUIRE(out.reactive_power_support_cpd_res.volt_var.enable == false);
            REQUIRE(out.reactive_power_support_cpd_res.watt_var.enable == false);
            REQUIRE(out.reactive_power_support_cpd_res.constant_var.enable == false);

            REQUIRE(out.active_power_support_cpd_res.frequency_droop.enable == false);
            REQUIRE(out.active_power_support_cpd_res.volt_watt.enable == false);
            REQUIRE(out.active_power_support_cpd_res.constant_watt.enable == false);
            REQUIRE(out.active_power_support_cpd_res.limit_max_discharge_power.enable == false);

            REQUIRE(out.reactive_power_support_cpd_res.volt_var.autonomous_reference_voltage_adjustment_enable ==
                    false);
        }

        THEN("The enter service thresholds survive the conversion") {
            REQUIRE_THAT(value_of(out.enter_service_cpd_res.enter_service_voltage_high),
                         WithinRel(241.5f, RELATIVE_TOLERANCE));
            REQUIRE_THAT(value_of(out.enter_service_cpd_res.enter_service_voltage_low),
                         WithinRel(210.91f, RELATIVE_TOLERANCE));
            REQUIRE_THAT(value_of(out.enter_service_cpd_res.enter_service_frequency_high),
                         WithinAbs(50.1f, COARSE_TOLERANCE));
            REQUIRE_THAT(value_of(out.enter_service_cpd_res.enter_service_frequency_low),
                         WithinAbs(49.5f, COARSE_TOLERANCE));

            REQUIRE(out.enter_service_cpd_res.enter_service_delay.has_value() == false);
            REQUIRE(out.enter_service_cpd_res.enter_service_randomized_delay.has_value() == false);
            REQUIRE(out.enter_service_cpd_res.enter_service_ramp_time.has_value() == false);
        }

        THEN("The reactive and active power support setpoints survive the conversion") {
            REQUIRE_THAT(value_of(out.reactive_power_support_cpd_res.constant_power_factor.power_factor_value),
                         WithinAbs(1.0f, TOLERANCE));
            REQUIRE(out.reactive_power_support_cpd_res.constant_power_factor.power_factor_excitation ==
                    dt::sae::PowerFactorExcitation::OverExcited);
            REQUIRE_THAT(value_of(out.reactive_power_support_cpd_res.constant_var.var_setpoint),
                         WithinAbs(0.0f, TOLERANCE));
            REQUIRE_THAT(value_of(out.reactive_power_support_cpd_res.volt_var.reference_voltage),
                         WithinAbs(NOMINAL_VOLTAGE_V, TOLERANCE));
            REQUIRE(out.reactive_power_support_cpd_res.volt_var.reference_voltage_adjustment_time_constant == 0);
            REQUIRE_THAT(value_of(out.reactive_power_support_cpd_res.volt_var.open_loop_response_time),
                         WithinAbs(5.0f, TOLERANCE));

            REQUIRE_THAT(value_of(out.active_power_support_cpd_res.constant_watt.watt_setpoint),
                         WithinAbs(0.0f, TOLERANCE));
            REQUIRE_THAT(value_of(out.active_power_support_cpd_res.volt_watt.open_loop_response_time),
                         WithinAbs(5.0f, TOLERANCE));
            REQUIRE(out.active_power_support_cpd_res.limit_max_discharge_power.percentage_value == 100);
        }

        THEN("The volt var, watt var and volt watt curves carry their data points") {
            REQUIRE(out.reactive_power_support_cpd_res.volt_var.curve_data_points.size() == 2);
            REQUIRE(out.reactive_power_support_cpd_res.watt_var.curve_data_points.size() == 2);
            REQUIRE(out.active_power_support_cpd_res.volt_watt.curve_data_points.size() == 2);

            const auto& volt_watt_points = out.active_power_support_cpd_res.volt_watt.curve_data_points;
            REQUIRE_THAT(value_of(volt_watt_points[0].x_value), WithinAbs(100.0f, TOLERANCE));
            REQUIRE_THAT(value_of(volt_watt_points[0].y_value), WithinAbs(100.0f, TOLERANCE));
            REQUIRE_THAT(value_of(volt_watt_points[1].x_value), WithinAbs(110.0f, TOLERANCE));
            REQUIRE_THAT(value_of(volt_watt_points[1].y_value), WithinAbs(100.0f, TOLERANCE));
        }

        THEN("The optional trip curves stay unset") {
            REQUIRE(out.voltage_trip.over_voltage_momentary_cessation_trip_curve.has_value() == false);
            REQUIRE(out.voltage_trip.under_voltage_momentary_cessation_trip_curve.has_value() == false);
            REQUIRE(out.voltage_trip.over_voltage_may_trip_curve.has_value() == false);
            REQUIRE(out.voltage_trip.under_voltage_may_trip_curve.has_value() == false);
            REQUIRE(out.frequency_trip.over_frequency_may_trip_curve.has_value() == false);
            REQUIRE(out.frequency_trip.under_frequency_may_trip_curve.has_value() == false);
        }

        THEN("The inert over frequency droop branch carries through and the under frequency one stays unset") {
            // Table M.33: an absent OverFrequencyDroop obliges UnderFrequencyDroop, so the default carries
            // a zeroed over frequency branch instead of leaving both unset.
            const auto& frequency_droop = out.active_power_support_cpd_res.frequency_droop;
            REQUIRE(frequency_droop.over_frequency_droop.has_value() == true);
            REQUIRE_THAT(value_of(frequency_droop.over_frequency_droop.value().db), WithinAbs(0.0f, TOLERANCE));
            REQUIRE_THAT(value_of(frequency_droop.over_frequency_droop.value().droop_factor),
                         WithinAbs(0.0f, TOLERANCE));
            REQUIRE(frequency_droop.over_frequency_droop.value().power_reference ==
                    dt::sae::PowerReference::MaximumActivePower);
            REQUIRE_THAT(value_of(frequency_droop.over_frequency_droop.value().open_loop_response_time),
                         WithinAbs(0.0f, TOLERANCE));
            REQUIRE(frequency_droop.under_frequency_droop.has_value() == false);
        }
    }
}

SCENARIO("Optional SAE DER control members carry through the conversion") {

    GIVEN("A DER control with the optional curves and droop settings filled in") {
        namespace sae = iso15118::sae;

        auto der_control = iso15118::d20::get_default_sae_der_control(NOMINAL_VOLTAGE_V);

        auto& may_trip = der_control.voltage_trip.over_voltage_may_trip_curve.emplace();
        may_trip.enable = true;
        may_trip.priority = 3;
        may_trip.x_unit = sae::DERUnit::PercentageV;
        may_trip.y_unit = sae::DERUnit::s;
        may_trip.curve_data_points = {{115.0f, 1.0f}, {125.0f, 0.5f}};
        may_trip.curve_data_points_L2 = sae::CurveDataPointsList{{116.0f, 1.0f}, {126.0f, 0.5f}};
        may_trip.curve_data_points_L3 = std::nullopt;

        auto& droop = der_control.active_power_support.frequency_droop.over_frequency_droop.emplace();
        droop.db = 0.036f;
        droop.droop_factor = 20.0f;
        droop.droop_factor_L2 = 21.0f;
        droop.droop_factor_L3 = std::nullopt;
        droop.power_reference = sae::PowerReference::MomentaryPower;
        droop.power_reference_L2 = sae::PowerReference::MaximumActivePower;
        droop.power_reference_L3 = std::nullopt;
        droop.open_loop_response_time = 5.0f;

        der_control.reactive_power_support.constant_power_factor.power_factor_excitation_L2 =
            sae::PowerFactorExcitation::UnderExcited;
        der_control.active_power_support.limit_max_discharge_power.percentage_value_L2 = 80;
        der_control.active_power_support.limit_max_discharge_power.open_loop_response_time = 2.5f;
        der_control.reactive_power_support.volt_var.time_constant_pt1 = 7;

        dt::sae::DERControlCPDRes out{};
        iso15118::d20::state::convert(out, der_control);

        THEN("The optional may trip curve is carried over with its points") {
            REQUIRE(out.voltage_trip.over_voltage_may_trip_curve.has_value() == true);
            const auto& curve = out.voltage_trip.over_voltage_may_trip_curve.value();
            REQUIRE(curve.enable == true);
            REQUIRE(curve.priority == 3);
            REQUIRE(curve.curve_data_points.size() == 2);
            REQUIRE_THAT(value_of(curve.curve_data_points[0].x_value), WithinAbs(115.0f, TOLERANCE));
            REQUIRE_THAT(value_of(curve.curve_data_points[1].y_value), WithinAbs(0.5f, TOLERANCE));
            REQUIRE(curve.curve_data_points_L2.has_value() == true);
            REQUIRE(curve.curve_data_points_L2.value().size() == 2);
            REQUIRE_THAT(value_of(curve.curve_data_points_L2.value()[0].x_value), WithinAbs(116.0f, TOLERANCE));
            REQUIRE(curve.curve_data_points_L3.has_value() == false);
        }

        THEN("The frequency droop settings are carried over") {
            REQUIRE(out.active_power_support_cpd_res.frequency_droop.over_frequency_droop.has_value() == true);
            const auto& settings = out.active_power_support_cpd_res.frequency_droop.over_frequency_droop.value();
            REQUIRE_THAT(value_of(settings.db), WithinAbs(0.036f, 0.001f));
            REQUIRE_THAT(value_of(settings.droop_factor), WithinAbs(20.0f, TOLERANCE));
            REQUIRE(settings.droop_factor_L2.has_value() == true);
            REQUIRE_THAT(value_of(settings.droop_factor_L2.value()), WithinAbs(21.0f, TOLERANCE));
            REQUIRE(settings.droop_factor_L3.has_value() == false);
            REQUIRE(settings.power_reference == dt::sae::PowerReference::MomentaryPower);
            REQUIRE(settings.power_reference_L2 == dt::sae::PowerReference::MaximumActivePower);
            REQUIRE(settings.power_reference_L3.has_value() == false);
            REQUIRE_THAT(value_of(settings.open_loop_response_time), WithinAbs(5.0f, TOLERANCE));

            REQUIRE(out.active_power_support_cpd_res.frequency_droop.under_frequency_droop.has_value() == false);
        }

        THEN("The remaining per phase optionals are carried over") {
            REQUIRE(out.reactive_power_support_cpd_res.constant_power_factor.power_factor_excitation_L2 ==
                    dt::sae::PowerFactorExcitation::UnderExcited);
            REQUIRE(out.active_power_support_cpd_res.limit_max_discharge_power.percentage_value_L2 == 80);
            REQUIRE(out.active_power_support_cpd_res.limit_max_discharge_power.open_loop_response_time.has_value() ==
                    true);
            REQUIRE_THAT(
                value_of(out.active_power_support_cpd_res.limit_max_discharge_power.open_loop_response_time.value()),
                WithinAbs(2.5f, TOLERANCE));
            REQUIRE(out.reactive_power_support_cpd_res.volt_var.time_constant_pt1 == 7);
        }
    }
}
