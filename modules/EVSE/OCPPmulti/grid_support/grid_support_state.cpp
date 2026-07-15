// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include "grid_support_state.hpp"

#include <algorithm>
#include <utility>

namespace ocpp_multi {

void GridSupportState::set_capability(int32_t evse_id, const types::grid_support::DERCapability& capability) {
    capability_by_evse[evse_id] = capability;
}

std::optional<types::grid_support::DERCapability> GridSupportState::capability_for(int32_t evse_id) const {
    const auto it = capability_by_evse.find(evse_id);
    if (it == capability_by_evse.end()) {
        return std::nullopt;
    }
    return it->second;
}

void GridSupportState::unregister(int32_t evse_id) {
    capability_by_evse.erase(evse_id);
    disabled_evses.erase(evse_id);
}

void GridSupportState::set_enabled(int32_t evse_id, bool enabled) {
    if (enabled) {
        disabled_evses.erase(evse_id);
    } else {
        disabled_evses.insert(evse_id);
    }
}

void GridSupportState::set_active_directives(std::vector<types::grid_support::Directive> directives) {
    last_active_directives = std::move(directives);
}

types::grid_support::ActiveDirectiveSet GridSupportState::build_active_set(int32_t evse_id) const {
    types::grid_support::ActiveDirectiveSet set;
    set.evse_id = evse_id;

    const auto capability_it = capability_by_evse.find(evse_id);
    if (capability_it == capability_by_evse.end()) {
        return set;
    }
    if (disabled_evses.count(evse_id) != 0) {
        return set;
    }
    const auto& supported = capability_it->second.supported_types;

    for (const auto& directive : last_active_directives) {
        if (std::find(supported.begin(), supported.end(), directive.directive_type) != supported.end()) {
            set.directives.push_back(directive);
        }
    }
    return set;
}

std::vector<int32_t> GridSupportState::registered_evses() const {
    std::vector<int32_t> evses;
    evses.reserve(capability_by_evse.size());
    for (const auto& [evse_id, capability] : capability_by_evse) {
        evses.push_back(evse_id);
    }
    return evses;
}

void GridSupportState::buffer_pending_capability(int32_t evse_id,
                                                 const types::grid_support::DERCapability& capability) {
    pending_capability_by_evse[evse_id] = capability;
}

std::vector<std::pair<int32_t, types::grid_support::DERCapability>> GridSupportState::take_pending_capabilities() {
    std::vector<std::pair<int32_t, types::grid_support::DERCapability>> taken;
    taken.reserve(pending_capability_by_evse.size());
    for (auto& [evse_id, capability] : pending_capability_by_evse) {
        taken.emplace_back(evse_id, std::move(capability));
    }
    pending_capability_by_evse.clear();
    return taken;
}

void GridSupportState::buffer_pending_alarm(const types::grid_support::GridAlarm& alarm) {
    pending_alarms.push_back(alarm);
}

std::vector<types::grid_support::GridAlarm> GridSupportState::take_pending_alarms() {
    std::vector<types::grid_support::GridAlarm> taken;
    taken.swap(pending_alarms);
    return taken;
}

bool GridSupportState::capabilities_live() const {
    return capabilities_are_live;
}

void GridSupportState::set_capabilities_live() {
    capabilities_are_live = true;
}

bool GridSupportState::alarms_live() const {
    return alarms_are_live;
}

void GridSupportState::set_alarms_live() {
    alarms_are_live = true;
}

} // namespace ocpp_multi
