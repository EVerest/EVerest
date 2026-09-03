// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include <gtest/gtest.h>

#include <algorithm>
#include <optional>
#include <string>
#include <vector>

#include <iso15118/d20/config.hpp>
#include <iso15118/d20/der_functions.hpp>

#include "der_relay_sae.hpp"

namespace {

using module::map_active_directives_to_sae_der_control;
namespace gs = types::grid_support;
namespace sae = iso15118::sae;
using DT = gs::DirectiveType;

constexpr float NOMINAL_V = 230.0f;
constexpr float NOMINAL_HZ = 50.0f;

gs::DERCurvePoint pt(float x, float y) {
    gs::DERCurvePoint p{};
    p.x = x;
    p.y = y;
    return p;
}

gs::Directive make_directive(DT type, const std::string& id = "d1", int priority = 0) {
    gs::Directive d{};
    d.id = id;
    d.directive_type = type;
    d.priority = priority;
    d.is_default = false;
    d.source = "test";
    d.received_at = "2026-07-10T00:00:00Z";
    return d;
}

gs::Directive make_curve_directive(DT type, gs::DERUnit y_unit, std::vector<gs::DERCurvePoint> points,
                                   std::optional<float> response_time = std::nullopt, const std::string& id = "d1",
                                   int priority = 0) {
    auto d = make_directive(type, id, priority);
    gs::DERCurve curve{};
    curve.curve_data = std::move(points);
    curve.y_unit = y_unit;
    curve.response_time = response_time;
    d.curve = std::move(curve);
    return d;
}

gs::EnterServiceParams make_enter_service(float high_voltage, float low_voltage, float high_freq, float low_freq) {
    gs::EnterServiceParams es{};
    es.high_voltage = high_voltage;
    es.low_voltage = low_voltage;
    es.high_freq = high_freq;
    es.low_freq = low_freq;
    return es;
}

gs::FreqDroopParams make_freq_droop(float over_freq, float under_freq, float over_droop, float under_droop,
                                    float response_time) {
    gs::FreqDroopParams fd{};
    fd.over_freq = over_freq;
    fd.under_freq = under_freq;
    fd.over_droop = over_droop;
    fd.under_droop = under_droop;
    fd.response_time = response_time;
    return fd;
}

gs::FixedPFParams make_fixed_pf(float displacement, bool excitation) {
    gs::FixedPFParams pf{};
    pf.displacement = displacement;
    pf.excitation = excitation;
    return pf;
}

gs::FixedVarParams make_fixed_var(float setpoint, gs::DERUnit unit) {
    gs::FixedVarParams fv{};
    fv.setpoint = setpoint;
    fv.unit = unit;
    return fv;
}

gs::ActiveDirectiveSet make_set(std::vector<gs::Directive> directives) {
    gs::ActiveDirectiveSet set{};
    set.evse_id = 1;
    set.directives = std::move(directives);
    return set;
}

void expect_curve_eq(const sae::DERCurve& actual, const sae::DERCurve& expected) {
    EXPECT_EQ(actual.enable, expected.enable);
    EXPECT_EQ(actual.priority, expected.priority);
    EXPECT_EQ(actual.x_unit, expected.x_unit);
    EXPECT_EQ(actual.y_unit, expected.y_unit);
    ASSERT_EQ(actual.curve_data_points.size(), expected.curve_data_points.size());
    for (std::size_t i = 0; i < expected.curve_data_points.size(); ++i) {
        EXPECT_FLOAT_EQ(actual.curve_data_points[i].x_value, expected.curve_data_points[i].x_value);
        EXPECT_FLOAT_EQ(actual.curve_data_points[i].y_value, expected.curve_data_points[i].y_value);
    }
    EXPECT_FALSE(actual.curve_data_points_L2.has_value());
    EXPECT_FALSE(actual.curve_data_points_L3.has_value());
}

void expect_default_baseline(const sae::DERControl& actual) {
    const auto def = iso15118::d20::get_default_sae_der_control(NOMINAL_V);

    EXPECT_EQ(actual.enter_service.permit_service, def.enter_service.permit_service);
    EXPECT_FLOAT_EQ(actual.enter_service.enter_service_voltage_high, def.enter_service.enter_service_voltage_high);
    EXPECT_FLOAT_EQ(actual.enter_service.enter_service_voltage_low, def.enter_service.enter_service_voltage_low);
    EXPECT_FLOAT_EQ(actual.enter_service.enter_service_frequency_high, def.enter_service.enter_service_frequency_high);
    EXPECT_FLOAT_EQ(actual.enter_service.enter_service_frequency_low, def.enter_service.enter_service_frequency_low);
    EXPECT_EQ(actual.enter_service.enter_service_delay, def.enter_service.enter_service_delay);
    EXPECT_EQ(actual.enter_service.enter_service_randomized_delay, def.enter_service.enter_service_randomized_delay);
    EXPECT_EQ(actual.enter_service.enter_service_ramp_time, def.enter_service.enter_service_ramp_time);

    expect_curve_eq(actual.voltage_trip.over_voltage_must_trip_curve, def.voltage_trip.over_voltage_must_trip_curve);
    expect_curve_eq(actual.voltage_trip.under_voltage_must_trip_curve, def.voltage_trip.under_voltage_must_trip_curve);
    expect_curve_eq(actual.frequency_trip.over_frequency_must_trip_curve,
                    def.frequency_trip.over_frequency_must_trip_curve);
    expect_curve_eq(actual.frequency_trip.under_frequency_must_trip_curve,
                    def.frequency_trip.under_frequency_must_trip_curve);

    EXPECT_FALSE(actual.voltage_trip.over_voltage_may_trip_curve.has_value());
    EXPECT_FALSE(actual.voltage_trip.under_voltage_may_trip_curve.has_value());
    EXPECT_FALSE(actual.voltage_trip.over_voltage_momentary_cessation_trip_curve.has_value());
    EXPECT_FALSE(actual.voltage_trip.under_voltage_momentary_cessation_trip_curve.has_value());
    EXPECT_FALSE(actual.frequency_trip.over_frequency_may_trip_curve.has_value());
    EXPECT_FALSE(actual.frequency_trip.under_frequency_may_trip_curve.has_value());

    EXPECT_FALSE(actual.reactive_power_support.constant_power_factor.enable);
    EXPECT_FALSE(actual.reactive_power_support.volt_var.enable);
    EXPECT_FALSE(actual.reactive_power_support.watt_var.enable);
    EXPECT_FALSE(actual.reactive_power_support.constant_var.enable);
    EXPECT_FALSE(actual.active_power_support.frequency_droop.enable);
    EXPECT_FALSE(actual.active_power_support.volt_watt.enable);
    EXPECT_FALSE(actual.active_power_support.constant_watt.enable);
    EXPECT_FALSE(actual.active_power_support.limit_max_discharge_power.enable);
    EXPECT_EQ(actual.active_power_support.limit_max_discharge_power.percentage_value,
              def.active_power_support.limit_max_discharge_power.percentage_value);
}

} // namespace

