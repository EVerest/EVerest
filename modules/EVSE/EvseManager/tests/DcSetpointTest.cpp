// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include <gtest/gtest.h>

#include <optional>

#include <generated/types/power_supply_DC.hpp>
#include <generated/types/powermeter.hpp>

#include "dc_setpoint.hpp"
#include "powermeter_limits.hpp"

namespace {

/// \brief Power supply: charging 3 A to 200 A, discharging 2 A to 100 A.
types::power_supply_DC::Capabilities make_psu_caps() {
    types::power_supply_DC::Capabilities caps{};
    caps.bidirectional = true;
    caps.current_regulation_tolerance_A = 0.5f;
    caps.peak_current_ripple_A = 0.5f;
    caps.max_export_voltage_V = 900.0f;
    caps.min_export_voltage_V = 150.0f;
    caps.max_export_current_A = 200.0f;
    caps.min_export_current_A = 3.0f;
    caps.max_export_power_W = 150000.0f;
    caps.max_import_current_A = 100.0f;
    caps.min_import_current_A = 2.0f;
    return caps;
}

/// \brief Capabilities offered to the EV with a power meter needing \p meter_min_A in both directions.
types::power_supply_DC::Capabilities with_meter_min(const types::power_supply_DC::Capabilities& caps,
                                                    float meter_min_A) {
    types::powermeter::Capabilities meter{};
    meter.min_import_current_A = meter_min_A;
    meter.min_export_current_A = meter_min_A;
    return module::apply_powermeter_limits(caps, meter, false);
}

constexpr double no_evse_limit = 1000.0;

double charge(double ramped, double ev_target, bool current_demand_active, float meter_min_A = 0.0f,
              double evse_max_current = no_evse_limit) {
    const auto caps = make_psu_caps();
    return module::dc_export_setpoint_current(ramped, ev_target, evse_max_current, current_demand_active, caps,
                                              with_meter_min(caps, meter_min_A));
}

double discharge(double ramped, double ev_target, float meter_min_A = 0.0f, double evse_max_current = no_evse_limit) {
    const auto caps = make_psu_caps();
    return module::dc_import_setpoint_current(ramped, ev_target, evse_max_current, caps,
                                              with_meter_min(caps, meter_min_A));
}

} // namespace

// Cable check and precharge: below the power supply minimum is raised to it, the meter minimum does not apply

TEST(DcSetpointTest, cable_check_current_below_psu_min_is_raised_to_psu_min) {
    EXPECT_DOUBLE_EQ(charge(2.0, 2.0, false), 3.0);
}

TEST(DcSetpointTest, precharge_ignores_meter_min) {
    EXPECT_DOUBLE_EQ(charge(2.0, 2.0, false, 8.0f), 3.0);
}

TEST(DcSetpointTest, precharge_current_within_limits_is_unchanged) {
    EXPECT_DOUBLE_EQ(charge(5.0, 5.0, false, 8.0f), 5.0);
}

TEST(DcSetpointTest, current_above_psu_max_is_clamped) {
    EXPECT_DOUBLE_EQ(charge(250.0, 250.0, false), 200.0);
    EXPECT_DOUBLE_EQ(charge(250.0, 250.0, true), 200.0);
}

// Current demand

TEST(DcSetpointTest, target_within_range_is_unchanged) {
    EXPECT_DOUBLE_EQ(charge(50.0, 50.0, true, 8.0f), 50.0);
    EXPECT_DOUBLE_EQ(charge(8.0, 8.0, true, 8.0f), 8.0);
}

TEST(DcSetpointTest, target_between_psu_min_and_meter_min_is_zero) {
    EXPECT_DOUBLE_EQ(charge(6.4, 6.4, true, 8.0f), 0.0);
}

TEST(DcSetpointTest, target_below_psu_min_is_zero) {
    EXPECT_DOUBLE_EQ(charge(2.0, 2.0, true), 0.0);
}

TEST(DcSetpointTest, target_zero_is_zero) {
    EXPECT_DOUBLE_EQ(charge(0.0, 0.0, true, 8.0f), 0.0);
}

TEST(DcSetpointTest, ramp_below_offered_min_is_raised_to_it) {
    EXPECT_DOUBLE_EQ(charge(2.5, 50.0, true, 8.0f), 8.0);
}

TEST(DcSetpointTest, ramp_above_offered_min_is_unchanged) {
    EXPECT_DOUBLE_EQ(charge(30.0, 50.0, true, 8.0f), 30.0);
}

