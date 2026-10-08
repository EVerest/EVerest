// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#include "grid_event.hpp"

namespace module {

std::optional<types::grid_support::GridEventFault> map_grid_event_condition(uint8_t condition) {
    if (condition == 0) {
        return std::nullopt;
    }
    return types::grid_support::GridEventFault::LocalEmergency;
}

EdgeResult GridEventEdgeDetector::peek(uint8_t condition) const {
    const auto incoming = map_grid_event_condition(condition);

    if (incoming.has_value()) {
        if (active_fault.has_value()) {
            // Already in fault. A repeated or differing nonzero code is treated as
            // still-in-fault: deduplicate and keep the original active fault.
            return EdgeResult::none();
        }
        return EdgeResult::rising(incoming.value());
    }

    if (active_fault.has_value()) {
        return EdgeResult::falling(active_fault.value());
    }

    return EdgeResult::none();
}

void GridEventEdgeDetector::commit(uint8_t condition) {
    const auto incoming = map_grid_event_condition(condition);

    if (incoming.has_value()) {
        if (not active_fault.has_value()) {
            active_fault = incoming;
        }
    } else {
        active_fault.reset();
    }
}

void GridEventEdgeDetector::reset() {
    active_fault.reset();
}

} // namespace module