TEST(DerSaeRelayTest, empty_set_yields_inert_default) {
    const auto out = map_active_directives_to_sae_der_control(make_set({}), NOMINAL_V, NOMINAL_HZ);
    expect_default_baseline(out.der_control);
    EXPECT_TRUE(out.shadowed_ids.empty());
    EXPECT_TRUE(out.unmapped.empty());
}

TEST(DerSaeRelayTest, hv_must_trip_transposes_to_duration_x) {
    // grid_support carries x = percent of nominal voltage, y = seconds; Annex M wants the axes swapped.
    const auto d =
        make_curve_directive(DT::HVMustTrip, gs::DERUnit::Not_Applicable, {pt(110.0f, 2.0f), pt(120.0f, 0.16f)});
    const auto out = map_active_directives_to_sae_der_control(make_set({d}), NOMINAL_V, NOMINAL_HZ);

    const auto& curve = out.der_control.voltage_trip.over_voltage_must_trip_curve;
    EXPECT_TRUE(curve.enable);
    EXPECT_EQ(curve.x_unit, sae::DERUnit::s);
    EXPECT_EQ(curve.y_unit, sae::DERUnit::PercentageV);
    ASSERT_EQ(curve.curve_data_points.size(), 2u);
    EXPECT_FLOAT_EQ(curve.curve_data_points[0].x_value, 0.16f);
    EXPECT_FLOAT_EQ(curve.curve_data_points[0].y_value, 120.0f);
    EXPECT_FLOAT_EQ(curve.curve_data_points[1].x_value, 2.0f);
    EXPECT_FLOAT_EQ(curve.curve_data_points[1].y_value, 110.0f);
    EXPECT_TRUE(out.shadowed_ids.empty());
    EXPECT_TRUE(out.unmapped.empty());
}

TEST(DerSaeRelayTest, hv_may_trip_emplaces_optional_curve) {
    const auto d =
        make_curve_directive(DT::HVMayTrip, gs::DERUnit::Not_Applicable, {pt(118.0f, 0.16f), pt(110.0f, 2.0f)});
    const auto out = map_active_directives_to_sae_der_control(make_set({d}), NOMINAL_V, NOMINAL_HZ);

    ASSERT_TRUE(out.der_control.voltage_trip.over_voltage_may_trip_curve.has_value());
    const auto& curve = out.der_control.voltage_trip.over_voltage_may_trip_curve.value();
    EXPECT_TRUE(curve.enable);
    EXPECT_EQ(curve.x_unit, sae::DERUnit::s);
    EXPECT_EQ(curve.y_unit, sae::DERUnit::PercentageV);
    ASSERT_EQ(curve.curve_data_points.size(), 2u);
}

TEST(DerSaeRelayTest, hf_must_trip_transposes_with_hz_y) {
    const auto d =
        make_curve_directive(DT::HFMustTrip, gs::DERUnit::Not_Applicable, {pt(51.5f, 300.0f), pt(52.0f, 0.16f)});
    const auto out = map_active_directives_to_sae_der_control(make_set({d}), NOMINAL_V, NOMINAL_HZ);

    const auto& curve = out.der_control.frequency_trip.over_frequency_must_trip_curve;
    EXPECT_TRUE(curve.enable);
    EXPECT_EQ(curve.x_unit, sae::DERUnit::s);
    EXPECT_EQ(curve.y_unit, sae::DERUnit::Hz);
    ASSERT_EQ(curve.curve_data_points.size(), 2u);
    EXPECT_FLOAT_EQ(curve.curve_data_points[0].x_value, 0.16f);
    EXPECT_FLOAT_EQ(curve.curve_data_points[0].y_value, 52.0f);
    EXPECT_FLOAT_EQ(curve.curve_data_points[1].x_value, 300.0f);
    EXPECT_FLOAT_EQ(curve.curve_data_points[1].y_value, 51.5f);
}

namespace {

struct TripRoute {
    DT type;
    sae::DERUnit y_unit;
    // Selects the written curve; the optional members are checked for presence.
    const sae::DERCurve* (*pick)(const sae::DERControl&);
};

const sae::DERCurve* opt(const std::optional<sae::DERCurve>& c) {
    return c.has_value() ? &c.value() : nullptr;
}

const TripRoute TRIP_ROUTES[] = {
    {DT::HVMustTrip, sae::DERUnit::PercentageV,
     [](const sae::DERControl& c) { return &c.voltage_trip.over_voltage_must_trip_curve; }},
    {DT::HVMayTrip, sae::DERUnit::PercentageV,
     [](const sae::DERControl& c) { return opt(c.voltage_trip.over_voltage_may_trip_curve); }},
    {DT::HVMomCess, sae::DERUnit::PercentageV,
     [](const sae::DERControl& c) { return opt(c.voltage_trip.over_voltage_momentary_cessation_trip_curve); }},
    {DT::LVMustTrip, sae::DERUnit::PercentageV,
     [](const sae::DERControl& c) { return &c.voltage_trip.under_voltage_must_trip_curve; }},
    {DT::LVMayTrip, sae::DERUnit::PercentageV,
     [](const sae::DERControl& c) { return opt(c.voltage_trip.under_voltage_may_trip_curve); }},
    {DT::LVMomCess, sae::DERUnit::PercentageV,
     [](const sae::DERControl& c) { return opt(c.voltage_trip.under_voltage_momentary_cessation_trip_curve); }},
    {DT::HFMustTrip, sae::DERUnit::Hz,
     [](const sae::DERControl& c) { return &c.frequency_trip.over_frequency_must_trip_curve; }},
    {DT::HFMayTrip, sae::DERUnit::Hz,
     [](const sae::DERControl& c) { return opt(c.frequency_trip.over_frequency_may_trip_curve); }},
    {DT::LFMustTrip, sae::DERUnit::Hz,
     [](const sae::DERControl& c) { return &c.frequency_trip.under_frequency_must_trip_curve; }},
};

} // namespace

