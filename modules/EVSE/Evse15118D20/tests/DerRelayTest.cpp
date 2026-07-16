// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include <gtest/gtest.h>

#include <optional>
#include <variant>

#include <iso15118/d20/der_functions.hpp>

#include "der_relay.hpp"

namespace {

using module::map_active_directives_to_der_functions;
using DT = types::grid_support::DirectiveType;
using iso15118::iec::DERControlName;
using iso15118::iec::PowerFactorExcitation;

constexpr float VOLT_BASE = 230.0f;
constexpr float WATT_BASE = 11000.0f;
constexpr float VAR_BASE = 5000.0f;

types::grid_support::Directive make_curve_directive(DT type, types::grid_support::DERUnit y_unit,
                                                    std::vector<types::grid_support::DERCurvePoint> points,
                                                    std::optional<float> response_time = std::nullopt) {
    types::grid_support::Directive d{};
    d.id = "d1";
    d.directive_type = type;
    d.priority = 0;
    d.is_default = false;
    d.source = "test";
    d.received_at = "2026-07-10T00:00:00Z";
    types::grid_support::DERCurve curve{};
    curve.curve_data = std::move(points);
    curve.y_unit = y_unit;
    curve.response_time = response_time;
    d.curve = std::move(curve);
    return d;
}

types::grid_support::ActiveDirectiveSet make_set(std::vector<types::grid_support::Directive> directives) {
    types::grid_support::ActiveDirectiveSet set{};
    set.evse_id = 1;
    set.directives = std::move(directives);
    return set;
}

const iso15118::iec::DERCurve& as_curve(const iso15118::iec::DERControlFunction& fn) {
    return std::get<iso15118::iec::DERCurve>(fn);
}

} // namespace

TEST(DerRelayTest, empty_directive_set_yields_empty_map) {
    const auto out = map_active_directives_to_der_functions(make_set({}), VOLT_BASE, WATT_BASE, VAR_BASE);
    EXPECT_TRUE(out.empty());
}

TEST(DerRelayTest, voltvar_maps_to_voltvarmode_with_var_denormalized) {
    const auto d = make_curve_directive(DT::VoltVar, types::grid_support::DERUnit::PctMaxVar,
                                        {{50.0f, 40.0f}}); // x=50% of V, y=40% of var
    const auto out = map_active_directives_to_der_functions(make_set({d}), VOLT_BASE, WATT_BASE, VAR_BASE);

    ASSERT_EQ(out.size(), 1u);
    ASSERT_EQ(out.count(DERControlName::VoltVarMode), 1u);
    const auto& curve = as_curve(out.at(DERControlName::VoltVarMode));
    EXPECT_EQ(curve.x_unit, iso15118::iec::CurveDataPointsUnit::V);
    EXPECT_EQ(curve.y_unit, iso15118::iec::CurveDataPointsUnit::var);
    ASSERT_EQ(curve.curve_data_points.size(), 1u);
    EXPECT_FLOAT_EQ(curve.curve_data_points[0].x_value, 0.5f * VOLT_BASE);
    EXPECT_FLOAT_EQ(curve.curve_data_points[0].y_value.setpoint_value, 0.4f * VAR_BASE);
    EXPECT_FALSE(curve.curve_data_points[0].y_value.excitation.has_value());
}

TEST(DerRelayTest, wattvar_maps_to_wattvarmode_x_in_watts) {
    const auto d = make_curve_directive(DT::WattVar, types::grid_support::DERUnit::PctMaxVar, {{25.0f, 10.0f}});
    const auto out = map_active_directives_to_der_functions(make_set({d}), VOLT_BASE, WATT_BASE, VAR_BASE);

    ASSERT_EQ(out.count(DERControlName::WattVarMode), 1u);
    const auto& curve = as_curve(out.at(DERControlName::WattVarMode));
    EXPECT_EQ(curve.x_unit, iso15118::iec::CurveDataPointsUnit::W);
    ASSERT_EQ(curve.curve_data_points.size(), 1u);
    EXPECT_FLOAT_EQ(curve.curve_data_points[0].x_value, 0.25f * WATT_BASE);
    EXPECT_FLOAT_EQ(curve.curve_data_points[0].y_value.setpoint_value, 0.1f * VAR_BASE);
}

