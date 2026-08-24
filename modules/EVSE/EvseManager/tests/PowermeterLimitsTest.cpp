// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include <gtest/gtest.h>

#include <optional>

#include <generated/types/power_supply_DC.hpp>
#include <generated/types/powermeter.hpp>

#include "powermeter_limits.hpp"

namespace {

/// \brief Power supply capabilities with only the mandatory fields populated.
///        Charging (export) direction: min 5 A, max 200 A. Discharge (import) side left unset.
types::power_supply_DC::Capabilities make_psu_caps() {
    types::power_supply_DC::Capabilities caps{};
    caps.bidirectional = true;
    caps.current_regulation_tolerance_A = 0.5f;
    caps.peak_current_ripple_A = 0.5f;
    caps.max_export_voltage_V = 900.0f;
    caps.min_export_voltage_V = 150.0f;
    caps.max_export_current_A = 200.0f;
    caps.min_export_current_A = 5.0f;
    caps.max_export_power_W = 150000.0f;
    return caps;
}

types::powermeter::Capabilities make_meter_caps(std::optional<float> min_import_current_A,
                                                std::optional<float> min_export_current_A) {
    types::powermeter::Capabilities meter{};
    meter.min_import_current_A = min_import_current_A;
    meter.min_export_current_A = min_export_current_A;
    return meter;
}

} // namespace

TEST(PowermeterLimitsTest, no_meter_capabilities_leaves_caps_unchanged) {
    const auto caps = make_psu_caps();

    const auto result = module::apply_powermeter_limits(caps, std::nullopt);

    EXPECT_EQ(result.min_export_current_A, caps.min_export_current_A);
    EXPECT_FALSE(result.nominal_min_export_current_A.has_value());
    EXPECT_FALSE(result.min_import_current_A.has_value());
    EXPECT_FALSE(result.nominal_min_import_current_A.has_value());
}

TEST(PowermeterLimitsTest, meter_without_minimums_leaves_caps_unchanged) {
    auto caps = make_psu_caps();
    caps.nominal_min_export_current_A = 6.0f;
    caps.min_import_current_A = 4.0f;
    caps.nominal_min_import_current_A = 4.5f;

    const auto result = module::apply_powermeter_limits(caps, make_meter_caps(std::nullopt, std::nullopt));

    EXPECT_FLOAT_EQ(result.min_export_current_A, 5.0f);
    ASSERT_TRUE(result.nominal_min_export_current_A.has_value());
    EXPECT_FLOAT_EQ(result.nominal_min_export_current_A.value(), 6.0f);
    ASSERT_TRUE(result.min_import_current_A.has_value());
    EXPECT_FLOAT_EQ(result.min_import_current_A.value(), 4.0f);
    ASSERT_TRUE(result.nominal_min_import_current_A.has_value());
    EXPECT_FLOAT_EQ(result.nominal_min_import_current_A.value(), 4.5f);
}

TEST(PowermeterLimitsTest, meter_min_import_raises_charging_minimums) {
    // Convention crossing: meter import (charging) limits the PSU export (charging) minimums.
    auto caps = make_psu_caps();
    caps.nominal_min_export_current_A = 6.0f;

    const auto result = module::apply_powermeter_limits(caps, make_meter_caps(10.0f, std::nullopt));

    EXPECT_FLOAT_EQ(result.min_export_current_A, 10.0f);
    ASSERT_TRUE(result.nominal_min_export_current_A.has_value());
    EXPECT_FLOAT_EQ(result.nominal_min_export_current_A.value(), 10.0f);
}

TEST(PowermeterLimitsTest, meter_min_import_does_not_fabricate_nominal_min_export) {
    // A nominal minimum the PSU never reported must stay unset so consumers fall back to the
    // (raised and clamped) regular minimum instead of an unclamped fabricated value.
    const auto caps = make_psu_caps();

    const auto result = module::apply_powermeter_limits(caps, make_meter_caps(10.0f, std::nullopt));

    EXPECT_FLOAT_EQ(result.min_export_current_A, 10.0f);
    EXPECT_FALSE(result.nominal_min_export_current_A.has_value());
}

TEST(PowermeterLimitsTest, meter_min_import_below_psu_min_keeps_psu_values) {
    auto caps = make_psu_caps();
    caps.nominal_min_export_current_A = 6.0f;

    const auto result = module::apply_powermeter_limits(caps, make_meter_caps(2.0f, std::nullopt));

    EXPECT_FLOAT_EQ(result.min_export_current_A, 5.0f);
    ASSERT_TRUE(result.nominal_min_export_current_A.has_value());
    EXPECT_FLOAT_EQ(result.nominal_min_export_current_A.value(), 6.0f);
}

TEST(PowermeterLimitsTest, meter_min_import_above_max_export_is_clamped_to_max) {
    auto caps = make_psu_caps();
    caps.max_export_current_A = 100.0f;
    caps.nominal_max_export_current_A = 90.0f;
    caps.nominal_min_export_current_A = 6.0f;

    const auto result = module::apply_powermeter_limits(caps, make_meter_caps(150.0f, std::nullopt));

    EXPECT_FLOAT_EQ(result.min_export_current_A, 100.0f);
    ASSERT_TRUE(result.nominal_min_export_current_A.has_value());
    EXPECT_FLOAT_EQ(result.nominal_min_export_current_A.value(), 90.0f);
}

TEST(PowermeterLimitsTest, nominal_min_export_not_clamped_when_nominal_max_unset) {
    // A PSU-provided nominal minimum is raised but not clamped when the PSU gave no nominal maximum.
    auto caps = make_psu_caps();
    caps.max_export_current_A = 100.0f;
    caps.nominal_min_export_current_A = 6.0f;

    const auto result = module::apply_powermeter_limits(caps, make_meter_caps(150.0f, std::nullopt));

    EXPECT_FLOAT_EQ(result.min_export_current_A, 100.0f);
    ASSERT_TRUE(result.nominal_min_export_current_A.has_value());
    EXPECT_FLOAT_EQ(result.nominal_min_export_current_A.value(), 150.0f);
}

