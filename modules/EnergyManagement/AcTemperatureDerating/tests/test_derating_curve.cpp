// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include "derating/derating_curve.hpp"

#include <gtest/gtest.h>

namespace ac_temperature_derating {
namespace {

DeratingCurveMap make_curves() {
    DeratingCurveMap curves;
    curves["sensor_a.internal"] = {
        {20.0, 32.0},
        {60.0, 16.0},
        {80.0, 6.0},
    };
    curves["sensor_b.internal"] = {
        {25.0, 32.0},
        {75.0, 10.0},
    };
    return curves;
}

TEST(DeratingCurveTest, make_curve_key) {
    EXPECT_EQ(make_curve_key("yeti_driver_1", "Powermeter"), "yeti_driver_1.Powermeter");
}

TEST(DeratingCurveTest, parse_json) {
    const auto curves = parse_derating_curves_json(R"({"meter1.internal":[{"temp_C":20,"max_current_A":32}]})");
    ASSERT_EQ(curves.size(), 1U);
    ASSERT_NE(curves.find("meter1.internal"), curves.end());
    ASSERT_EQ(curves.at("meter1.internal").size(), 1U);
}

TEST(DeratingCurveTest, parse_rejects_key_without_dot) {
    EXPECT_THROW(parse_derating_curves_json(R"({"meter1":[{"temp_C":20,"max_current_A":32}]})"), std::invalid_argument);
}

TEST(DeratingCurveTest, validate_provider_curves) {
    const auto curves = parse_derating_curves_json(R"({"meter1.internal":[{"temp_C":20,"max_current_A":32}]})");
    EXPECT_NO_THROW(validate_curves_for_providers(curves, {"meter1"}));
    EXPECT_THROW(validate_curves_for_providers(curves, {"meter2"}), std::invalid_argument);
}

TEST(DeratingCurveTest, interpolate_between_points) {
    const DeratingCurve curve{{20.0, 32.0}, {60.0, 16.0}};
    EXPECT_DOUBLE_EQ(interpolate_max_current_A(curve, 20.0), 32.0);
    EXPECT_DOUBLE_EQ(interpolate_max_current_A(curve, 40.0), 24.0);
    EXPECT_DOUBLE_EQ(interpolate_max_current_A(curve, 70.0), 16.0);
}

TEST(DeratingCurveTest, compute_minimum_across_sensors) {
    const auto curves = make_curves();
    const std::vector<SensorReadingInput> readings = {
        {"sensor_a", "internal", 40.0},
        {"sensor_b", "internal", 50.0},
    };

    const auto result = compute_effective_limit_A(curves, readings, 0.0);
    ASSERT_TRUE(result.effective_limit_A.has_value());
    EXPECT_TRUE(result.missing_curve_keys.empty());
    // sensor_a@40C -> 24A (between 20C/32A and 60C/16A); sensor_b@50C -> 21A (between 25C/32A
    // and 75C/10A); the effective limit is the minimum across sensors.
    EXPECT_DOUBLE_EQ(result.effective_limit_A.value(), 21.0);
}

TEST(DeratingCurveTest, stale_sensor_uses_fallback) {
    const auto curves = make_curves();
    const std::vector<SensorReadingInput> readings = {
        {"sensor_a", "internal", std::nullopt},
    };

    const auto result = compute_effective_limit_A(curves, readings, 6.0);
    ASSERT_TRUE(result.effective_limit_A.has_value());
    EXPECT_DOUBLE_EQ(result.effective_limit_A.value(), 6.0);
}

TEST(DeratingCurveTest, missing_curve_uses_fallback) {
    const auto curves = make_curves();
    const std::vector<SensorReadingInput> readings = {
        {"sensor_a", "unknown", 40.0},
    };

    const auto result = compute_effective_limit_A(curves, readings, 6.0);
    ASSERT_TRUE(result.effective_limit_A.has_value());
    ASSERT_EQ(result.missing_curve_keys.size(), 1U);
    EXPECT_EQ(result.missing_curve_keys.front(), "sensor_a.unknown");
    EXPECT_DOUBLE_EQ(result.effective_limit_A.value(), 6.0);
}

TEST(DeratingCurveTest, missing_identification_uses_fallback) {
    const auto curves = make_curves();
    const std::vector<SensorReadingInput> readings = {
        {"sensor_a", std::nullopt, 40.0},
    };

    const auto result = compute_effective_limit_A(curves, readings, 6.0);
    ASSERT_TRUE(result.effective_limit_A.has_value());
    ASSERT_EQ(result.missing_identification_curve_keys.size(), 1U);
    EXPECT_EQ(result.missing_identification_curve_keys.front(), "sensor_a");
    EXPECT_DOUBLE_EQ(result.effective_limit_A.value(), 6.0);
}

TEST(DeratingCurveTest, flat_curve_is_no_derating) {
    const DeratingCurve curve{{-20.0, 32.0}, {25.0, 32.0}, {80.0, 32.0}};
    EXPECT_DOUBLE_EQ(interpolate_max_current_A(curve, -20.0), 32.0);
    EXPECT_DOUBLE_EQ(interpolate_max_current_A(curve, 25.0), 32.0);
    EXPECT_DOUBLE_EQ(interpolate_max_current_A(curve, 50.0), 32.0);
    EXPECT_DOUBLE_EQ(interpolate_max_current_A(curve, 80.0), 32.0);
}

TEST(DeratingCurveTest, find_curve_by_key) {
    const auto curves = parse_derating_curves_json(R"({"meter1.internal":[{"temp_C":30,"max_current_A":20}]})");
    const DeratingCurve* curve = find_derating_curve(curves, "meter1.internal");
    ASSERT_NE(curve, nullptr);
    EXPECT_DOUBLE_EQ(interpolate_max_current_A(*curve, 30.0), 20.0);
}

} // namespace
} // namespace ac_temperature_derating