TEST(DerRelayTest, wattpf_maps_to_wattcosphimode_setpoint_and_excitation) {
    const auto d = make_curve_directive(DT::WattPF, types::grid_support::DERUnit::Not_Applicable,
                                        {{20.0f, 0.95f}, {80.0f, -0.90f}});
    const auto out = map_active_directives_to_der_functions(make_set({d}), VOLT_BASE, WATT_BASE, VAR_BASE);

    ASSERT_EQ(out.count(DERControlName::WattCosPhiMode), 1u);
    const auto& curve = as_curve(out.at(DERControlName::WattCosPhiMode));
    EXPECT_EQ(curve.x_unit, iso15118::iec::CurveDataPointsUnit::W);
    ASSERT_EQ(curve.curve_data_points.size(), 2u);

    EXPECT_FLOAT_EQ(curve.curve_data_points[0].x_value, 0.2f * WATT_BASE);
    EXPECT_FLOAT_EQ(curve.curve_data_points[0].y_value.setpoint_value, 0.95f);
    ASSERT_TRUE(curve.curve_data_points[0].y_value.excitation.has_value());
    EXPECT_EQ(curve.curve_data_points[0].y_value.excitation.value(), PowerFactorExcitation::OverExcited);

    EXPECT_FLOAT_EQ(curve.curve_data_points[1].y_value.setpoint_value, 0.90f);
    ASSERT_TRUE(curve.curve_data_points[1].y_value.excitation.has_value());
    EXPECT_EQ(curve.curve_data_points[1].y_value.excitation.value(), PowerFactorExcitation::UnderExcited);
}

TEST(DerRelayTest, var_axis_curve_skipped_when_var_base_absent) {
    const auto volt_var = make_curve_directive(DT::VoltVar, types::grid_support::DERUnit::PctMaxVar, {{50.0f, 40.0f}});
    const auto watt_var =
        make_curve_directive(DT::WattVar, types::grid_support::DERUnit::PctVarAvail, {{25.0f, 10.0f}});
    const auto watt_pf =
        make_curve_directive(DT::WattPF, types::grid_support::DERUnit::Not_Applicable, {{20.0f, 0.95f}});

    const auto out = map_active_directives_to_der_functions(make_set({volt_var, watt_var, watt_pf}), VOLT_BASE,
                                                            WATT_BASE, std::nullopt);

    EXPECT_EQ(out.count(DERControlName::VoltVarMode), 0u);
    EXPECT_EQ(out.count(DERControlName::WattVarMode), 0u);
    EXPECT_EQ(out.count(DERControlName::WattCosPhiMode), 1u);
}

TEST(DerRelayTest, zero_volt_base_skips_voltvar_curve) {
    const auto d = make_curve_directive(DT::VoltVar, types::grid_support::DERUnit::PctMaxVar, {{50.0f, 40.0f}});
    const auto out = map_active_directives_to_der_functions(make_set({d}), 0.0f, WATT_BASE, VAR_BASE);
    EXPECT_EQ(out.count(DERControlName::VoltVarMode), 0u);
    EXPECT_TRUE(out.empty());
}

TEST(DerRelayTest, zero_watt_base_skips_wattvar_curve) {
    const auto d = make_curve_directive(DT::WattVar, types::grid_support::DERUnit::PctMaxVar, {{25.0f, 10.0f}});
    const auto out = map_active_directives_to_der_functions(make_set({d}), VOLT_BASE, 0.0f, VAR_BASE);
    EXPECT_EQ(out.count(DERControlName::WattVarMode), 0u);
    EXPECT_TRUE(out.empty());
}

TEST(DerRelayTest, curve_over_ten_points_truncated_to_ten) {
    std::vector<types::grid_support::DERCurvePoint> points;
    for (int i = 0; i < 15; ++i) {
        points.push_back({static_cast<float>(i), 10.0f});
    }
    const auto d = make_curve_directive(DT::VoltVar, types::grid_support::DERUnit::PctMaxVar, std::move(points));
    const auto out = map_active_directives_to_der_functions(make_set({d}), VOLT_BASE, WATT_BASE, VAR_BASE);

    ASSERT_EQ(out.count(DERControlName::VoltVarMode), 1u);
    const auto& curve = as_curve(out.at(DERControlName::VoltVarMode));
    ASSERT_EQ(curve.curve_data_points.size(), 10u);
    // Order is preserved: first ten input points kept, later points dropped.
    EXPECT_FLOAT_EQ(curve.curve_data_points[0].x_value, 0.0f / 100.0f * VOLT_BASE);
    EXPECT_FLOAT_EQ(curve.curve_data_points[9].x_value, 9.0f / 100.0f * VOLT_BASE);
}

