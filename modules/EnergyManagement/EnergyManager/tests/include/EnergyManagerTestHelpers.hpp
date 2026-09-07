// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#pragma once

#include <optional>
#include <string>
#include <vector>

#include <generated/types/energy.hpp>
#include <generated/types/powermeter.hpp>

#include "EnergyManagerImpl.hpp"

namespace module::test {

// The instant most tests run the optimizer at. Measurements meant to be fresh must carry
// this (or a nearby) timestamp: the redistribution broker rejects readings older than its
// configured maximum age, and the older default timestamp of set_measurement() is half an
// hour before this - deliberately stale.
inline constexpr auto NOW_TS = "2026-08-04T12:30:00.000Z";

// A schedule_total_duration of 1h with a 60min interval yields exactly one schedule slot,
// which keeps assertions on enforced limits unambiguous.
inline EnergyManagerConfig make_default_config() {
    EnergyManagerConfig c;
    c.nominal_ac_voltage = 230.0;
    c.update_interval = 1;
    c.schedule_interval_duration = 60;
    c.schedule_total_duration = 1;
    c.slice_ampere = 0.5;
    c.slice_watt = 500;
    c.debug = false;
    c.switch_3ph1ph_while_charging_mode = "Never";
    c.switch_3ph1ph_max_nr_of_switches_per_session = 0;
    c.switch_3ph1ph_switch_limit_stickyness = "DontChange";
    c.switch_3ph1ph_power_hysteresis_W = 200;
    c.switch_3ph1ph_time_hysteresis_s = 0;
    c.broker_strategy = "FastCharging";
    c.redistribution_margin_A = 2.0;
    c.redistribution_start_with_lower_limit = true;
    c.redistribution_reduction_hold_s = 30;
    c.redistribution_measurement_max_age_s = 10;
    c.power_meter_aggregation_window_s = 5;
    c.power_redistribution_margin = 0.1;
    c.power_redistribution_gain = 0.5;
    c.power_redistribution_hold_time_s = 10;
    return c;
}

// PowerRedistribution with the reduction hold disabled, so allocation tests exercise the
// measurement tracking directly; hold behaviour tests set their own hold time.
inline EnergyManagerConfig make_redistribution_config() {
    auto c = make_default_config();
    c.broker_strategy = "PowerRedistribution";
    c.redistribution_reduction_hold_s = 0;
    return c;
}

inline types::energy::ScheduleReqEntry make_schedule_entry(const std::string& timestamp, float max_current_A,
                                                           float min_current_A,
                                                           std::optional<float> total_power_W = std::nullopt) {
    types::energy::ScheduleReqEntry e;
    e.timestamp = timestamp;
    e.limits_to_root.ac_max_current_A = {max_current_A, "TEST_max_current"};
    e.limits_to_root.ac_min_current_A = {min_current_A, "TEST_min_current"};
    e.limits_to_root.ac_max_phase_count = {3, "TEST_max_phases"};
    e.limits_to_root.ac_min_phase_count = {1, "TEST_min_phases"};
    e.limits_to_root.ac_number_of_active_phases = 3;
    e.limits_to_root.ac_supports_changing_phases_during_charging = false;
    if (total_power_W.has_value()) {
        e.limits_to_root.total_power_W = {total_power_W.value(), "TEST_total_power"};
    }
    return e;
}

inline types::energy::EnergyFlowRequest make_evse_node(const std::string& uuid, float max_current_A,
                                                       float min_current_A,
                                                       std::optional<float> total_power_W = std::nullopt,
                                                       const std::string& timestamp = "2026-08-04T12:00:00.000Z") {
    types::energy::EnergyFlowRequest n;
    n.uuid = uuid;
    n.node_type = types::energy::NodeType::Evse;
    n.evse_state = types::energy::EvseState::Charging;
    n.priority_request = false;
    n.schedule_import = {make_schedule_entry(timestamp, max_current_A, min_current_A, total_power_W)};
    n.schedule_export = {make_schedule_entry(timestamp, 0.0f, 0.0f, 0.0f)};
    return n;
}

// An EVSE whose schedule carries only a watt limit, the shape a DC connector requests
// energy in. No ampere limit means the redistribution broker has nothing to narrow.
inline types::energy::EnergyFlowRequest make_dc_evse_node(const std::string& uuid, float total_power_W,
                                                          const std::string& timestamp = "2026-08-04T12:00:00.000Z") {
    types::energy::EnergyFlowRequest n;
    n.uuid = uuid;
    n.node_type = types::energy::NodeType::Evse;
    n.evse_state = types::energy::EvseState::Charging;
    n.priority_request = false;
    types::energy::ScheduleReqEntry import;
    import.timestamp = timestamp;
    import.limits_to_root.total_power_W = {total_power_W, "TEST_dc_total_power"};
    n.schedule_import = {import};
    types::energy::ScheduleReqEntry exp;
    exp.timestamp = timestamp;
    exp.limits_to_root.total_power_W = {0.0f, "TEST_dc_total_power"};
    n.schedule_export = {exp};
    return n;
}

inline types::energy::EnergyFlowRequest make_root_node(const std::string& uuid, float max_current_A,
                                                       std::optional<float> total_power_W,
                                                       std::vector<types::energy::EnergyFlowRequest> children,
                                                       const std::string& timestamp = "2026-08-04T12:00:00.000Z") {
    types::energy::EnergyFlowRequest n;
    n.uuid = uuid;
    n.node_type = types::energy::NodeType::Generic;
    n.priority_request = false;
    n.schedule_import = {make_schedule_entry(timestamp, max_current_A, 0.0f, total_power_W)};
    n.schedule_export = {make_schedule_entry(timestamp, 0.0f, 0.0f, 0.0f)};
    n.children = std::move(children);
    return n;
}

// Attach a leaves-side measurement, the field EvseManager populates for an EVSE.
inline void set_measurement(types::energy::EnergyFlowRequest& node, float power_W,
                            const std::string& timestamp = "2026-08-04T12:00:00.000Z") {
    types::powermeter::Powermeter p;
    p.timestamp = timestamp;
    p.energy_Wh_import.total = 0.0f;
    types::units::Power power;
    power.total = power_W;
    p.power_W = power;
    node.energy_usage_leaves = p;
}

// Attach a leaves-side per-phase current measurement. Merges into an existing
// leaves powermeter so power and current can be set independently.
inline void set_measurement_current(types::energy::EnergyFlowRequest& node, std::optional<float> l1,
                                    std::optional<float> l2, std::optional<float> l3,
                                    const std::string& timestamp = "2026-08-04T12:00:00.000Z") {
    types::powermeter::Powermeter p;
    if (node.energy_usage_leaves.has_value()) {
        p = node.energy_usage_leaves.value();
    } else {
        p.energy_Wh_import.total = 0.0f;
    }
    // Always the timestamp the caller asked for. Keeping the existing one when the node
    // already carries a meter silently pins every later reading of a session to the age of
    // the first, which is the one thing a freshness test must not do.
    p.timestamp = timestamp;
    types::units::Current current;
    current.L1 = l1;
    current.L2 = l2;
    current.L3 = l3;
    p.current_A = current;
    node.energy_usage_leaves = p;
}

inline std::optional<types::energy::EnforcedLimits>
find_limit(const std::vector<types::energy::EnforcedLimits>& results, const std::string& uuid) {
    for (const auto& r : results) {
        if (r.uuid == uuid) {
            return r;
        }
    }
    return std::nullopt;
}

} // namespace module::test