TEST(DerSaeRelayTest, every_trip_type_routes_to_its_own_curve_and_leaves_the_rest_default) {
    const auto def = iso15118::d20::get_default_sae_der_control(NOMINAL_V);
    for (const auto& route : TRIP_ROUTES) {
        const auto d = make_curve_directive(route.type, gs::DERUnit::Not_Applicable, {pt(7.0f, 0.5f), pt(9.0f, 0.1f)},
                                            std::nullopt, "d1", 4);
        const auto out = map_active_directives_to_sae_der_control(make_set({d}), NOMINAL_V, NOMINAL_HZ);
        const auto name = std::string(gs::directive_type_to_string_view(route.type));

        const auto* written = route.pick(out.der_control);
        ASSERT_NE(written, nullptr) << name;
        EXPECT_TRUE(written->enable) << name;
        EXPECT_EQ(written->x_unit, sae::DERUnit::s) << name;
        EXPECT_EQ(written->y_unit, route.y_unit) << name;
        ASSERT_TRUE(written->priority.has_value()) << name;
        EXPECT_EQ(written->priority.value(), 4u) << name;
        ASSERT_EQ(written->curve_data_points.size(), 2u) << name;
        EXPECT_FLOAT_EQ(written->curve_data_points[0].x_value, 0.1f) << name;
        EXPECT_FLOAT_EQ(written->curve_data_points[0].y_value, 9.0f) << name;

        for (const auto& other : TRIP_ROUTES) {
            if (other.type == route.type) {
                continue;
            }
            const auto* other_curve = other.pick(out.der_control);
            const auto* other_default = other.pick(def);
            const auto other_name = name + " vs " + std::string(gs::directive_type_to_string_view(other.type));
            if (other_default == nullptr) {
                EXPECT_EQ(other_curve, nullptr) << other_name;
            } else {
                ASSERT_NE(other_curve, nullptr) << other_name;
                expect_curve_eq(*other_curve, *other_default);
            }
        }
        EXPECT_TRUE(out.shadowed_ids.empty()) << name;
        EXPECT_TRUE(out.unmapped.empty()) << name;
    }
}

TEST(DerSaeRelayTest, one_point_must_trip_is_rejected_and_keeps_default_curve) {
    const auto d = make_curve_directive(DT::HVMustTrip, gs::DERUnit::Not_Applicable, {pt(120.0f, 0.16f)});
    const auto out = map_active_directives_to_sae_der_control(make_set({d}), NOMINAL_V, NOMINAL_HZ);

    expect_default_baseline(out.der_control);
    EXPECT_EQ(out.der_control.voltage_trip.over_voltage_must_trip_curve.curve_data_points.size(), 2u);
    EXPECT_TRUE(out.unmapped.empty());
}

TEST(DerSaeRelayTest, trip_curve_with_duplicate_duration_is_rejected) {
    const auto d = make_curve_directive(DT::LVMustTrip, gs::DERUnit::Not_Applicable,
                                        {pt(50.0f, 0.16f), pt(88.0f, 2.0f), pt(70.0f, 2.0f)});
    const auto out = map_active_directives_to_sae_der_control(make_set({d}), NOMINAL_V, NOMINAL_HZ);

    expect_default_baseline(out.der_control);
    EXPECT_FALSE(out.der_control.voltage_trip.under_voltage_must_trip_curve.enable);
}

TEST(DerSaeRelayTest, trip_curve_with_eleven_points_is_rejected) {
    std::vector<gs::DERCurvePoint> points;
    for (int i = 1; i <= 11; ++i) {
        points.push_back(pt(100.0f + static_cast<float>(i), static_cast<float>(i)));
    }
    const auto d = make_curve_directive(DT::HVMustTrip, gs::DERUnit::Not_Applicable, points);
    const auto out = map_active_directives_to_sae_der_control(make_set({d}), NOMINAL_V, NOMINAL_HZ);

    expect_default_baseline(out.der_control);
    EXPECT_FALSE(out.der_control.voltage_trip.over_voltage_must_trip_curve.enable);
}

TEST(DerSaeRelayTest, voltvar_sets_units_reference_voltage_and_response_time) {
    auto d = make_curve_directive(DT::VoltVar, gs::DERUnit::PctMaxVar, {pt(92.0f, 44.0f), pt(108.0f, -44.0f)}, 3.0f);
    gs::ReactivePowerParams rp{};
    rp.v_ref = 101.0f;
    rp.autonomous_v_ref_enable = true;
    rp.autonomous_v_ref_time_constant = 300.0f;
    d.curve->reactive_power_params = rp;

    const auto out = map_active_directives_to_sae_der_control(make_set({d}), NOMINAL_V, NOMINAL_HZ);

    const auto& vv = out.der_control.reactive_power_support.volt_var;
    EXPECT_TRUE(vv.enable);
    EXPECT_EQ(vv.x_unit, sae::DERUnit::PercentageV);
    EXPECT_EQ(vv.y_unit, sae::DERUnit::PercentageEVMaximumConfiguredReactivePower);
    ASSERT_EQ(vv.curve_data_points.size(), 2u);
    EXPECT_FLOAT_EQ(vv.curve_data_points[0].x_value, 92.0f);
    EXPECT_FLOAT_EQ(vv.curve_data_points[0].y_value, 44.0f);
    EXPECT_FLOAT_EQ(vv.curve_data_points[1].y_value, -44.0f);
    EXPECT_FLOAT_EQ(vv.reference_voltage, 101.0f);
    EXPECT_FLOAT_EQ(vv.open_loop_response_time, 3.0f);
    EXPECT_TRUE(vv.autonomous_reference_voltage_adjustment_enable);
    EXPECT_EQ(vv.reference_voltage_adjustment_time_constant, 300u);
}

TEST(DerSaeRelayTest, voltvar_pct_var_avail_writes_available_reactive_power_unit) {
    const auto d = make_curve_directive(DT::VoltVar, gs::DERUnit::PctVarAvail, {pt(92.0f, 44.0f), pt(108.0f, -44.0f)});
    const auto out = map_active_directives_to_sae_der_control(make_set({d}), NOMINAL_V, NOMINAL_HZ);

    const auto& vv = out.der_control.reactive_power_support.volt_var;
    EXPECT_TRUE(vv.enable);
    EXPECT_EQ(vv.x_unit, sae::DERUnit::PercentageV);
    EXPECT_EQ(vv.y_unit, sae::DERUnit::PercentageEVMaximumAvailableReactivePower);
}