TEST(DerRelayTest, empty_curve_data_is_skipped) {
    const auto d = make_curve_directive(DT::VoltVar, types::grid_support::DERUnit::PctMaxVar, {});
    const auto out = map_active_directives_to_der_functions(make_set({d}), VOLT_BASE, WATT_BASE, VAR_BASE);
    EXPECT_EQ(out.count(DERControlName::VoltVarMode), 0u);
    EXPECT_TRUE(out.empty());
}

TEST(DerRelayTest, response_time_maps_to_step_response_time_constant) {
    const auto with_rt =
        make_curve_directive(DT::VoltVar, types::grid_support::DERUnit::PctMaxVar, {{50.0f, 40.0f}}, 2.5f);
    const auto out = map_active_directives_to_der_functions(make_set({with_rt}), VOLT_BASE, WATT_BASE, VAR_BASE);
    ASSERT_EQ(out.count(DERControlName::VoltVarMode), 1u);
    EXPECT_FLOAT_EQ(as_curve(out.at(DERControlName::VoltVarMode)).step_response_time_constant_reactive_power, 2.5f);

    const auto without_rt =
        make_curve_directive(DT::VoltVar, types::grid_support::DERUnit::PctMaxVar, {{50.0f, 40.0f}});
    const auto out2 = map_active_directives_to_der_functions(make_set({without_rt}), VOLT_BASE, WATT_BASE, VAR_BASE);
    ASSERT_EQ(out2.count(DERControlName::VoltVarMode), 1u);
    EXPECT_FLOAT_EQ(as_curve(out2.at(DERControlName::VoltVarMode)).step_response_time_constant_reactive_power, 0.0f);
}

TEST(DerRelayTest, duplicate_directive_type_last_wins) {
    const auto first = make_curve_directive(DT::VoltVar, types::grid_support::DERUnit::PctMaxVar, {{10.0f, 20.0f}});
    const auto second = make_curve_directive(DT::VoltVar, types::grid_support::DERUnit::PctMaxVar, {{60.0f, 80.0f}});
    const auto out = map_active_directives_to_der_functions(make_set({first, second}), VOLT_BASE, WATT_BASE, VAR_BASE);

    ASSERT_EQ(out.size(), 1u);
    ASSERT_EQ(out.count(DERControlName::VoltVarMode), 1u);
    const auto& curve = as_curve(out.at(DERControlName::VoltVarMode));
    ASSERT_EQ(curve.curve_data_points.size(), 1u);
    EXPECT_FLOAT_EQ(curve.curve_data_points[0].x_value, 0.6f * VOLT_BASE);
    EXPECT_FLOAT_EQ(curve.curve_data_points[0].y_value.setpoint_value, 0.8f * VAR_BASE);
}

TEST(DerRelayTest, wattpf_zero_pf_is_overexcited) {
    const auto d = make_curve_directive(DT::WattPF, types::grid_support::DERUnit::Not_Applicable, {{20.0f, 0.0f}});
    const auto out = map_active_directives_to_der_functions(make_set({d}), VOLT_BASE, WATT_BASE, VAR_BASE);

    ASSERT_EQ(out.count(DERControlName::WattCosPhiMode), 1u);
    const auto& curve = as_curve(out.at(DERControlName::WattCosPhiMode));
    ASSERT_EQ(curve.curve_data_points.size(), 1u);
    ASSERT_TRUE(curve.curve_data_points[0].y_value.excitation.has_value());
    EXPECT_EQ(curve.curve_data_points[0].y_value.excitation.value(), PowerFactorExcitation::OverExcited);
}

TEST(DerRelayTest, non_curve_directive_type_is_skipped) {
    types::grid_support::Directive d{};
    d.id = "es1";
    d.directive_type = DT::EnterService;
    d.priority = 0;
    d.is_default = false;
    d.source = "test";
    d.received_at = "2026-07-10T00:00:00Z";

    const auto out = map_active_directives_to_der_functions(make_set({d}), VOLT_BASE, WATT_BASE, VAR_BASE);
    EXPECT_TRUE(out.empty());
}

