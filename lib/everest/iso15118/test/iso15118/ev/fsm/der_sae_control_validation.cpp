// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#include <catch2/catch_test_macros.hpp>

#include <functional>
#include <string>
#include <vector>

#include <iso15118/ev/der_sae_control_validation.hpp>

using namespace iso15118;

namespace dt = message_20::datatypes;
namespace sae = dt::sae;

using ev::DerControlProblems;
using ev::validate_der_control;
using U = sae::DERUnit;

namespace {

sae::CurveDataPointsList make_points(std::initializer_list<float> x_values,
                                     std::initializer_list<float> y_values = {}) {
    sae::CurveDataPointsList points;
    auto y = y_values.begin();
    for (const auto x : x_values) {
        points.push_back({dt::from_float(x), dt::from_float(y != y_values.end() ? *y++ : 1.0f)});
    }
    return points;
}

// AMD1 M.2.2.1.10 and M.2.2.1.11: x is the duration in s, y the voltage or frequency.
sae::DERCurve make_trip_curve(U y_unit, std::initializer_list<float> y_values) {
    sae::DERCurve curve;
    curve.enable = true;
    curve.x_unit = U::s;
    curve.y_unit = y_unit;
    curve.curve_data_points = make_points({0.16f, 2.0f}, y_values);
    return curve;
}

sae::DERControlCPDRes make_well_formed_cpd_res() {
    sae::DERControlCPDRes res;

    auto& voltage = res.voltage_trip;
    voltage.over_voltage_must_trip_curve = make_trip_curve(U::V, {264.0f, 253.0f});
    voltage.under_voltage_must_trip_curve = make_trip_curve(U::V, {196.0f, 160.0f});
    voltage.over_voltage_momentary_cessation_trip_curve = make_trip_curve(U::PercentageV, {110.0f, 105.0f});
    voltage.under_voltage_momentary_cessation_trip_curve = make_trip_curve(U::PercentageV, {50.0f, 45.0f});
    voltage.over_voltage_may_trip_curve = make_trip_curve(U::PercentageV, {120.0f, 110.0f});
    voltage.under_voltage_may_trip_curve = make_trip_curve(U::V, {160.0f, 140.0f});

    auto& frequency = res.frequency_trip;
    frequency.over_frequency_must_trip_curve = make_trip_curve(U::Hz, {62.0f, 61.2f});
    frequency.under_frequency_must_trip_curve = make_trip_curve(U::Hz, {58.0f, 57.0f});
    frequency.over_frequency_may_trip_curve = make_trip_curve(U::Hz, {61.8f, 61.0f});
    frequency.under_frequency_may_trip_curve = make_trip_curve(U::Hz, {57.5f, 56.5f});

    auto& reactive = res.reactive_power_support_cpd_res;
    reactive.constant_power_factor.enable = true;
    reactive.constant_power_factor.power_factor_value = {95, -2};
    reactive.constant_power_factor.power_factor_value_L2 = dt::RationalNumber{9, -1};
    reactive.constant_power_factor.power_factor_value_L3 = dt::RationalNumber{9, -1};
    reactive.volt_var.enable = true;
    reactive.volt_var.x_unit = U::PercentageV;
    reactive.volt_var.y_unit = U::PercentageEVMaximumConfiguredReactivePower;
    reactive.volt_var.curve_data_points = make_points({92.0f, 98.0f, 102.0f, 108.0f});
    reactive.volt_var.curve_data_points_L2 = make_points({92.0f, 108.0f});
    reactive.volt_var.curve_data_points_L3 = make_points({92.0f, 108.0f});
    reactive.watt_var.enable = true;
    reactive.watt_var.x_unit = U::PercentageEVMaximumConfiguredActivePower;
    reactive.watt_var.y_unit = U::PercentageEVMaximumConfiguredReactivePower;
    reactive.watt_var.curve_data_points = make_points({-100.0f, 0.0f, 100.0f});
    reactive.constant_var.enable = true;
    reactive.constant_var.unit = U::PercentageEVMaximumConfiguredReactivePower;

    auto& active = res.active_power_support_cpd_res;
    active.volt_watt.enable = true;
    active.volt_watt.x_unit = U::PercentageV;
    active.volt_watt.y_unit = U::PercentageEVMaximumConfiguredActivePower;
    active.volt_watt.curve_data_points = make_points({106.0f, 110.0f});
    active.volt_watt.curve_data_points_L2 = make_points({106.0f, 110.0f});
    active.volt_watt.curve_data_points_L3 = make_points({106.0f, 110.0f});
    active.constant_watt.enable = true;
    active.constant_watt.unit = U::PercentageEVMaximumConfiguredActivePower;
    active.limit_max_discharge_power.enable = true;
    active.limit_max_discharge_power.percentage_value = 90;
    active.limit_max_discharge_power.percentage_value_L2 = 90;
    active.limit_max_discharge_power.percentage_value_L3 = 90;

    return res;
}

sae::DERControlCLRes make_well_formed_cl_res() {
    const auto cpd = make_well_formed_cpd_res();
    const auto& reactive = cpd.reactive_power_support_cpd_res;
    const auto& active = cpd.active_power_support_cpd_res;

    sae::DERControlCLRes res;
    res.voltage_trip = cpd.voltage_trip;
    res.frequency_trip = cpd.frequency_trip;
    res.reactive_power_support_cl_res = sae::ReactivePowerSupportCLRes{
        reactive.constant_power_factor, reactive.volt_var, reactive.watt_var, reactive.constant_var};
    res.active_power_support_cl_res = sae::ActivePowerSupportCLRes{
        active.frequency_droop, active.volt_watt, active.constant_watt, active.limit_max_discharge_power};
    return res;
}

template <typename ResT> struct Case {
    std::function<void(ResT&)> mutate;
    DerControlProblems expected;
};

template <typename ResT> void check_cases(ResT (*make)(), const std::vector<Case<ResT>>& cases) {
    for (std::size_t i = 0; i < cases.size(); ++i) {
        auto res = make();
        cases[i].mutate(res);
        CAPTURE(i);
        CHECK(validate_der_control(res) == cases[i].expected);
    }
}

const std::string duration_hint = " (trip curves carry the duration on x)";
const std::string var_units =
    "{var, PercentageEVMaximumConfiguredReactivePower, PercentageEVMaximumAvailableReactivePower}";
const std::string watt_units = "{W, PercentageEVMaximumConfiguredActivePower, PercentageEVMaximumAvailableActivePower}";
const std::string constant_watt_units = "{PercentageEVMaximumConfiguredActivePower, "
                                        "PercentageEVMaximumConfiguredApparentPower, "
                                        "PercentageEVMaximumAvailableActivePower}";
const std::string constant_var_units = "{PercentageEVMaximumConfiguredActivePower, "
                                       "PercentageEVMaximumConfiguredReactivePower, "
                                       "PercentageEVMaximumAvailableReactivePower}";

} // namespace