TEST(DerSaeRelayTest, wattvar_pct_var_avail_writes_available_reactive_power_unit) {
    const auto d = make_curve_directive(DT::WattVar, gs::DERUnit::PctVarAvail, {pt(0.0f, 0.0f), pt(100.0f, -30.0f)});
    const auto out = map_active_directives_to_sae_der_control(make_set({d}), NOMINAL_V, NOMINAL_HZ);

    const auto& wv = out.der_control.reactive_power_support.watt_var;
    EXPECT_TRUE(wv.enable);
    EXPECT_EQ(wv.x_unit, sae::DERUnit::PercentageEVMaximumConfiguredActivePower);
    EXPECT_EQ(wv.y_unit, sae::DERUnit::PercentageEVMaximumAvailableReactivePower);
}

TEST(DerSaeRelayTest, voltwatt_pct_w_avail_writes_available_active_power_unit) {
    const auto d = make_curve_directive(DT::VoltWatt, gs::DERUnit::PctWAvail, {pt(106.0f, 100.0f), pt(110.0f, 20.0f)});
    const auto out = map_active_directives_to_sae_der_control(make_set({d}), NOMINAL_V, NOMINAL_HZ);

    const auto& vw = out.der_control.active_power_support.volt_watt;
    EXPECT_TRUE(vw.enable);
    EXPECT_EQ(vw.x_unit, sae::DERUnit::PercentageV);
    EXPECT_EQ(vw.y_unit, sae::DERUnit::PercentageEVMaximumAvailableActivePower);
}

TEST(DerSaeRelayTest, response_curve_with_wrong_unit_family_is_rejected) {
    const auto vv = make_curve_directive(DT::VoltVar, gs::DERUnit::PctMaxW, {pt(92.0f, 44.0f), pt(108.0f, -44.0f)},
                                         std::nullopt, "vv");
    const auto vw = make_curve_directive(DT::VoltWatt, gs::DERUnit::PctMaxVar, {pt(106.0f, 100.0f), pt(110.0f, 20.0f)},
                                         std::nullopt, "vw");
    const auto wv = make_curve_directive(DT::WattVar, gs::DERUnit::PctEffectiveV, {pt(0.0f, 0.0f), pt(100.0f, -30.0f)},
                                         std::nullopt, "wv");
    const auto out = map_active_directives_to_sae_der_control(make_set({vv, vw, wv}), NOMINAL_V, NOMINAL_HZ);

    expect_default_baseline(out.der_control);
    EXPECT_TRUE(out.shadowed_ids.empty());
    EXPECT_TRUE(out.unmapped.empty());
}

TEST(DerSaeRelayTest, wattvar_and_voltwatt_map_to_their_slots) {
    const auto wv =
        make_curve_directive(DT::WattVar, gs::DERUnit::PctMaxVar, {pt(0.0f, 0.0f), pt(100.0f, -30.0f)}, 2.0f, "wv");
    const auto vw =
        make_curve_directive(DT::VoltWatt, gs::DERUnit::PctMaxW, {pt(106.0f, 100.0f), pt(110.0f, 20.0f)}, 4.0f, "vw");
    const auto out = map_active_directives_to_sae_der_control(make_set({wv, vw}), NOMINAL_V, NOMINAL_HZ);

    const auto& watt_var = out.der_control.reactive_power_support.watt_var;
    EXPECT_TRUE(watt_var.enable);
    EXPECT_EQ(watt_var.x_unit, sae::DERUnit::PercentageEVMaximumConfiguredActivePower);
    EXPECT_EQ(watt_var.y_unit, sae::DERUnit::PercentageEVMaximumConfiguredReactivePower);
    ASSERT_TRUE(watt_var.open_loop_response_time.has_value());
    EXPECT_FLOAT_EQ(watt_var.open_loop_response_time.value(), 2.0f);

    const auto& volt_watt = out.der_control.active_power_support.volt_watt;
    EXPECT_TRUE(volt_watt.enable);
    EXPECT_EQ(volt_watt.x_unit, sae::DERUnit::PercentageV);
    EXPECT_EQ(volt_watt.y_unit, sae::DERUnit::PercentageEVMaximumConfiguredActivePower);
    ASSERT_EQ(volt_watt.curve_data_points.size(), 2u);
    EXPECT_FLOAT_EQ(volt_watt.curve_data_points[1].y_value, 20.0f);
    EXPECT_FLOAT_EQ(volt_watt.open_loop_response_time, 4.0f);
}

TEST(DerSaeRelayTest, response_curve_with_not_applicable_y_unit_is_rejected) {
    const auto d =
        make_curve_directive(DT::VoltVar, gs::DERUnit::Not_Applicable, {pt(92.0f, 44.0f), pt(108.0f, -44.0f)});
    const auto out = map_active_directives_to_sae_der_control(make_set({d}), NOMINAL_V, NOMINAL_HZ);
    EXPECT_FALSE(out.der_control.reactive_power_support.volt_var.enable);
}

TEST(DerSaeRelayTest, lowest_priority_voltvar_wins_and_other_is_shadowed) {
    const auto low_prec = make_curve_directive(DT::VoltVar, gs::DERUnit::PctMaxVar,
                                               {pt(90.0f, 10.0f), pt(110.0f, -10.0f)}, std::nullopt, "vv5", 5);
    const auto high_prec = make_curve_directive(DT::VoltVar, gs::DERUnit::PctMaxVar,
                                                {pt(95.0f, 20.0f), pt(105.0f, -20.0f)}, std::nullopt, "vv2", 2);
    const auto out = map_active_directives_to_sae_der_control(make_set({low_prec, high_prec}), NOMINAL_V, NOMINAL_HZ);

    const auto& vv = out.der_control.reactive_power_support.volt_var;
    ASSERT_EQ(vv.curve_data_points.size(), 2u);
    EXPECT_FLOAT_EQ(vv.curve_data_points[0].x_value, 95.0f);
    ASSERT_EQ(out.shadowed_ids.size(), 1u);
    EXPECT_EQ(out.shadowed_ids[0], "vv5");
}