TEST(PowermeterLimitsTest, meter_min_export_raises_discharge_minimums) {
    // Convention crossing: meter export (discharge) limits the PSU import (discharge) minimums.
    auto caps = make_psu_caps();
    caps.max_import_current_A = 200.0f;
    caps.nominal_min_import_current_A = 8.0f;

    const auto result = module::apply_powermeter_limits(caps, make_meter_caps(std::nullopt, 12.0f));

    ASSERT_TRUE(result.min_import_current_A.has_value());
    EXPECT_FLOAT_EQ(result.min_import_current_A.value(), 12.0f);
    ASSERT_TRUE(result.nominal_min_import_current_A.has_value());
    EXPECT_FLOAT_EQ(result.nominal_min_import_current_A.value(), 12.0f);
}

TEST(PowermeterLimitsTest, meter_min_export_below_psu_min_keeps_psu_values) {
    auto caps = make_psu_caps();
    caps.max_import_current_A = 200.0f;
    caps.min_import_current_A = 20.0f;
    caps.nominal_min_import_current_A = 18.0f;

    const auto result = module::apply_powermeter_limits(caps, make_meter_caps(std::nullopt, 12.0f));

    ASSERT_TRUE(result.min_import_current_A.has_value());
    EXPECT_FLOAT_EQ(result.min_import_current_A.value(), 20.0f);
    ASSERT_TRUE(result.nominal_min_import_current_A.has_value());
    EXPECT_FLOAT_EQ(result.nominal_min_import_current_A.value(), 18.0f);
}

TEST(PowermeterLimitsTest, meter_min_export_above_max_import_is_clamped_to_max) {
    auto caps = make_psu_caps();
    caps.max_import_current_A = 50.0f;
    caps.nominal_min_import_current_A = 10.0f;
    caps.nominal_max_import_current_A = 45.0f;

    const auto result = module::apply_powermeter_limits(caps, make_meter_caps(std::nullopt, 80.0f));

    ASSERT_TRUE(result.min_import_current_A.has_value());
    EXPECT_FLOAT_EQ(result.min_import_current_A.value(), 50.0f);
    ASSERT_TRUE(result.nominal_min_import_current_A.has_value());
    EXPECT_FLOAT_EQ(result.nominal_min_import_current_A.value(), 45.0f);
}

TEST(PowermeterLimitsTest, discharge_minimum_not_clamped_when_max_import_unset) {
    const auto caps = make_psu_caps();
    ASSERT_FALSE(caps.max_import_current_A.has_value());

    const auto result = module::apply_powermeter_limits(caps, make_meter_caps(std::nullopt, 80.0f));

    ASSERT_TRUE(result.min_import_current_A.has_value());
    EXPECT_FLOAT_EQ(result.min_import_current_A.value(), 80.0f);
    EXPECT_FALSE(result.nominal_min_import_current_A.has_value());
}

TEST(PowermeterLimitsTest, zero_max_export_still_clamps_minimum) {
    auto caps = make_psu_caps();
    caps.max_export_current_A = 0.0f;

    const auto result = module::apply_powermeter_limits(caps, make_meter_caps(12.0f, std::nullopt));

    EXPECT_FLOAT_EQ(result.min_export_current_A, 0.0f);
}

TEST(PowermeterLimitsTest, only_minimum_fields_are_modified) {
    // Callers rely on the merge touching nothing but the (nominal) minimum currents, e.g. the
    // energy limits path uses the merged view for its maximum limit calculations as well.
    auto caps = make_psu_caps();
    caps.max_import_voltage_V = 900.0f;
    caps.min_import_voltage_V = 150.0f;
    caps.max_import_current_A = 200.0f;
    caps.min_import_current_A = 4.0f;
    caps.max_import_power_W = 150000.0f;
    caps.nominal_max_export_current_A = 190.0f;
    caps.nominal_max_import_current_A = 190.0f;
    caps.conversion_efficiency_import = 0.95f;
    caps.conversion_efficiency_export = 0.95f;

    auto result = module::apply_powermeter_limits(caps, make_meter_caps(10.0f, 12.0f));

    result.min_export_current_A = caps.min_export_current_A;
    result.nominal_min_export_current_A = caps.nominal_min_export_current_A;
    result.min_import_current_A = caps.min_import_current_A;
    result.nominal_min_import_current_A = caps.nominal_min_import_current_A;
    EXPECT_EQ(result, caps);
}

TEST(PowermeterLimitsTest, both_directions_applied_independently) {
    auto caps = make_psu_caps();
    caps.max_import_current_A = 200.0f;
    caps.nominal_min_export_current_A = 6.0f;
    caps.nominal_min_import_current_A = 8.0f;

    const auto result = module::apply_powermeter_limits(caps, make_meter_caps(10.0f, 12.0f));

    EXPECT_FLOAT_EQ(result.min_export_current_A, 10.0f);
    ASSERT_TRUE(result.nominal_min_export_current_A.has_value());
    EXPECT_FLOAT_EQ(result.nominal_min_export_current_A.value(), 10.0f);
    ASSERT_TRUE(result.min_import_current_A.has_value());
    EXPECT_FLOAT_EQ(result.min_import_current_A.value(), 12.0f);
    ASSERT_TRUE(result.nominal_min_import_current_A.has_value());
    EXPECT_FLOAT_EQ(result.nominal_min_import_current_A.value(), 12.0f);
}
