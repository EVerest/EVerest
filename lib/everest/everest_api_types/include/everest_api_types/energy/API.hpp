// SPDX-License-Identifier: Apache-2.0
// Copyright 2020 - 2025 Pionix GmbH and Contributors to EVerest

#pragma once

#include <cstdint>
#include <everest_api_types/powermeter/API.hpp>
#include <optional>
#include <string>
#include <vector>

namespace everest::lib::API::V1_0::types::energy {

struct NumberWithSource {
    float value;
    std::string source;
};

struct IntegerWithSource {
    int32_t value;
    std::string source;
};

struct FrequencyWattPoint {
    float frequency_Hz;
    float total_power_W;
};

struct SetpointType {
    int32_t priority;
    std::string source;
    std::optional<float> ac_current_A;
    std::optional<float> total_power_W;
    std::optional<std::vector<types::energy::FrequencyWattPoint>> frequency_table;
};

struct PricePerkWh {
    std::string timestamp;
    float value;
    std::string currency;
};

struct LimitsReq {
    std::optional<NumberWithSource> total_power_W;
    std::optional<NumberWithSource> ac_max_current_A;
    std::optional<NumberWithSource> ac_min_current_A;
    std::optional<IntegerWithSource> ac_max_phase_count;
    std::optional<IntegerWithSource> ac_min_phase_count;
    std::optional<bool> ac_supports_changing_phases_during_charging;
    std::optional<int32_t> ac_number_of_active_phases;
};

struct LimitsRes {
    std::optional<NumberWithSource> total_power_W;
    std::optional<NumberWithSource> ac_max_current_A;
    std::optional<IntegerWithSource> ac_max_phase_count;
};

struct ScheduleReqEntry {
    std::string timestamp;
    LimitsReq limits_to_root;
    LimitsReq limits_to_leaves;
    std::optional<float> conversion_efficiency;
    std::optional<PricePerkWh> price_per_kwh;
};

struct ScheduleResEntry {
    std::string timestamp;
    LimitsRes limits_to_root;
    std::optional<PricePerkWh> price_per_kwh;
};

struct ScheduleSetpointEntry {
    std::string timestamp;
    std::optional<SetpointType> setpoint;
};

struct ExternalLimits {
    std::vector<ScheduleReqEntry> schedule_import;
    std::vector<ScheduleReqEntry> schedule_export;
    std::vector<ScheduleSetpointEntry> schedule_setpoints;
};

struct EnforcedLimits {
    std::string uuid;
    int32_t valid_for;
    LimitsRes limits_root_side;
    std::vector<ScheduleResEntry> schedule;
};

struct CapabilityLimits {
    float max_current_A;
    int32_t max_phase_count;
    float nominal_voltage_V;
    float total_power_W;
};

enum class NodeType {
    Undefined,
    Evse,
    Generic,
};

enum class EvseState {
    Unplugged,
    WaitForAuth,
    WaitForEnergy,
    PrepareCharging,
    PausedEV,
    PausedEVSE,
    Charging,
    Finished,
    Disabled,
};

struct OptimizerTarget {
    std::optional<float> energy_amount_needed;
    std::optional<float> charge_to_max_percent;
    std::optional<float> car_battery_soc;
    std::optional<std::string> leave_time;
    std::optional<float> price_limit;
    std::optional<bool> full_autonomy;
};

struct EnergyFlowRequest {
    std::vector<EnergyFlowRequest> children;
    std::string uuid;
    NodeType node_type;
    std::vector<ScheduleReqEntry> schedule_import;
    std::vector<ScheduleReqEntry> schedule_export;
    std::vector<ScheduleSetpointEntry> schedule_setpoints;
    std::optional<bool> priority_request;
    std::optional<EvseState> evse_state;
    std::optional<OptimizerTarget> optimizer_target;
    std::optional<types::powermeter::PowermeterValues> energy_usage_root;
    std::optional<types::powermeter::PowermeterValues> energy_usage_leaves;
};

} // namespace everest::lib::API::V1_0::types::energy