TEST(DerSaeRelayTest, rejected_winner_frees_slot_for_next_candidate) {
    const auto invalid = make_curve_directive(DT::VoltVar, gs::DERUnit::Not_Applicable,
                                              {pt(90.0f, 10.0f), pt(110.0f, -10.0f)}, std::nullopt, "vv1", 1);
    const auto valid = make_curve_directive(DT::VoltVar, gs::DERUnit::PctMaxVar, {pt(95.0f, 20.0f), pt(105.0f, -20.0f)},
                                            std::nullopt, "vv5", 5);
    const auto out = map_active_directives_to_sae_der_control(make_set({invalid, valid}), NOMINAL_V, NOMINAL_HZ);

    const auto& vv = out.der_control.reactive_power_support.volt_var;
    EXPECT_TRUE(vv.enable);
    ASSERT_EQ(vv.curve_data_points.size(), 2u);
    EXPECT_FLOAT_EQ(vv.curve_data_points[0].x_value, 95.0f);
    EXPECT_TRUE(out.shadowed_ids.empty());
}

TEST(DerSaeRelayTest, fixed_pf_equal_priority_first_in_array_wins) {
    auto absorb = make_directive(DT::FixedPFAbsorb, "pfa");
    absorb.fixed_pf = make_fixed_pf(0.95f, true);
    auto inject = make_directive(DT::FixedPFInject, "pfi");
    inject.fixed_pf = make_fixed_pf(0.90f, false);

    const auto out = map_active_directives_to_sae_der_control(make_set({absorb, inject}), NOMINAL_V, NOMINAL_HZ);

    const auto& cpf = out.der_control.reactive_power_support.constant_power_factor;
    EXPECT_TRUE(cpf.enable);
    EXPECT_FLOAT_EQ(cpf.power_factor_value, 0.95f);
    EXPECT_EQ(cpf.power_factor_excitation, sae::PowerFactorExcitation::OverExcited);
    ASSERT_EQ(out.shadowed_ids.size(), 1u);
    EXPECT_EQ(out.shadowed_ids[0], "pfi");
}

TEST(DerSaeRelayTest, fixed_pf_inject_under_excited) {
    auto inject = make_directive(DT::FixedPFInject, "pfi");
    inject.fixed_pf = make_fixed_pf(0.90f, false);
    const auto out = map_active_directives_to_sae_der_control(make_set({inject}), NOMINAL_V, NOMINAL_HZ);

    const auto& cpf = out.der_control.reactive_power_support.constant_power_factor;
    EXPECT_TRUE(cpf.enable);
    EXPECT_FLOAT_EQ(cpf.power_factor_value, 0.90f);
    EXPECT_EQ(cpf.power_factor_excitation, sae::PowerFactorExcitation::UnderExcited);
}

TEST(DerSaeRelayTest, fixed_var_maps_unit_and_rejects_unsupported) {
    auto ok = make_directive(DT::FixedVar, "fv");
    ok.fixed_var = make_fixed_var(25.0f, gs::DERUnit::PctVarAvail);
    auto out = map_active_directives_to_sae_der_control(make_set({ok}), NOMINAL_V, NOMINAL_HZ);
    const auto& cv = out.der_control.reactive_power_support.constant_var;
    EXPECT_TRUE(cv.enable);
    EXPECT_FLOAT_EQ(cv.var_setpoint, -25.0f);
    EXPECT_EQ(cv.unit, sae::DERUnit::PercentageEVMaximumAvailableReactivePower);

    auto bad = make_directive(DT::FixedVar, "fv-bad");
    bad.fixed_var = make_fixed_var(25.0f, gs::DERUnit::PctEffectiveV);
    out = map_active_directives_to_sae_der_control(make_set({bad}), NOMINAL_V, NOMINAL_HZ);
    EXPECT_FALSE(out.der_control.reactive_power_support.constant_var.enable);
}

TEST(DerSaeRelayTest, fixed_var_setpoint_is_negated) {
    // OCPP: negative setpoint absorbs. Table M.25: negative VarSetpoint injects.
    auto d = make_directive(DT::FixedVar, "fv");
    d.fixed_var = make_fixed_var(-30.0f, gs::DERUnit::PctMaxVar);
    const auto out = map_active_directives_to_sae_der_control(make_set({d}), NOMINAL_V, NOMINAL_HZ);

    const auto& cv = out.der_control.reactive_power_support.constant_var;
    EXPECT_TRUE(cv.enable);
    EXPECT_FLOAT_EQ(cv.var_setpoint, 30.0f);
    EXPECT_EQ(cv.unit, sae::DERUnit::PercentageEVMaximumConfiguredReactivePower);
}

TEST(DerSaeRelayTest, fixed_var_accepts_configured_active_power_unit) {
    auto ok = make_directive(DT::FixedVar, "fv");
    ok.fixed_var = make_fixed_var(10.0f, gs::DERUnit::PctMaxW);
    auto out = map_active_directives_to_sae_der_control(make_set({ok}), NOMINAL_V, NOMINAL_HZ);
    const auto& cv = out.der_control.reactive_power_support.constant_var;
    EXPECT_TRUE(cv.enable);
    EXPECT_EQ(cv.unit, sae::DERUnit::PercentageEVMaximumConfiguredActivePower);

    auto bad = make_directive(DT::FixedVar, "fv-bad");
    bad.fixed_var = make_fixed_var(10.0f, gs::DERUnit::PctWAvail);
    out = map_active_directives_to_sae_der_control(make_set({bad}), NOMINAL_V, NOMINAL_HZ);
    EXPECT_FALSE(out.der_control.reactive_power_support.constant_var.enable);
}

TEST(DerSaeRelayTest, directive_priority_is_forwarded) {
    const auto d = make_curve_directive(DT::VoltVar, gs::DERUnit::PctMaxVar, {pt(92.0f, 44.0f), pt(108.0f, -44.0f)},
                                        std::nullopt, "vv", 3);
    auto out = map_active_directives_to_sae_der_control(make_set({d}), NOMINAL_V, NOMINAL_HZ);
    ASSERT_TRUE(out.der_control.reactive_power_support.volt_var.priority.has_value());
    EXPECT_EQ(out.der_control.reactive_power_support.volt_var.priority.value(), 3u);

    const auto negative = make_curve_directive(DT::VoltVar, gs::DERUnit::PctMaxVar,
                                               {pt(92.0f, 44.0f), pt(108.0f, -44.0f)}, std::nullopt, "vv", -1);
    out = map_active_directives_to_sae_der_control(make_set({negative}), NOMINAL_V, NOMINAL_HZ);
    EXPECT_TRUE(out.der_control.reactive_power_support.volt_var.enable);
    EXPECT_FALSE(out.der_control.reactive_power_support.volt_var.priority.has_value());

    // Above the uint16 ceiling the priority is omitted rather than wrapped, so the element carries no
    // fabricated precedence.
    const auto too_large = make_curve_directive(DT::VoltVar, gs::DERUnit::PctMaxVar,
                                                {pt(92.0f, 44.0f), pt(108.0f, -44.0f)}, std::nullopt, "vv", 65536);
    out = map_active_directives_to_sae_der_control(make_set({too_large}), NOMINAL_V, NOMINAL_HZ);
    EXPECT_TRUE(out.der_control.reactive_power_support.volt_var.enable);
    EXPECT_FALSE(out.der_control.reactive_power_support.volt_var.priority.has_value());
}

