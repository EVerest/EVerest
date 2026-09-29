// SPDX-License-Identifier: Apache-2.0
// Copyright 2024 - 2026 Pionix GmbH and Contributors to EVerest

#include <everest/external_energy_limits/external_energy_limits.hpp>

#include <algorithm>

namespace external_energy_limits {

bool is_evse_sink_configured(const std::vector<std::unique_ptr<external_energy_limitsIntf>>& r_evse_energy_sink,
                             const int32_t evse_id) {
    for (const auto& evse_sink : r_evse_energy_sink) {
        if (not evse_sink->get_mapping().has_value()) {
            EVLOG_critical << "Please configure an evse mapping in your configuration file for the connected "
                              "r_evse_energy_sink with module_id: "
                           << evse_sink->module_id;
            throw std::runtime_error("No mapping configured for evse_id: " + std::to_string(evse_id));
        }
        if (evse_sink->get_mapping().value().evse == evse_id) {
            return true;
        }
    }
    return false;
}

external_energy_limitsIntf&
get_evse_sink_by_evse_id(const std::vector<std::unique_ptr<external_energy_limitsIntf>>& r_evse_energy_sink,
                         const int32_t evse_id) {
    for (const auto& evse_sink : r_evse_energy_sink) {
        if (not evse_sink->get_mapping().has_value()) {
            EVLOG_critical << "Please configure an evse mapping in your configuration file for the connected "
                              "r_evse_energy_sink with module_id: "
                           << evse_sink->module_id;
            throw std::runtime_error("No mapping configured for evse_id: " + std::to_string(evse_id));
        }
        if (evse_sink->get_mapping().value().evse == evse_id) {
            return *evse_sink;
        }
    }
    throw std::runtime_error("No mapping configured for evse_id: " + std::to_string(evse_id));
}

void set_current_limit(types::energy::LimitsReq& limits, float limit, const std::optional<float>& limit_L2,
                       const std::optional<float>& limit_L3, const std::string& source) {
    if (not limit_L2.has_value() and not limit_L3.has_value()) {
        limits.ac_max_current_A = {limit, source};
        return;
    }
    types::energy::PhaseCurrentsWithSource per_phase;
    per_phase.L1 = limit;
    per_phase.L2 = limit_L2.value_or(limit);
    per_phase.L3 = limit_L3.value_or(limit);
    per_phase.source = source;
    limits.ac_max_current_A = {std::max({per_phase.L1.value(), per_phase.L2.value(), per_phase.L3.value()}), source};
    limits.ac_max_current_per_phase_A = per_phase;
}

float total_power_limit(float limit, const std::optional<float>& limit_L2, const std::optional<float>& limit_L3) {
    if (not limit_L2.has_value() and not limit_L3.has_value()) {
        return limit;
    }
    return limit + limit_L2.value_or(limit) + limit_L3.value_or(limit);
}

} // namespace external_energy_limits
