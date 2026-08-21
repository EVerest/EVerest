// SPDX-License-Identifier: Apache-2.0
// Copyright 2020 - 2025 Pionix GmbH and Contributors to EVerest

#include "energy/json_codec.hpp"
#include "energy/API.hpp"
#include "nlohmann/json.hpp"
#include "powermeter/API.hpp"
#include "powermeter/json_codec.hpp"
#include <stdexcept>
#include <string>

namespace everest::lib::API::V1_0::types::energy {

void to_json(json& j, NumberWithSource const& k) noexcept {
    j = json{
        {"value", k.value},
        {"source", k.source},
    };
}

void from_json(const json& j, NumberWithSource& k) {
    k.value = j.at("value");
    k.source = j.at("source");
}

void to_json(json& j, IntegerWithSource const& k) noexcept {
    j = json{
        {"value", k.value},
        {"source", k.source},
    };
}

void from_json(const json& j, IntegerWithSource& k) {
    k.value = j.at("value");
    k.source = j.at("source");
}

void to_json(json& j, FrequencyWattPoint const& k) noexcept {
    j = json{
        {"frequency_Hz", k.frequency_Hz},
        {"total_power_W", k.total_power_W},
    };
}

void from_json(const json& j, FrequencyWattPoint& k) {
    k.frequency_Hz = j.at("frequency_Hz");
    k.total_power_W = j.at("total_power_W");
}

void to_json(json& j, SetpointType const& k) noexcept {
    j = json{
        {"priority", k.priority},
        {"source", k.source},
    };
    if (k.ac_current_A) {
        j["ac_current_A"] = k.ac_current_A.value();
    }
    if (k.total_power_W) {
        j["total_power_W"] = k.total_power_W.value();
    }
    if (k.frequency_table) {
        j["frequency_table"] = json::array();
        for (auto val : k.frequency_table.value()) {
            j["frequency_table"].push_back(val);
        }
    }
}

void from_json(const json& j, SetpointType& k) {
    k.priority = j.at("priority");
    k.source = j.at("source");

    if (j.contains("ac_current_A")) {
        k.ac_current_A.emplace(j.at("ac_current_A"));
    }
    if (j.contains("total_power_W")) {
        k.total_power_W.emplace(j.at("total_power_W"));
    }
    if (j.contains("frequency_table")) {
        json arr = j.at("frequency_table");
        std::vector<types::energy::FrequencyWattPoint> vec;
        for (auto val : arr) {
            vec.push_back(val);
        }
        k.frequency_table.emplace(vec);
    }
}

void to_json(json& j, PricePerkWh const& k) noexcept {
    j = json{
        {"timestamp", k.timestamp},
        {"value", k.value},
        {"currency", k.currency},
    };
}

void from_json(const json& j, PricePerkWh& k) {
    k.timestamp = j.at("timestamp");
    k.value = j.at("value");
    k.currency = j.at("currency");
}

void to_json(json& j, LimitsReq const& k) noexcept {
    j = json({});
    // the optional parts of the type
    if (k.total_power_W) {
        j["total_power_W"] = k.total_power_W.value();
    }
    if (k.ac_max_current_A) {
        j["ac_max_current_A"] = k.ac_max_current_A.value();
    }
    if (k.ac_min_current_A) {
        j["ac_min_current_A"] = k.ac_min_current_A.value();
    }
    if (k.ac_max_phase_count) {
        j["ac_max_phase_count"] = k.ac_max_phase_count.value();
    }
    if (k.ac_min_phase_count) {
        j["ac_min_phase_count"] = k.ac_min_phase_count.value();
    }
    if (k.ac_supports_changing_phases_during_charging) {
        j["ac_supports_changing_phases_during_charging"] = k.ac_supports_changing_phases_during_charging.value();
    }
    if (k.ac_number_of_active_phases) {
        j["ac_number_of_active_phases"] = k.ac_number_of_active_phases.value();
    }
}

void from_json(const json& j, LimitsReq& k) {
    if (j.contains("total_power_W")) {
        k.total_power_W.emplace(j.at("total_power_W"));
    }
    if (j.contains("ac_max_current_A")) {
        k.ac_max_current_A.emplace(j.at("ac_max_current_A"));
    }
    if (j.contains("ac_min_current_A")) {
        k.ac_min_current_A.emplace(j.at("ac_min_current_A"));
    }
    if (j.contains("ac_max_phase_count")) {
        k.ac_max_phase_count.emplace(j.at("ac_max_phase_count"));
    }
    if (j.contains("ac_min_phase_count")) {
        k.ac_min_phase_count.emplace(j.at("ac_min_phase_count"));
    }
    if (j.contains("ac_supports_changing_phases_during_charging")) {
        k.ac_supports_changing_phases_during_charging.emplace(j.at("ac_supports_changing_phases_during_charging"));
    }
    if (j.contains("ac_number_of_active_phases")) {
        k.ac_number_of_active_phases.emplace(j.at("ac_number_of_active_phases"));
    }
}

void to_json(json& j, LimitsRes const& k) noexcept {
    j = json({});
    if (k.total_power_W) {
        j["total_power_W"] = k.total_power_W.value();
    }
    if (k.ac_max_current_A) {
        j["ac_max_current_A"] = k.ac_max_current_A.value();
    }
    if (k.ac_max_phase_count) {
        j["ac_max_phase_count"] = k.ac_max_phase_count.value();
    }
}

void from_json(const json& j, LimitsRes& k) {
    if (j.contains("total_power_W")) {
        k.total_power_W.emplace(j.at("total_power_W"));
    }
    if (j.contains("ac_max_current_A")) {
        k.ac_max_current_A.emplace(j.at("ac_max_current_A"));
    }
    if (j.contains("ac_max_phase_count")) {
        k.ac_max_phase_count.emplace(j.at("ac_max_phase_count"));
    }
}

void to_json(json& j, ScheduleReqEntry const& k) noexcept {
    j = json{
        {"timestamp", k.timestamp},
        {"limits_to_root", k.limits_to_root},
        {"limits_to_leaves", k.limits_to_leaves},
    };

    if (k.conversion_efficiency) {
        j["conversion_efficiency"] = k.conversion_efficiency.value();
    }
    if (k.price_per_kwh) {
        j["price_per_kwh"] = k.price_per_kwh.value();
    }
}

void from_json(const json& j, ScheduleReqEntry& k) {
    k.timestamp = j.at("timestamp");
    k.limits_to_root = j.at("limits_to_root");
    k.limits_to_leaves = j.at("limits_to_leaves");

    if (j.contains("conversion_efficiency")) {
        k.conversion_efficiency.emplace(j.at("conversion_efficiency"));
    }
    if (j.contains("price_per_kwh")) {
        k.price_per_kwh.emplace(j.at("price_per_kwh"));
    }
}

void to_json(json& j, ScheduleResEntry const& k) noexcept {
    j = json{
        {"timestamp", k.timestamp},
        {"limits_to_root", k.limits_to_root},
    };
    if (k.price_per_kwh) {
        j["price_per_kwh"] = k.price_per_kwh.value();
    }
}

void from_json(const json& j, ScheduleResEntry& k) {
    k.timestamp = j.at("timestamp");
    k.limits_to_root = j.at("limits_to_root");

    if (j.contains("price_per_kwh")) {
        k.price_per_kwh.emplace(j.at("price_per_kwh"));
    }
}

void to_json(json& j, ScheduleSetpointEntry const& k) noexcept {
    j = json{
        {"timestamp", k.timestamp},
    };
    if (k.setpoint) {
        j["setpoint"] = k.setpoint.value();
    }
}

void from_json(const json& j, ScheduleSetpointEntry& k) {
    k.timestamp = j.at("timestamp");
    if (j.contains("setpoint")) {
        k.setpoint.emplace(j.at("setpoint"));
    }
}

void to_json(json& j, ExternalLimits const& k) noexcept {
    j = json{
        {"schedule_import", k.schedule_import},
        {"schedule_export", k.schedule_export},
        {"schedule_setpoints", k.schedule_setpoints},
    };
}

void from_json(const json& j, ExternalLimits& k) {
    for (auto val : j.at("schedule_import")) {
        k.schedule_import.push_back(val);
    }
    for (auto val : j.at("schedule_export")) {
        k.schedule_export.push_back(val);
    }
    for (auto val : j.at("schedule_setpoints")) {
        k.schedule_setpoints.push_back(val);
    }
}

void to_json(json& j, EnforcedLimits const& k) noexcept {
    j = json{
        {"uuid", k.uuid},
        {"valid_for", k.valid_for},
        {"limits_root_side", k.limits_root_side},
        {"schedule", k.schedule},
    };
}

void from_json(const json& j, EnforcedLimits& k) {
    k.uuid = j.at("uuid");
    k.valid_for = j.at("valid_for");
    k.limits_root_side = j.at("limits_root_side");
    for (auto val : j.at("schedule")) {
        k.schedule.push_back(val);
    }
}

void to_json(json& j, CapabilityLimits const& k) noexcept {
    j = json{
        {"max_current_A", k.max_current_A},
        {"max_phase_count", k.max_phase_count},
        {"nominal_voltage_V", k.nominal_voltage_V},
        {"total_power_W", k.total_power_W},
    };
}

void from_json(const json& j, CapabilityLimits& k) {
    k.max_current_A = j.at("max_current_A");
    k.max_phase_count = j.at("max_phase_count");
    k.nominal_voltage_V = j.at("nominal_voltage_V");
    k.total_power_W = j.at("total_power_W");
}

void from_json(const json& j, NodeType& k) {
    std::string s = j;
    if (s == "Undefined") {
        k = NodeType::Undefined;
        return;
    }
    if (s == "Evse") {
        k = NodeType::Evse;
        return;
    }
    if (s == "Generic") {
        k = NodeType::Generic;
        return;
    }

    throw std::out_of_range("Provided string " + s + " could not be converted to enum of type NodeType");
}

void to_json(json& j, NodeType const& k) noexcept {
    switch (k) {
    case NodeType::Undefined:
        j = "Undefined";
        return;
    case NodeType::Evse:
        j = "Evse";
        return;
    case NodeType::Generic:
        j = "Generic";
        return;
    }
    j = "INVALID_VALUE__everest::lib::API::V1_0::types::energy::NodeType";
}

void from_json(const json& j, EvseState& k) {
    std::string s = j;
    if (s == "Unplugged") {
        k = EvseState::Unplugged;
        return;
    }
    if (s == "WaitForAuth") {
        k = EvseState::WaitForAuth;
        return;
    }
    if (s == "WaitForEnergy") {
        k = EvseState::WaitForEnergy;
        return;
    }
    if (s == "PrepareCharging") {
        k = EvseState::PrepareCharging;
        return;
    }
    if (s == "PausedEV") {
        k = EvseState::PausedEV;
        return;
    }
    if (s == "PausedEVSE") {
        k = EvseState::PausedEVSE;
        return;
    }
    if (s == "Charging") {
        k = EvseState::Charging;
        return;
    }
    if (s == "Finished") {
        k = EvseState::Finished;
        return;
    }
    if (s == "Disabled") {
        k = EvseState::Disabled;
        return;
    }

    throw std::out_of_range("Provided string " + s + " could not be converted to enum of type EvseState");
}

void to_json(json& j, EvseState const& k) noexcept {
    switch (k) {
    case EvseState::Unplugged:
        j = "Unplugged";
        return;
    case EvseState::WaitForAuth:
        j = "WaitForAuth";
        return;
    case EvseState::WaitForEnergy:
        j = "WaitForEnergy";
        return;
    case EvseState::PrepareCharging:
        j = "PrepareCharging";
        return;
    case EvseState::PausedEV:
        j = "PausedEV";
        return;
    case EvseState::PausedEVSE:
        j = "PausedEVSE";
        return;
    case EvseState::Charging:
        j = "Charging";
        return;
    case EvseState::Finished:
        j = "Finished";
        return;
    case EvseState::Disabled:
        j = "Disabled";
        return;
    }
    j = "INVALID_VALUE__everest::lib::API::V1_0::types::energy::EvseState";
}

void to_json(json& j, OptimizerTarget const& k) noexcept {
    j = json({});
    if (k.energy_amount_needed) {
        j["energy_amount_needed"] = k.energy_amount_needed.value();
    }
    if (k.charge_to_max_percent) {
        j["charge_to_max_percent"] = k.charge_to_max_percent.value();
    }
    if (k.car_battery_soc) {
        j["car_battery_soc"] = k.car_battery_soc.value();
    }
    if (k.leave_time) {
        j["leave_time"] = k.leave_time.value();
    }
    if (k.price_limit) {
        j["price_limit"] = k.price_limit.value();
    }
    if (k.full_autonomy) {
        j["full_autonomy"] = k.full_autonomy.value();
    }
}

void from_json(const json& j, OptimizerTarget& k) {
    if (j.contains("energy_amount_needed")) {
        k.energy_amount_needed.emplace(j.at("energy_amount_needed"));
    }
    if (j.contains("charge_to_max_percent")) {
        k.charge_to_max_percent.emplace(j.at("charge_to_max_percent"));
    }
    if (j.contains("car_battery_soc")) {
        k.car_battery_soc.emplace(j.at("car_battery_soc"));
    }
    if (j.contains("leave_time")) {
        k.leave_time.emplace(j.at("leave_time"));
    }
    if (j.contains("price_limit")) {
        k.price_limit.emplace(j.at("price_limit"));
    }
    if (j.contains("full_autonomy")) {
        k.full_autonomy.emplace(j.at("full_autonomy"));
    }
}

void to_json(json& j, EnergyFlowRequest const& k) noexcept {
    j = json{
        {"children", k.children},
        {"uuid", k.uuid},
        {"node_type", k.node_type},
        {"schedule_import", k.schedule_import},
        {"schedule_export", k.schedule_export},
        {"schedule_setpoints", k.schedule_setpoints},
    };
    if (k.priority_request) {
        j["priority_request"] = k.priority_request.value();
    }
    if (k.evse_state) {
        j["evse_state"] = k.evse_state.value();
    }
    if (k.optimizer_target) {
        j["optimizer_target"] = k.optimizer_target.value();
    }
    if (k.energy_usage_root) {
        j["energy_usage_root"] = k.energy_usage_root.value();
    }
    if (k.energy_usage_leaves) {
        j["energy_usage_leaves"] = k.energy_usage_leaves.value();
    }
}

void from_json(const json& j, EnergyFlowRequest& k) {
    for (auto val : j.at("children")) {
        k.children.push_back(val);
    }
    k.uuid = j.at("uuid");
    k.node_type = j.at("node_type");
    for (auto val : j.at("schedule_import")) {
        k.schedule_import.push_back(val);
    }
    for (auto val : j.at("schedule_export")) {
        k.schedule_export.push_back(val);
    }
    for (auto val : j.at("schedule_setpoints")) {
        k.schedule_setpoints.push_back(val);
    }
    if (j.contains("priority_request")) {
        k.priority_request.emplace(j.at("priority_request"));
    }
    if (j.contains("evse_state")) {
        k.evse_state.emplace(j.at("evse_state").get<EvseState>());
    }
    if (j.contains("optimizer_target")) {
        k.optimizer_target.emplace(j.at("optimizer_target").get<OptimizerTarget>());
    }
    if (j.contains("energy_usage_root")) {
        k.energy_usage_root.emplace(j.at("energy_usage_root").get<types::powermeter::PowermeterValues>());
    }
    if (j.contains("energy_usage_leaves")) {
        k.energy_usage_leaves.emplace(j.at("energy_usage_leaves").get<types::powermeter::PowermeterValues>());
    }
}

} // namespace everest::lib::API::V1_0::types::energy