TEST(DerSaeRelayTest, limit_max_discharge_copies_percentage_and_clamps) {
    auto d60 = make_directive(DT::LimitMaxDischarge, "lmd");
    d60.limit_max_discharge = gs::LimitMaxDischargeParams{};
    d60.limit_max_discharge->pct_max_discharge_power = 60.0f;
    auto out = map_active_directives_to_sae_der_control(make_set({d60}), NOMINAL_V, NOMINAL_HZ);
    EXPECT_TRUE(out.der_control.active_power_support.limit_max_discharge_power.enable);
    EXPECT_EQ(out.der_control.active_power_support.limit_max_discharge_power.percentage_value, 60u);

    auto d150 = make_directive(DT::LimitMaxDischarge, "lmd");
    d150.limit_max_discharge = gs::LimitMaxDischargeParams{};
    d150.limit_max_discharge->pct_max_discharge_power = 150.0f;
    out = map_active_directives_to_sae_der_control(make_set({d150}), NOMINAL_V, NOMINAL_HZ);
    EXPECT_EQ(out.der_control.active_power_support.limit_max_discharge_power.percentage_value, 100u);
}

TEST(DerSaeRelayTest, limit_max_discharge_without_percentage_is_rejected) {
    auto d = make_directive(DT::LimitMaxDischarge, "lmd");
    d.limit_max_discharge = gs::LimitMaxDischargeParams{};
    const auto out = map_active_directives_to_sae_der_control(make_set({d}), NOMINAL_V, NOMINAL_HZ);
    EXPECT_FALSE(out.der_control.active_power_support.limit_max_discharge_power.enable);
    EXPECT_EQ(out.der_control.active_power_support.limit_max_discharge_power.percentage_value, 100u);
    EXPECT_TRUE(out.unmapped.empty());
}

TEST(DerSaeRelayTest, freq_droop_deadbands_relative_to_nominal) {
    auto d = make_directive(DT::FreqDroop, "fd");
    d.freq_droop = make_freq_droop(50.2f, 49.8f, 5.0f, 4.0f, 1.5f);
    const auto out = map_active_directives_to_sae_der_control(make_set({d}), NOMINAL_V, NOMINAL_HZ);

    const auto& fd = out.der_control.active_power_support.frequency_droop;
    EXPECT_TRUE(fd.enable);
    ASSERT_TRUE(fd.over_frequency_droop.has_value());
    ASSERT_TRUE(fd.under_frequency_droop.has_value());
    EXPECT_NEAR(fd.over_frequency_droop->db, 0.2f, 1e-4f);
    EXPECT_FLOAT_EQ(fd.over_frequency_droop->droop_factor, 5.0f);
    EXPECT_EQ(fd.over_frequency_droop->power_reference, sae::PowerReference::MaximumActivePower);
    EXPECT_FLOAT_EQ(fd.over_frequency_droop->open_loop_response_time, 1.5f);
    EXPECT_NEAR(fd.under_frequency_droop->db, 0.2f, 1e-4f);
    EXPECT_FLOAT_EQ(fd.under_frequency_droop->droop_factor, 4.0f);
}

TEST(DerSaeRelayTest, freq_droop_deadbands_at_sixty_hertz) {
    auto d = make_directive(DT::FreqDroop, "fd");
    d.freq_droop = make_freq_droop(60.036f, 59.964f, 5.0f, 5.0f, 1.0f);
    const auto out = map_active_directives_to_sae_der_control(make_set({d}), 240.0f, 60.0f);

    const auto& fd = out.der_control.active_power_support.frequency_droop;
    EXPECT_TRUE(fd.enable);
    ASSERT_TRUE(fd.over_frequency_droop.has_value());
    ASSERT_TRUE(fd.under_frequency_droop.has_value());
    EXPECT_NEAR(fd.over_frequency_droop->db, 0.036f, 1e-4f);
    EXPECT_NEAR(fd.under_frequency_droop->db, 0.036f, 1e-4f);
}

TEST(DerSaeRelayTest, freq_droop_with_misordered_thresholds_is_rejected) {
    auto over_below = make_directive(DT::FreqDroop, "fd-over");
    over_below.freq_droop = make_freq_droop(49.8f, 49.5f, 5.0f, 5.0f, 1.0f);
    auto out = map_active_directives_to_sae_der_control(make_set({over_below}), NOMINAL_V, NOMINAL_HZ);
    EXPECT_FALSE(out.der_control.active_power_support.frequency_droop.enable);
    // The default carries an inert over-frequency branch, so a rejection leaves it untouched rather than absent.
    ASSERT_TRUE(out.der_control.active_power_support.frequency_droop.over_frequency_droop.has_value());
    EXPECT_FLOAT_EQ(out.der_control.active_power_support.frequency_droop.over_frequency_droop->droop_factor, 0.0f);
    EXPECT_FALSE(out.der_control.active_power_support.frequency_droop.under_frequency_droop.has_value());

    auto under_above = make_directive(DT::FreqDroop, "fd-under");
    under_above.freq_droop = make_freq_droop(50.5f, 50.2f, 5.0f, 5.0f, 1.0f);
    out = map_active_directives_to_sae_der_control(make_set({under_above}), NOMINAL_V, NOMINAL_HZ);
    EXPECT_FALSE(out.der_control.active_power_support.frequency_droop.enable);
}

TEST(DerSaeRelayTest, freq_droop_with_zero_nominal_frequency_is_rejected) {
    auto d = make_directive(DT::FreqDroop, "fd");
    d.freq_droop = make_freq_droop(50.2f, 49.8f, 5.0f, 4.0f, 1.5f);
    const auto out = map_active_directives_to_sae_der_control(make_set({d}), NOMINAL_V, 0.0f);
    EXPECT_FALSE(out.der_control.active_power_support.frequency_droop.enable);
}

