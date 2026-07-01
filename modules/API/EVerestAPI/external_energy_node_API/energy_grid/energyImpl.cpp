// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#include "energyImpl.hpp"

namespace module {
namespace energy_grid {

void energyImpl::init() {
    // Nothing to initialise here — the module-level init() handles child subscriptions.
}

void energyImpl::ready() {
    // Nothing to do — module-level ready() handles MQTT subscriptions and initial publish.
}

void energyImpl::handle_enforce_limits(types::energy::EnforcedLimits& value) {
    // This is called by the local L0 EnergyManager via the Everest internal bus.
    // Apply L0 limits ONLY when the external L1 EnergyManager is not currently active.
    // When L1 is active, its enforce_limits (received via external MQTT in the module's
    // ready() callback) take priority and L0 limits are discarded here.
    if (!mod->external_active.load()) {
        for (auto& entry : mod->r_energy_consumer) {
            entry->call_enforce_limits(value);
        }
    }
    // If external_active is true: L1 is in control, silently discard L0's limits.
    // L1 limits are already being applied by the MQTT subscriber in external_energy_node_API.cpp.
}

} // namespace energy_grid
} // namespace module
