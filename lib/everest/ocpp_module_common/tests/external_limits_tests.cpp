// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include <gtest/gtest.h>

#include <chrono>
#include <optional>

#include <everest/ocpp_module_common/external_limits.hpp>

namespace {

using namespace ocpp_module_common::external_limits;
using ocpp::v2::ChargingRateUnitEnum;
using ocpp::v2::EnhancedChargingSchedulePeriod;
using ocpp::v2::EnhancedCompositeSchedule;

constexpr auto SOURCE = "OCPP201/OCPP_set_external_limits";
constexpr std::int32_t SETPOINT_PRIORITY = 7;
const ocpp::DateTime START_TIME("2026-01-01T00:00:00.000Z");

EnhancedChargingSchedulePeriod make_period(std::int32_t start_period, std::optional<float> limit,
                                           std::optional<float> discharge_limit) {
    EnhancedChargingSchedulePeriod period;
    period.startPeriod = start_period;
    period.limit = limit;
    period.dischargeLimit = discharge_limit;
    return period;
}

EnhancedCompositeSchedule make_schedule(ChargingRateUnitEnum unit,
                                        std::vector<EnhancedChargingSchedulePeriod> periods) {
    EnhancedCompositeSchedule schedule;
    schedule.evseId = 1;
    schedule.chargingRateUnit = unit;
    schedule.chargingSchedulePeriod = std::move(periods);
    return schedule;
}

std::string timestamp_at(std::int32_t seconds) {
    return ocpp::DateTime(START_TIME.to_time_point() + std::chrono::seconds(seconds)).to_rfc3339();
}

TEST(TotalPowerW, TotalWithoutPhaseValues) {
    EXPECT_FLOAT_EQ(total_power_W(8280.F, std::nullopt, std::nullopt, 3), 8280.F);
    EXPECT_FLOAT_EQ(total_power_W(8280.F, std::nullopt, std::nullopt, std::nullopt), 8280.F);
}

TEST(TotalPowerW, SumsThreePhases) {
    EXPECT_FLOAT_EQ(total_power_W(2760.F, 2760.F, 2760.F, 3), 8280.F);
    EXPECT_FLOAT_EQ(total_power_W(2760.F, 2760.F, 2760.F, std::nullopt), 8280.F);
}

TEST(TotalPowerW, SumsOnlyUsedPhases) {
    EXPECT_FLOAT_EQ(total_power_W(3500.F, 3500.F, 3500.F, 2), 7000.F);
    EXPECT_FLOAT_EQ(total_power_W(3500.F, 3500.F, 3500.F, 1), 3500.F);
}

TEST(TotalPowerW, TwoPhaseForm) {
    EXPECT_FLOAT_EQ(total_power_W(3000.F, 2000.F, std::nullopt, 2), 5000.F);
}

TEST(TotalPowerW, MissingPhaseTakesL1) {
    EXPECT_FLOAT_EQ(total_power_W(3000.F, 2000.F, std::nullopt, 3), 8000.F);
    EXPECT_FLOAT_EQ(total_power_W(3000.F, std::nullopt, 2000.F, 3), 8000.F);
}

TEST(MostRestrictivePhaseValue, LowestLimit) {
    EXPECT_FLOAT_EQ(most_restrictive_phase_value(16.F, 10.F, 12.F), 10.F);
    EXPECT_FLOAT_EQ(most_restrictive_phase_value(16.F, std::nullopt, std::nullopt), 16.F);
    EXPECT_FLOAT_EQ(most_restrictive_phase_value(16.F, std::nullopt, 12.F), 12.F);
}

TEST(MostRestrictivePhaseValue, SmallestDischargeMagnitude) {
    EXPECT_FLOAT_EQ(most_restrictive_phase_value(-20.F, -8.F, -10.F), -8.F);
}

TEST(ToExternalLimits, WattsSumOverUsedPhases) {
    auto period = make_period(0, 3500.F, -1000.F);
    period.limit_L2 = 3500.F;
    period.limit_L3 = 3500.F;
    period.dischargeLimit_L2 = -1000.F;
    period.dischargeLimit_L3 = -1000.F;
    period.setpoint = 1500.F;
    period.setpoint_L2 = 1500.F;
    period.setpoint_L3 = 1500.F;
    period.numberPhases = 2;

    const auto limits =
        to_external_limits(make_schedule(ChargingRateUnitEnum::W, {period}), START_TIME, SOURCE, SETPOINT_PRIORITY);

    ASSERT_EQ(limits.schedule_import.size(), 1);
    ASSERT_TRUE(limits.schedule_import[0].limits_to_leaves.total_power_W.has_value());
    EXPECT_FLOAT_EQ(limits.schedule_import[0].limits_to_leaves.total_power_W->value, 7000.F);
    EXPECT_EQ(limits.schedule_import[0].limits_to_leaves.total_power_W->source, SOURCE);
    ASSERT_TRUE(limits.schedule_import[0].limits_to_leaves.ac_max_phase_count.has_value());
    EXPECT_EQ(limits.schedule_import[0].limits_to_leaves.ac_max_phase_count->value, 2);

    ASSERT_EQ(limits.schedule_export.size(), 1);
    ASSERT_TRUE(limits.schedule_export[0].limits_to_leaves.total_power_W.has_value());
    EXPECT_FLOAT_EQ(limits.schedule_export[0].limits_to_leaves.total_power_W->value, 2000.F);
    ASSERT_TRUE(limits.schedule_export[0].limits_to_leaves.ac_max_phase_count.has_value());
    EXPECT_EQ(limits.schedule_export[0].limits_to_leaves.ac_max_phase_count->value, 2);

    ASSERT_EQ(limits.schedule_setpoints.size(), 1);
    ASSERT_TRUE(limits.schedule_setpoints[0].setpoint.has_value());
    EXPECT_FLOAT_EQ(limits.schedule_setpoints[0].setpoint->total_power_W.value(), 3000.F);
}

TEST(ToExternalLimits, AmpsLowestPhaseValue) {
    auto period = make_period(0, 16.F, -20.F);
    period.limit_L2 = 10.F;
    period.limit_L3 = 12.F;
    period.dischargeLimit_L2 = -8.F;
    period.dischargeLimit_L3 = -10.F;
    period.setpoint = 10.F;
    period.setpoint_L2 = 6.F;
    period.setpoint_L3 = 8.F;
    period.numberPhases = 3;

    const auto limits =
        to_external_limits(make_schedule(ChargingRateUnitEnum::A, {period}), START_TIME, SOURCE, SETPOINT_PRIORITY);

    ASSERT_EQ(limits.schedule_import.size(), 1);
    const auto& import = limits.schedule_import[0].limits_to_leaves;
    ASSERT_TRUE(import.ac_max_current_A.has_value());
    EXPECT_FLOAT_EQ(import.ac_max_current_A->value, 10.F);
    ASSERT_TRUE(import.ac_max_phase_count.has_value());
    EXPECT_EQ(import.ac_max_phase_count->value, 3);
    EXPECT_FALSE(import.total_power_W.has_value());

    ASSERT_EQ(limits.schedule_export.size(), 1);
    const auto& export_limits = limits.schedule_export[0].limits_to_leaves;
    ASSERT_TRUE(export_limits.ac_max_current_A.has_value());
    EXPECT_FLOAT_EQ(export_limits.ac_max_current_A->value, 8.F);
    ASSERT_TRUE(export_limits.ac_max_phase_count.has_value());
    EXPECT_EQ(export_limits.ac_max_phase_count->value, 3);

    ASSERT_EQ(limits.schedule_setpoints.size(), 1);
    EXPECT_FLOAT_EQ(limits.schedule_setpoints[0].setpoint->ac_current_A.value(), 6.F);
}

TEST(ToExternalLimits, ExportEntryForEveryPeriod) {
    const auto limits =
        to_external_limits(make_schedule(ChargingRateUnitEnum::W,
                                         {make_period(0, 8000.F, -2000.F), make_period(3600, 6000.F, std::nullopt)}),
                           START_TIME, SOURCE, SETPOINT_PRIORITY);

    ASSERT_EQ(limits.schedule_import.size(), 2);
    EXPECT_EQ(limits.schedule_import[0].timestamp, timestamp_at(0));
    EXPECT_EQ(limits.schedule_import[1].timestamp, timestamp_at(3600));

    ASSERT_EQ(limits.schedule_export.size(), 2);
    EXPECT_EQ(limits.schedule_export[0].timestamp, timestamp_at(0));
    EXPECT_FLOAT_EQ(limits.schedule_export[0].limits_to_leaves.total_power_W->value, 2000.F);
    EXPECT_EQ(limits.schedule_export[1].timestamp, timestamp_at(3600));
    EXPECT_FALSE(limits.schedule_export[1].limits_to_leaves.total_power_W.has_value());
}

TEST(ToExternalLimits, NoExportWithoutDischargeLimit) {
    const auto limits =
        to_external_limits(make_schedule(ChargingRateUnitEnum::W, {make_period(0, 8000.F, std::nullopt)}), START_TIME,
                           SOURCE, SETPOINT_PRIORITY);

    EXPECT_EQ(limits.schedule_import.size(), 1);
    EXPECT_FALSE(limits.schedule_import[0].limits_to_leaves.ac_max_phase_count.has_value());
    EXPECT_TRUE(limits.schedule_export.empty());
    EXPECT_TRUE(limits.schedule_setpoints.empty());
}

TEST(ToExternalLimits, SetpointSourceAndPriority) {
    auto period = make_period(0, std::nullopt, std::nullopt);
    period.setpoint = -4000.F;

    const auto limits =
        to_external_limits(make_schedule(ChargingRateUnitEnum::W, {period}), START_TIME, SOURCE, SETPOINT_PRIORITY);

    EXPECT_TRUE(limits.schedule_import.empty());
    ASSERT_EQ(limits.schedule_setpoints.size(), 1);
    EXPECT_EQ(limits.schedule_setpoints[0].timestamp, timestamp_at(0));
    EXPECT_EQ(limits.schedule_setpoints[0].setpoint->source, "OCPP");
    EXPECT_EQ(limits.schedule_setpoints[0].setpoint->priority, SETPOINT_PRIORITY);
    EXPECT_FLOAT_EQ(limits.schedule_setpoints[0].setpoint->total_power_W.value(), -4000.F);
}

} // namespace