TEST(DerSaeRelayTest, enter_service_copies_volts_through) {
    auto d = make_directive(DT::EnterService, "es");
    auto es = make_enter_service(253.0f, 207.0f, 50.2f, 49.8f);
    es.random_delay = 30.0f;
    d.enter_service = es;
    const auto out = map_active_directives_to_sae_der_control(make_set({d}), NOMINAL_V, NOMINAL_HZ);

    const auto& out_es = out.der_control.enter_service;
    EXPECT_TRUE(out_es.permit_service);
    // AMD1 Table 1 states EnterServiceVoltageHigh/Low in volts, so the grid_support volts pass through.
    EXPECT_FLOAT_EQ(out_es.enter_service_voltage_high, 253.0f);
    EXPECT_FLOAT_EQ(out_es.enter_service_voltage_low, 207.0f);
    EXPECT_FLOAT_EQ(out_es.enter_service_frequency_high, 50.2f);
    EXPECT_FLOAT_EQ(out_es.enter_service_frequency_low, 49.8f);
    EXPECT_FALSE(out_es.enter_service_delay.has_value());
    ASSERT_TRUE(out_es.enter_service_randomized_delay.has_value());
    EXPECT_FLOAT_EQ(out_es.enter_service_randomized_delay.value(), 30.0f);
    EXPECT_FALSE(out_es.enter_service_ramp_time.has_value());
}

TEST(DerSaeRelayTest, enter_service_without_any_delay_sets_randomized_delay_zero) {
    auto d = make_directive(DT::EnterService, "es");
    d.enter_service = make_enter_service(253.0f, 207.0f, 50.2f, 49.8f);
    const auto out = map_active_directives_to_sae_der_control(make_set({d}), NOMINAL_V, NOMINAL_HZ);

    const auto& out_es = out.der_control.enter_service;
    EXPECT_TRUE(out_es.permit_service);
    EXPECT_FALSE(out_es.enter_service_delay.has_value());
    ASSERT_TRUE(out_es.enter_service_randomized_delay.has_value());
    EXPECT_FLOAT_EQ(out_es.enter_service_randomized_delay.value(), 0.0f);
}

TEST(DerSaeRelayTest, enter_service_with_delay_and_ramp_sets_both) {
    auto d = make_directive(DT::EnterService, "es");
    auto es = make_enter_service(253.0f, 207.0f, 50.2f, 49.8f);
    es.delay = 300.0f;
    es.ramp_rate = 10.0f;
    d.enter_service = es;
    const auto out = map_active_directives_to_sae_der_control(make_set({d}), NOMINAL_V, NOMINAL_HZ);

    const auto& out_es = out.der_control.enter_service;
    EXPECT_TRUE(out_es.permit_service);
    ASSERT_TRUE(out_es.enter_service_delay.has_value());
    EXPECT_FLOAT_EQ(out_es.enter_service_delay.value(), 300.0f);
    ASSERT_TRUE(out_es.enter_service_ramp_time.has_value());
    EXPECT_FLOAT_EQ(out_es.enter_service_ramp_time.value(), 10.0f);
}

TEST(DerSaeRelayTest, enter_service_with_delay_but_no_ramp_is_rejected) {
    auto d = make_directive(DT::EnterService, "es");
    auto es = make_enter_service(253.0f, 207.0f, 50.2f, 49.8f);
    es.delay = 300.0f;
    d.enter_service = es;
    const auto out = map_active_directives_to_sae_der_control(make_set({d}), NOMINAL_V, NOMINAL_HZ);

    expect_default_baseline(out.der_control);
    EXPECT_FALSE(out.der_control.enter_service.permit_service);
}

TEST(DerSaeRelayTest, response_curve_with_eleven_points_is_rejected) {
    std::vector<gs::DERCurvePoint> points;
    for (int i = 0; i < 11; ++i) {
        points.push_back(pt(90.0f + static_cast<float>(i), static_cast<float>(i)));
    }
    const auto d = make_curve_directive(DT::VoltWatt, gs::DERUnit::PctMaxW, points);
    const auto out = map_active_directives_to_sae_der_control(make_set({d}), NOMINAL_V, NOMINAL_HZ);

    expect_default_baseline(out.der_control);
    EXPECT_FALSE(out.der_control.active_power_support.volt_watt.enable);
}

TEST(DerSaeRelayTest, freq_watt_lands_in_unmapped) {
    const auto d = make_curve_directive(DT::FreqWatt, gs::DERUnit::PctMaxW, {pt(50.2f, 100.0f), pt(51.0f, 0.0f)});
    const auto out = map_active_directives_to_sae_der_control(make_set({d}), NOMINAL_V, NOMINAL_HZ);

    ASSERT_EQ(out.unmapped.size(), 1u);
    EXPECT_EQ(out.unmapped[0], DT::FreqWatt);
    EXPECT_TRUE(out.shadowed_ids.empty());
    expect_default_baseline(out.der_control);
}

TEST(DerSaeRelayTest, removed_directive_yields_default_again) {
    const auto d =
        make_curve_directive(DT::HVMustTrip, gs::DERUnit::Not_Applicable, {pt(120.0f, 0.16f), pt(115.0f, 1.0f)});
    const auto first = map_active_directives_to_sae_der_control(make_set({d}), NOMINAL_V, NOMINAL_HZ);
    ASSERT_TRUE(first.der_control.voltage_trip.over_voltage_must_trip_curve.enable);

    const auto second = map_active_directives_to_sae_der_control(make_set({}), NOMINAL_V, NOMINAL_HZ);
    expect_default_baseline(second.der_control);
}

TEST(DerSaeRelayTest, relay_input_equal_for_same_set_and_nominals) {
    auto a = make_directive(DT::VoltVar, "a");
    a.received_at = "2026-07-10T00:00:01Z";
    auto b = make_directive(DT::HVMustTrip, "b");
    b.received_at = "2026-07-10T00:00:02Z";

    const auto forward = module::sae_relay_input(make_set({a, b}), NOMINAL_V, NOMINAL_HZ);
    const auto reversed = module::sae_relay_input(make_set({b, a}), NOMINAL_V, NOMINAL_HZ);

    EXPECT_TRUE(forward == reversed);
    ASSERT_EQ(forward.directives.size(), 2u);
    EXPECT_EQ(forward.directives[0].first, "a");
    EXPECT_EQ(forward.directives[1].first, "b");
    EXPECT_FLOAT_EQ(forward.nominal_voltage_v, NOMINAL_V);
    EXPECT_FLOAT_EQ(forward.nominal_frequency_hz, NOMINAL_HZ);
}

