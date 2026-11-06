// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include <everest/ocpp_module_common/external_limits.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <initializer_list>
#include <vector>

namespace ocpp_module_common {
namespace external_limits {

namespace {

constexpr auto SETPOINT_SOURCE = "OCPP";
constexpr std::int32_t DEFAULT_NUMBER_PHASES = 3;

using ocpp::v2::ChargingRateUnitEnum;
using ocpp::v2::EnhancedChargingSchedulePeriod;

float phase_aware_value(ChargingRateUnitEnum unit, float value, const std::optional<float>& value_L2,
                        const std::optional<float>& value_L3, const std::optional<std::int32_t>& number_phases) {
    if (unit == ChargingRateUnitEnum::A) {
        return most_restrictive_phase_value(value, value_L2, value_L3);
    }
    return total_power_W(value, value_L2, value_L3, number_phases);
}

types::energy::LimitsReq to_limits_req(ChargingRateUnitEnum unit, float value,
                                       const std::optional<std::int32_t>& number_phases, const std::string& source) {
    types::energy::LimitsReq limits_req;
    if (unit == ChargingRateUnitEnum::A) {
        limits_req.ac_max_current_A = {value, source};
    } else {
        limits_req.total_power_W = {value, source};
    }
    if (number_phases.has_value()) {
        limits_req.ac_max_phase_count = {number_phases.value(), source};
    }
    return limits_req;
}

std::optional<types::energy::ScheduleReqEntry> create_limits_entry(const std::string& timestamp,
                                                                   const EnhancedChargingSchedulePeriod& period,
                                                                   ChargingRateUnitEnum unit,
                                                                   const std::string& source) {
    if (not period.limit.has_value()) {
        return std::nullopt;
    }

    types::energy::ScheduleReqEntry entry;
    entry.timestamp = timestamp;
    entry.limits_to_leaves = to_limits_req(
        unit, phase_aware_value(unit, period.limit.value(), period.limit_L2, period.limit_L3, period.numberPhases),
        period.numberPhases, source);
    return entry;
}

std::optional<types::energy::ScheduleReqEntry> create_export_limits_entry(const std::string& timestamp,
                                                                          const EnhancedChargingSchedulePeriod& period,
                                                                          ChargingRateUnitEnum unit,
                                                                          const std::string& source) {
    if (not period.dischargeLimit.has_value()) {
        return std::nullopt;
    }

    types::energy::ScheduleReqEntry entry;
    entry.timestamp = timestamp;
    entry.limits_to_leaves =
        to_limits_req(unit,
                      std::abs(phase_aware_value(unit, period.dischargeLimit.value(), period.dischargeLimit_L2,
                                                 period.dischargeLimit_L3, period.numberPhases)),
                      period.numberPhases, source);
    return entry;
}

std::optional<types::energy::ScheduleSetpointEntry> create_setpoint_entry(std::int32_t setpoint_priority,
                                                                          const std::string& timestamp,
                                                                          const EnhancedChargingSchedulePeriod& period,
                                                                          ChargingRateUnitEnum unit) {
    const bool has_basic_setpoint = period.setpoint.has_value();
    const bool has_freq_table = period.v2xFreqWattCurve.has_value() && !period.v2xFreqWattCurve->empty();

    if (!has_basic_setpoint && !has_freq_table) {
        return std::nullopt;
    }

    types::energy::ScheduleSetpointEntry entry;
    types::energy::SetpointType setpoint;
    setpoint.source = SETPOINT_SOURCE;
    setpoint.priority = setpoint_priority;
    entry.timestamp = timestamp;

    if (has_basic_setpoint) {
        if (unit == ChargingRateUnitEnum::A) {
            setpoint.ac_current_A =
                most_restrictive_phase_value(period.setpoint.value(), period.setpoint_L2, period.setpoint_L3);
        } else {
            setpoint.total_power_W =
                total_power_W(period.setpoint.value(), period.setpoint_L2, period.setpoint_L3, period.numberPhases);
        }
    }

    if (has_freq_table) {
        std::vector<types::energy::FrequencyWattPoint> frequency_table;
        for (const auto& point : period.v2xFreqWattCurve.value()) {
            types::energy::FrequencyWattPoint freq_point{};
            freq_point.frequency_Hz = point.frequency;
            freq_point.total_power_W = point.power;
            frequency_table.push_back(freq_point);
        }
        setpoint.frequency_table = std::move(frequency_table);
    }

    entry.setpoint = std::move(setpoint);
    return entry;
}

} // namespace

float most_restrictive_phase_value(float value, const std::optional<float>& value_L2,
                                   const std::optional<float>& value_L3) {
    float result = value;
    for (const auto& phase_value : {value_L2, value_L3}) {
        if (phase_value.has_value() and std::abs(phase_value.value()) < std::abs(result)) {
            result = phase_value.value();
        }
    }
    return result;
}

float total_power_W(float value, const std::optional<float>& value_L2, const std::optional<float>& value_L3,
                    const std::optional<std::int32_t>& number_phases) {
    if (not value_L2.has_value() and not value_L3.has_value()) {
        return value;
    }

    const auto phases = std::clamp(number_phases.value_or(DEFAULT_NUMBER_PHASES), 1, DEFAULT_NUMBER_PHASES);
    float total = value;
    if (phases >= 2) {
        total += value_L2.value_or(value);
    }
    if (phases >= 3) {
        total += value_L3.value_or(value);
    }
    return total;
}

types::energy::ExternalLimits to_external_limits(const ocpp::v2::EnhancedCompositeSchedule& composite_schedule,
                                                 const ocpp::DateTime& start_time, const std::string& source,
                                                 std::int32_t setpoint_priority) {
    types::energy::ExternalLimits limits;
    std::vector<types::energy::ScheduleReqEntry> schedule_export;
    bool has_export_limit{false};

    const auto unit = composite_schedule.chargingRateUnit;

    for (const auto& period : composite_schedule.chargingSchedulePeriod) {
        const auto timestamp =
            ocpp::DateTime(start_time.to_time_point() + std::chrono::seconds(period.startPeriod)).to_rfc3339();

        if (auto setpoint_entry = create_setpoint_entry(setpoint_priority, timestamp, period, unit)) {
            limits.schedule_setpoints.push_back(*setpoint_entry);
        }

        if (auto limits_entry = create_limits_entry(timestamp, period, unit, source)) {
            limits.schedule_import.push_back(*limits_entry);
        }

        // a period without a discharge limit gets an empty entry so a previous limit does not carry over
        auto export_entry = create_export_limits_entry(timestamp, period, unit, source);
        if (export_entry.has_value()) {
            has_export_limit = true;
        } else {
            export_entry.emplace();
            export_entry->timestamp = timestamp;
        }
        schedule_export.push_back(*export_entry);
    }

    if (has_export_limit) {
        limits.schedule_export = std::move(schedule_export);
    }

    return limits;
}

} // namespace external_limits
} // namespace ocpp_module_common