TEST(DerRelayTest, curve_family_directive_missing_payload_is_skipped) {
    types::grid_support::Directive d{};
    d.id = "vv1";
    d.directive_type = DT::VoltVar;
    d.priority = 0;
    d.is_default = false;
    d.source = "test";
    d.received_at = "2026-07-10T00:00:00Z";
    // no curve payload

    const auto out = map_active_directives_to_der_functions(make_set({d}), VOLT_BASE, WATT_BASE, VAR_BASE);
    EXPECT_TRUE(out.empty());
}

TEST(DerRelayTest, wattvar_y_axis_pct_w_avail_denormalized_against_watt_base) {
    const auto d = make_curve_directive(DT::WattVar, types::grid_support::DERUnit::PctWAvail, {{25.0f, 60.0f}});
    const auto out = map_active_directives_to_der_functions(make_set({d}), VOLT_BASE, WATT_BASE, VAR_BASE);

    ASSERT_EQ(out.count(DERControlName::WattVarMode), 1u);
    const auto& curve = as_curve(out.at(DERControlName::WattVarMode));
    ASSERT_EQ(curve.curve_data_points.size(), 1u);
    EXPECT_FLOAT_EQ(curve.curve_data_points[0].x_value, 0.25f * WATT_BASE);
    EXPECT_FLOAT_EQ(curve.curve_data_points[0].y_value.setpoint_value, 0.6f * WATT_BASE);
    EXPECT_FALSE(curve.curve_data_points[0].y_value.excitation.has_value());
}

TEST(DerRelayTest, zero_watt_base_skips_pct_w_avail_y_axis) {
    // x-axis rides the volt base (present), y-axis needs the watt base (missing) -> whole curve skipped.
    const auto d = make_curve_directive(DT::VoltVar, types::grid_support::DERUnit::PctWAvail, {{50.0f, 60.0f}});
    const auto out = map_active_directives_to_der_functions(make_set({d}), VOLT_BASE, 0.0f, VAR_BASE);
    EXPECT_EQ(out.count(DERControlName::VoltVarMode), 0u);
    EXPECT_TRUE(out.empty());
}

TEST(DerRelayTest, wattvar_y_axis_pct_effective_v_denormalized_against_volt_base) {
    const auto d = make_curve_directive(DT::WattVar, types::grid_support::DERUnit::PctEffectiveV, {{25.0f, 50.0f}});
    const auto out = map_active_directives_to_der_functions(make_set({d}), VOLT_BASE, WATT_BASE, VAR_BASE);

    ASSERT_EQ(out.count(DERControlName::WattVarMode), 1u);
    const auto& curve = as_curve(out.at(DERControlName::WattVarMode));
    ASSERT_EQ(curve.curve_data_points.size(), 1u);
    EXPECT_FLOAT_EQ(curve.curve_data_points[0].x_value, 0.25f * WATT_BASE);
    EXPECT_FLOAT_EQ(curve.curve_data_points[0].y_value.setpoint_value, 0.5f * VOLT_BASE);
    EXPECT_FALSE(curve.curve_data_points[0].y_value.excitation.has_value());
}

TEST(DerRelayTest, hysteresis_reactive_voltage_params_ignored_curve_still_maps) {
    auto d = make_curve_directive(DT::VoltVar, types::grid_support::DERUnit::PctMaxVar, {{50.0f, 40.0f}});
    types::grid_support::Hysteresis hy{};
    hy.hysteresis_high = 250.0f;
    d.curve->hysteresis = hy;
    types::grid_support::ReactivePowerParams rp{};
    rp.v_ref = 230.0f;
    d.curve->reactive_power_params = rp;
    types::grid_support::VoltageParams vp{};
    vp.hv_10min_mean_value = 253.0f;
    d.curve->voltage_params = vp;

    const auto out = map_active_directives_to_der_functions(make_set({d}), VOLT_BASE, WATT_BASE, VAR_BASE);

    // Unmodeled features are dropped, not the curve: VoltVarMode still maps with denormalized points.
    ASSERT_EQ(out.count(DERControlName::VoltVarMode), 1u);
    const auto& curve = as_curve(out.at(DERControlName::VoltVarMode));
    ASSERT_EQ(curve.curve_data_points.size(), 1u);
    EXPECT_FLOAT_EQ(curve.curve_data_points[0].x_value, 0.5f * VOLT_BASE);
    EXPECT_FLOAT_EQ(curve.curve_data_points[0].y_value.setpoint_value, 0.4f * VAR_BASE);
}