TEST(DerSaeRelayTest, relay_input_differs_on_received_at) {
    auto a = make_directive(DT::VoltVar, "a");
    a.received_at = "2026-07-10T00:00:01Z";
    auto a_again = a;
    a_again.received_at = "2026-07-10T00:00:05Z";

    EXPECT_FALSE(module::sae_relay_input(make_set({a}), NOMINAL_V, NOMINAL_HZ) ==
                 module::sae_relay_input(make_set({a_again}), NOMINAL_V, NOMINAL_HZ));
}

TEST(DerSaeRelayTest, relay_input_differs_on_nominal) {
    const auto set = make_set({make_directive(DT::VoltVar, "a")});

    const auto base = module::sae_relay_input(set, NOMINAL_V, NOMINAL_HZ);

    EXPECT_TRUE(base == module::sae_relay_input(set, NOMINAL_V, NOMINAL_HZ));
    EXPECT_FALSE(base == module::sae_relay_input(set, 240.0f, NOMINAL_HZ));
    EXPECT_FALSE(base == module::sae_relay_input(set, NOMINAL_V, 60.0f));
}

TEST(DerSaeRelayTest, inert_functions_lists_everything_for_the_default_control) {
    const auto inert = module::inert_sae_der_functions(iso15118::d20::get_default_sae_der_control(NOMINAL_V));

    EXPECT_NE(std::find(inert.begin(), inert.end(), "VoltVar"), inert.end());
    EXPECT_NE(std::find(inert.begin(), inert.end(), "EnterService"), inert.end());
    EXPECT_NE(std::find(inert.begin(), inert.end(), "OverVoltageMustTrip"), inert.end());
    EXPECT_NE(std::find(inert.begin(), inert.end(), "LimitMaxDischargePower"), inert.end());
}

TEST(DerSaeRelayTest, inert_functions_drops_an_enabled_volt_var) {
    auto control = iso15118::d20::get_default_sae_der_control(NOMINAL_V);
    control.reactive_power_support.volt_var.enable = true;

    const auto inert = module::inert_sae_der_functions(control);

    EXPECT_EQ(std::find(inert.begin(), inert.end(), "VoltVar"), inert.end());
    EXPECT_NE(std::find(inert.begin(), inert.end(), "WattVar"), inert.end());
}

TEST(DerSaeRelayTest, zero_nominal_voltage_rejects_every_directive) {
    // The default is derived from the nominal, so without one there is no baseline to map onto.
    const auto vv = make_curve_directive(DT::VoltVar, gs::DERUnit::PctMaxVar, {pt(92.0f, 44.0f), pt(108.0f, -44.0f)},
                                         std::nullopt, "vv");
    auto fv = make_directive(DT::FixedVar, "fv");
    fv.fixed_var = make_fixed_var(25.0f, gs::DERUnit::PctMaxVar);

    const auto out = map_active_directives_to_sae_der_control(make_set({vv, fv}), 0.0f, NOMINAL_HZ);

    EXPECT_FALSE(out.der_control.reactive_power_support.volt_var.enable);
    EXPECT_FALSE(out.der_control.reactive_power_support.constant_var.enable);
    EXPECT_TRUE(out.shadowed_ids.empty());
    EXPECT_TRUE(out.unmapped.empty());

    const auto def = iso15118::d20::get_default_sae_der_control(0.0f);
    EXPECT_FLOAT_EQ(out.der_control.enter_service.enter_service_voltage_high,
                    def.enter_service.enter_service_voltage_high);
    EXPECT_FLOAT_EQ(out.der_control.enter_service.enter_service_voltage_high, 0.0f);
    EXPECT_FLOAT_EQ(out.der_control.enter_service.enter_service_voltage_low,
                    def.enter_service.enter_service_voltage_low);
}

TEST(DerSaeRelayTest, trip_curve_with_ten_points_is_accepted) {
    std::vector<gs::DERCurvePoint> points;
    for (int i = 1; i <= 10; ++i) {
        points.push_back(pt(100.0f + static_cast<float>(i), static_cast<float>(i)));
    }
    const auto d = make_curve_directive(DT::HVMustTrip, gs::DERUnit::Not_Applicable, points);
    const auto out = map_active_directives_to_sae_der_control(make_set({d}), NOMINAL_V, NOMINAL_HZ);

    const auto& curve = out.der_control.voltage_trip.over_voltage_must_trip_curve;
    EXPECT_TRUE(curve.enable);
    EXPECT_EQ(curve.curve_data_points.size(), 10u);
}

TEST(DerSaeRelayTest, fixed_var_with_not_applicable_unit_is_rejected) {
    auto d = make_directive(DT::FixedVar, "fv");
    d.fixed_var = make_fixed_var(25.0f, gs::DERUnit::Not_Applicable);
    const auto out = map_active_directives_to_sae_der_control(make_set({d}), NOMINAL_V, NOMINAL_HZ);

    expect_default_baseline(out.der_control);
    EXPECT_FALSE(out.der_control.reactive_power_support.constant_var.enable);
}

TEST(DerSaeRelayTest, negative_priority_wins_contention_without_a_priority) {
    // The raw int decides the slot, so -1 outranks 0 and reaches the EV carrying no precedence at all.
    const auto negative = make_curve_directive(DT::VoltVar, gs::DERUnit::PctMaxVar,
                                               {pt(95.0f, 20.0f), pt(105.0f, -20.0f)}, std::nullopt, "vvneg", -1);
    const auto zero = make_curve_directive(DT::VoltVar, gs::DERUnit::PctMaxVar, {pt(90.0f, 10.0f), pt(110.0f, -10.0f)},
                                           std::nullopt, "vv0", 0);
    const auto out = map_active_directives_to_sae_der_control(make_set({zero, negative}), NOMINAL_V, NOMINAL_HZ);

    const auto& vv = out.der_control.reactive_power_support.volt_var;
    EXPECT_TRUE(vv.enable);
    ASSERT_EQ(vv.curve_data_points.size(), 2u);
    EXPECT_FLOAT_EQ(vv.curve_data_points[0].x_value, 95.0f);
    EXPECT_FLOAT_EQ(vv.curve_data_points[1].x_value, 105.0f);
    EXPECT_FALSE(vv.priority.has_value());
    ASSERT_EQ(out.shadowed_ids.size(), 1u);
    EXPECT_EQ(out.shadowed_ids[0], "vv0");
}
