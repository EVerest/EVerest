// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#include "energyImpl.hpp"

#include <mutex>

namespace module {
namespace energy_grid {

void energyImpl::init() {
    // Nothing to initialise here — the module-level init() handles child subscriptions.
}

void energyImpl::ready() {
    // Nothing to do — module-level ready() handles MQTT subscriptions and initial publish.
}

void energyImpl::handle_enforce_limits(types::energy::EnforcedLimits& value) {
    // This is called by the internal EnergyManager via the Everest internal bus.
    // Apply internal limits ONLY when the external EnergyManager is not currently
    // in control (i.e. the watchdog has fired since its last message).
    //
    // The state check and the forwarding happen under the same lock as the
    // external MQTT path: without it, this thread could pass the check, get
    // preempted by an external message that re-takes control and forwards its
    // limits, and then still forward the now-stale internal limits afterwards
    // (TOCTOU) — whichever arrives last would win at the EVSEs.
    std::lock_guard<std::mutex> lock(mod->forwarding_mutex);

    if (mod->external_active) {
        // External EnergyManager is in control: its limits are applied by the MQTT
        // subscriber in external_energy_node_API.cpp; discard internal limits.
        return;
    }

    for (auto& entry : mod->r_energy_consumer) {
        entry->call_enforce_limits(value);
    }
}

} // namespace energy_grid
} // namespace module
