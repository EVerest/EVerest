// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include "derating/derating_curve.hpp"

#include <gtest/gtest.h>

#include <limits>

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

TEST(DeratingCurveTest, parse_empty_object_returns_empty_map) {
    const auto curves = parse_derating_curves_json("{}");
    EXPECT_TRUE(curves.empty());
}

TEST(DeratingCurveTest, parse_rejects_non_object_json) {
    EXPECT_THROW(parse_derating_curves_json(R"([])"), std::invalid_argument);
}

TEST(DeratingCurveTest, parse_rejects_empty_curve_array) {
    EXPECT_THROW(parse_derating_curves_json(R"({"meter1.internal":[]})"), std::invalid_argument);
}

TEST(DeratingCurveTest, parse_rejects_missing_point_fields) {
    EXPECT_THROW(parse_derating_curves_json(R"({"meter1.internal":[{"temp_C":20}]})"), std::invalid_argument);
}

TEST(DeratingCurveTest, parse_rejects_duplicate_temp_c) {
    EXPECT_THROW(parse_derating_curves_json(
                     R"({"meter1.internal":[{"temp_C":20,"max_current_A":32},{"temp_C":20,"max_current_A":16}]})"),
                 std::invalid_argument);
}

TEST(DeratingCurveTest, validate_provider_curves_rejects_empty_curve_map) {
    EXPECT_THROW(validate_curves_for_providers({}, {"meter1"}), std::invalid_argument);
}

TEST(DeratingCurveTest, compute_empty_readings_returns_no_limit) {
    const auto curves = make_curves();
    const auto result = compute_effective_limit_A(curves, {}, 6.0);
    EXPECT_FALSE(result.effective_limit_A.has_value());
}

TEST(DeratingCurveTest, ignored_reading_excluded_from_minimum) {
    const auto curves = make_curves();
    const std::vector<SensorReadingInput> readings = {
        {"sensor_a", "internal", 40.0},
    };

    const auto result = compute_effective_limit_A(curves, readings, 0.0);
    ASSERT_TRUE(result.effective_limit_A.has_value());
    EXPECT_DOUBLE_EQ(result.effective_limit_A.value(), 24.0);
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

TEST(DeratingCurveTest, parse_ignore_list) {
    const auto ignore_list = parse_temperature_provider_ignore_list(" meter1.internal , meter2.body ");
    ASSERT_EQ(ignore_list.size(), 2U);
    EXPECT_EQ(ignore_list.count("meter1.internal"), 1U);
    EXPECT_EQ(ignore_list.count("meter2.body"), 1U);
}

TEST(DeratingCurveTest, parse_ignore_list_rejects_invalid_key) {
    EXPECT_THROW(parse_temperature_provider_ignore_list("meter1"), std::invalid_argument);
}

TEST(DeratingCurveTest, validate_ignore_list_rejects_curve_for_ignored_reading) {
    const auto curves = parse_derating_curves_json(
        R"({"meter1.internal":[{"temp_C":20,"max_current_A":32}],"meter1.body":[{"temp_C":20,"max_current_A":16}]})");
    const auto ignore_list = parse_temperature_provider_ignore_list("meter1.body");
    EXPECT_THROW(validate_ignore_list_vs_curves(curves, ignore_list), std::invalid_argument);
}

TEST(DeratingCurveTest, non_finite_temperature_uses_fallback) {
    const auto curves = make_curves();
    const std::vector<SensorReadingInput> readings = {
        {"sensor_a", "internal", std::numeric_limits<double>::infinity()},
    };

    const auto result = compute_effective_limit_A(curves, readings, 6.0);
    ASSERT_TRUE(result.effective_limit_A.has_value());
    EXPECT_DOUBLE_EQ(result.effective_limit_A.value(), 6.0);
}

TEST(DeratingCurveTest, is_temperature_reading_ignored) {
    const auto ignore_list = parse_temperature_provider_ignore_list("meter1.body");
    EXPECT_TRUE(is_temperature_reading_ignored(ignore_list, "meter1", "body"));
    EXPECT_FALSE(is_temperature_reading_ignored(ignore_list, "meter1", "internal"));
}

} // namespace
} // namespace ac_temperature_derating