TEST(DcSetpointTest, ramp_towards_target_below_offered_min_is_zero) {
    EXPECT_DOUBLE_EQ(charge(2.5, 6.4, true, 8.0f), 0.0);
}

TEST(DcSetpointTest, higher_nominal_min_is_the_offered_min) {
    auto caps = make_psu_caps();
    caps.nominal_min_export_current_A = 7.0f;

    EXPECT_DOUBLE_EQ(module::dc_export_setpoint_current(6.0, 6.0, no_evse_limit, true, caps, caps), 0.0);
    EXPECT_DOUBLE_EQ(module::dc_export_setpoint_current(7.0, 7.0, no_evse_limit, true, caps, caps), 7.0);
}

TEST(DcSetpointTest, lower_nominal_min_is_the_offered_min) {
    auto caps = make_psu_caps();
    caps.nominal_min_export_current_A = 1.0f;

    EXPECT_DOUBLE_EQ(module::dc_export_setpoint_current(0.5, 0.5, no_evse_limit, true, caps, caps), 0.0);
    EXPECT_DOUBLE_EQ(module::dc_export_setpoint_current(5.0, 5.0, no_evse_limit, true, caps, caps), 5.0);
}

TEST(DcSetpointTest, target_above_lower_nominal_min_is_raised_to_psu_min) {
    auto caps = make_psu_caps();
    caps.nominal_min_export_current_A = 1.0f;

    EXPECT_DOUBLE_EQ(module::dc_export_setpoint_current(2.0, 2.0, no_evse_limit, true, caps, caps), 3.0);
}

TEST(DcSetpointTest, discharge_target_above_lower_nominal_min_is_raised_to_psu_min) {
    auto caps = make_psu_caps();
    caps.nominal_min_import_current_A = 1.0f;

    EXPECT_DOUBLE_EQ(module::dc_import_setpoint_current(1.5, 1.5, no_evse_limit, caps, caps), 2.0);
}

TEST(DcSetpointTest, discharge_nominal_min_is_the_offered_min) {
    auto caps = make_psu_caps();
    caps.nominal_min_import_current_A = 6.0f;

    EXPECT_DOUBLE_EQ(module::dc_import_setpoint_current(4.0, 4.0, no_evse_limit, caps, caps), 0.0);
    EXPECT_DOUBLE_EQ(module::dc_import_setpoint_current(6.0, 6.0, no_evse_limit, caps, caps), 6.0);
}

// Discharging (current demand only)

TEST(DcSetpointTest, discharge_target_within_range_is_unchanged) {
    EXPECT_DOUBLE_EQ(discharge(50.0, 50.0, 6.0f), 50.0);
}

TEST(DcSetpointTest, discharge_above_psu_max_is_clamped) {
    EXPECT_DOUBLE_EQ(discharge(150.0, 150.0), 100.0);
}

TEST(DcSetpointTest, discharge_target_below_meter_min_is_zero) {
    EXPECT_DOUBLE_EQ(discharge(4.0, 4.0, 6.0f), 0.0);
}

TEST(DcSetpointTest, discharge_ramp_below_offered_min_is_raised_to_it) {
    EXPECT_DOUBLE_EQ(discharge(1.0, 50.0, 6.0f), 6.0);
}

// Energy management limit below the offered minimum (current demand only)

TEST(DcSetpointTest, evse_max_below_offered_min_is_zero) {
    EXPECT_DOUBLE_EQ(charge(6.0, 50.0, true, 8.0f, 6.0), 0.0);
}

TEST(DcSetpointTest, evse_max_at_offered_min_is_delivered) {
    EXPECT_DOUBLE_EQ(charge(8.0, 50.0, true, 8.0f, 8.0), 8.0);
}

TEST(DcSetpointTest, evse_max_below_offered_min_is_ignored_outside_current_demand) {
    EXPECT_DOUBLE_EQ(charge(5.0, 50.0, false, 8.0f, 6.0), 5.0);
}

TEST(DcSetpointTest, discharge_evse_max_below_offered_min_is_zero) {
    EXPECT_DOUBLE_EQ(discharge(5.0, 50.0, 6.0f, 5.0), 0.0);
}

TEST(DcSetpointTest, discharge_without_minimum_is_unchanged) {
    auto caps = make_psu_caps();
    caps.min_import_current_A.reset();

    EXPECT_DOUBLE_EQ(module::dc_import_setpoint_current(0.5, 0.5, no_evse_limit, caps, caps), 0.5);
}