SCENARIO("SAE DER control structural validation") {
    GIVEN("Every trip curve in the AMD1 convention: x in s, y in V, PercentageV or Hz") {
        // The SAE EXI fixtures on main put the value on x. They disagree with AMD1 and are not the oracle.
        const auto cpd = make_well_formed_cpd_res();
        const auto& under = cpd.voltage_trip.under_voltage_must_trip_curve.curve_data_points;
        REQUIRE(dt::from_RationalNumber(under[1].x_value) > dt::from_RationalNumber(under[0].x_value));
        REQUIRE(dt::from_RationalNumber(under[1].y_value) < dt::from_RationalNumber(under[0].y_value));

        THEN("Both messages validate clean") {
            CHECK(validate_der_control(cpd).empty());
            CHECK(validate_der_control(make_well_formed_cl_res()).empty());
        }
    }

    GIVEN("A DERControlCLRes with all optional blocks absent") {
        THEN("Validation reports no problems") {
            CHECK(validate_der_control(sae::DERControlCLRes{}).empty());
        }
    }

    GIVEN("Disabled mandatory blocks with filler content") {
        auto res = make_well_formed_cpd_res();
        res.voltage_trip.over_voltage_must_trip_curve.enable = false;
        res.voltage_trip.over_voltage_must_trip_curve.x_unit = U::V;
        res.reactive_power_support_cpd_res.constant_power_factor.enable = false;
        res.reactive_power_support_cpd_res.constant_power_factor.power_factor_value = {0, 0};
        res.reactive_power_support_cpd_res.constant_var.enable = false;
        res.reactive_power_support_cpd_res.constant_var.unit = U::W;
        res.active_power_support_cpd_res.volt_watt.enable = false;
        res.active_power_support_cpd_res.volt_watt.x_unit = U::W;
        res.active_power_support_cpd_res.volt_watt.curve_data_points = make_points({1.0f});
        res.active_power_support_cpd_res.limit_max_discharge_power.enable = false;
        res.active_power_support_cpd_res.limit_max_discharge_power.percentage_value = 200;

        THEN("Validation reports no problems") {
            CHECK(validate_der_control(res).empty());
        }
    }

    GIVEN("A DERControlCPDRes with one site broken at a time") {
        using R = sae::DERControlCPDRes;
        const std::vector<Case<R>> cases{
            {[](R& r) { r.voltage_trip.over_voltage_must_trip_curve.x_unit = U::V; },
             {"VoltageTrip.over_voltage_must_trip_curve: x_unit V not in {s}" + duration_hint}},
            {[](R& r) { r.voltage_trip.under_voltage_must_trip_curve.y_unit = U::Hz; },
             {"VoltageTrip.under_voltage_must_trip_curve: y_unit Hz not in {V, PercentageV}"}},
            {[](R& r) {
                 r.voltage_trip.under_voltage_must_trip_curve.curve_data_points = make_points({2.0f, 0.5f});
             },
             {"VoltageTrip.under_voltage_must_trip_curve: x decreases at point[1] (0.5 after 2)"}},
            {[](R& r) { r.voltage_trip.over_voltage_momentary_cessation_trip_curve->y_unit = U::s; },
             {"VoltageTrip.over_voltage_momentary_cessation_trip_curve: y_unit s not in {V, PercentageV}"}},
            {[](R& r) { r.voltage_trip.under_voltage_momentary_cessation_trip_curve->x_unit = U::PercentageV; },
             {"VoltageTrip.under_voltage_momentary_cessation_trip_curve: x_unit PercentageV not in {s}" +
              duration_hint}},
            {[](R& r) {
                 r.voltage_trip.over_voltage_may_trip_curve->curve_data_points_L2 = make_points({2.0f, 0.5f});
             },
             {"VoltageTrip.over_voltage_may_trip_curve(L2): x decreases at point[1] (0.5 after 2)"}},
            {[](R& r) { r.voltage_trip.under_voltage_may_trip_curve->curve_data_points_L3 = make_points({0.16f}); },
             {"VoltageTrip.under_voltage_may_trip_curve(L3): 1 point(s), minimum is 2"}},
            {[](R& r) { r.frequency_trip.over_frequency_must_trip_curve.x_unit = U::Hz; },
             {"FrequencyTrip.over_frequency_must_trip_curve: x_unit Hz not in {s}" + duration_hint}},
            {[](R& r) { r.frequency_trip.under_frequency_must_trip_curve.y_unit = U::s; },
             {"FrequencyTrip.under_frequency_must_trip_curve: y_unit s not in {Hz}"}},
            {[](R& r) { r.frequency_trip.over_frequency_may_trip_curve->y_unit = U::V; },
             {"FrequencyTrip.over_frequency_may_trip_curve: y_unit V not in {Hz}"}},
            {[](R& r) {
                 auto& points = r.frequency_trip.under_frequency_may_trip_curve->curve_data_points;
                 points[0].x_value = {2, -7};
                 points[1].x_value = {5, -8};
             },
             {"FrequencyTrip.under_frequency_may_trip_curve: x decreases at point[1] (5e-08 after 2e-07)"}},
            {[](R& r) {
                 r.reactive_power_support_cpd_res.constant_power_factor.power_factor_value = {0, 0};
             },
             {"ReactivePowerSupport.constant_power_factor: power_factor_value 0 not in (0, 1]"}},
            {[](R& r) {
                 r.reactive_power_support_cpd_res.constant_power_factor.power_factor_value = {-9, -1};
             },
             {"ReactivePowerSupport.constant_power_factor: power_factor_value -0.9 not in (0, 1]"}},
            {[](R& r) {
                 r.reactive_power_support_cpd_res.constant_power_factor.power_factor_value_L2 =
                     dt::RationalNumber{105, -2};
             },
             {"ReactivePowerSupport.constant_power_factor(L2): power_factor_value 1.05 not in (0, 1]"}},
            {[](R& r) {
                 r.reactive_power_support_cpd_res.constant_power_factor.power_factor_value_L3 =
                     dt::RationalNumber{1, 127};
             },
             {"ReactivePowerSupport.constant_power_factor(L3): power_factor_value inf not in (0, 1]"}},
            {[](R& r) {
                 r.reactive_power_support_cpd_res.constant_power_factor.power_factor_value = {1, 0};
             },
             {}},
            {[](R& r) { r.reactive_power_support_cpd_res.volt_var.x_unit = U::W; },
             {"ReactivePowerSupport.volt_var: x_unit W not in {V, PercentageV}"}},
            {[](R& r) { r.reactive_power_support_cpd_res.volt_var.y_unit = U::W; },
             {"ReactivePowerSupport.volt_var: y_unit W not in " + var_units}},
            {[](R& r) {
                 r.reactive_power_support_cpd_res.volt_var.curve_data_points = make_points({108.0f, 92.0f});
             },
             {"ReactivePowerSupport.volt_var: x decreases at point[1] (92 after 108)"}},
            {[](R& r) {
                 r.reactive_power_support_cpd_res.volt_var.curve_data_points = make_points({100.0f, 100.0f});
             },
             {}},
            {[](R& r) { r.reactive_power_support_cpd_res.volt_var.curve_data_points_L2 = make_points({92.0f}); },
             {"ReactivePowerSupport.volt_var(L2): 1 point(s), minimum is 2"}},
            {[](R& r) { r.reactive_power_support_cpd_res.watt_var.x_unit = U::var; },
             {"ReactivePowerSupport.watt_var: x_unit var not in " + watt_units}},
            {[](R& r) { r.reactive_power_support_cpd_res.watt_var.y_unit = U::W; },
             {"ReactivePowerSupport.watt_var: y_unit W not in " + var_units}},
            {[](R& r) { r.reactive_power_support_cpd_res.watt_var.curve_data_points = make_points({0.0f}); },
             {"ReactivePowerSupport.watt_var: 1 point(s), minimum is 2"}},
            {[](R& r) { r.reactive_power_support_cpd_res.constant_var.unit = U::W; },
             {"ReactivePowerSupport.constant_var: unit W not in " + constant_var_units}},
            {[](R& r) { r.reactive_power_support_cpd_res.constant_var.unit = U::var; },
             {"ReactivePowerSupport.constant_var: unit var not in " + constant_var_units}},
            {[](R& r) {
                 r.reactive_power_support_cpd_res.constant_var.unit = U::PercentageEVMaximumConfiguredActivePower;
             },
             {}},
            {[](R& r) {
                 r.reactive_power_support_cpd_res.constant_var.unit = U::PercentageEVMaximumAvailableReactivePower;
             },
             {}},
            {[](R& r) { r.active_power_support_cpd_res.volt_watt.x_unit = U::W; },
             {"ActivePowerSupport.volt_watt: x_unit W not in {V, PercentageV}"}},
            {[](R& r) { r.active_power_support_cpd_res.volt_watt.y_unit = U::var; },
             {"ActivePowerSupport.volt_watt: y_unit var not in " + watt_units}},
            {[](R& r) {
                 r.active_power_support_cpd_res.volt_watt.curve_data_points_L3 = make_points({110.0f, 106.0f});
             },
             {"ActivePowerSupport.volt_watt(L3): x decreases at point[1] (106 after 110)"}},
            {[](R& r) { r.active_power_support_cpd_res.volt_watt.curve_data_points_L3 = make_points({106.0f}); },
             {"ActivePowerSupport.volt_watt(L3): 1 point(s), minimum is 2"}},
            {[](R& r) { r.active_power_support_cpd_res.constant_watt.unit = U::var; },
             {"ActivePowerSupport.constant_watt: unit var not in " + constant_watt_units}},
            {[](R& r) { r.active_power_support_cpd_res.constant_watt.unit = U::W; },
             {"ActivePowerSupport.constant_watt: unit W not in " + constant_watt_units}},
            {[](R& r) {
                 r.active_power_support_cpd_res.constant_watt.unit = U::PercentageEVMaximumConfiguredApparentPower;
             },
             {}},
            {[](R& r) { r.active_power_support_cpd_res.limit_max_discharge_power.percentage_value = 100; }, {}},
            {[](R& r) { r.active_power_support_cpd_res.limit_max_discharge_power.percentage_value = 101; },
             {"ActivePowerSupport.limit_max_discharge_power: percentage_value 101 not in [0, 100]"}},
            {[](R& r) { r.active_power_support_cpd_res.limit_max_discharge_power.percentage_value_L2 = 101; },
             {"ActivePowerSupport.limit_max_discharge_power(L2): percentage_value 101 not in [0, 100]"}},
        };

        THEN("Exactly the broken site is reported") {
            check_cases(&make_well_formed_cpd_res, cases);
        }
    }

    GIVEN("A DERControlCLRes with one block broken at a time") {
        using R = sae::DERControlCLRes;
        const std::vector<Case<R>> cases{
            {[](R& r) { r.voltage_trip->over_voltage_must_trip_curve.x_unit = U::V; },
             {"VoltageTrip.over_voltage_must_trip_curve: x_unit V not in {s}" + duration_hint}},
            {[](R& r) { r.frequency_trip->over_frequency_must_trip_curve.y_unit = U::W; },
             {"FrequencyTrip.over_frequency_must_trip_curve: y_unit W not in {Hz}"}},
            {[](R& r) {
                 r.reactive_power_support_cl_res->constant_power_factor->power_factor_value = {0, 0};
             },
             {"ReactivePowerSupport.constant_power_factor: power_factor_value 0 not in (0, 1]"}},
            {[](R& r) {
                 r.reactive_power_support_cl_res->volt_var->curve_data_points = make_points({108.0f, 92.0f});
             },
             {"ReactivePowerSupport.volt_var: x decreases at point[1] (92 after 108)"}},
            {[](R& r) { r.reactive_power_support_cl_res->watt_var->y_unit = U::W; },
             {"ReactivePowerSupport.watt_var: y_unit W not in " + var_units}},
            {[](R& r) { r.reactive_power_support_cl_res->constant_var->unit = U::var; },
             {"ReactivePowerSupport.constant_var: unit var not in " + constant_var_units}},
            {[](R& r) { r.active_power_support_cl_res->volt_watt->x_unit = U::W; },
             {"ActivePowerSupport.volt_watt: x_unit W not in {V, PercentageV}"}},
            {[](R& r) { r.active_power_support_cl_res->constant_watt->unit = U::var; },
             {"ActivePowerSupport.constant_watt: unit var not in " + constant_watt_units}},
            {[](R& r) { r.active_power_support_cl_res->limit_max_discharge_power->percentage_value = 101; },
             {"ActivePowerSupport.limit_max_discharge_power: percentage_value 101 not in [0, 100]"}},
        };

        THEN("Exactly the broken block is reported") {
            check_cases(&make_well_formed_cl_res, cases);
        }
    }
}
