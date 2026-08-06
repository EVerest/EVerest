// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#include <iso15118/ev/ac_phase_split.hpp>

namespace iso15118::ev {

namespace dt = message_20::datatypes;

AcPhaseLimits split_ac_limit(float total, uint8_t phase_count, dt::AcConnector connector) {
    const auto lines = (phase_count == 0) ? 1u : static_cast<unsigned>(phase_count);
    const auto per_line = total / static_cast<float>(lines);

    if (connector != dt::AcConnector::ThreePhase) {
        return {per_line, std::nullopt, std::nullopt};
    }

    // Peers present, so the base element now means L1. A single-phase EV draws on L1 only, which
    // is exactly what zeroed peers say; a three-phase EV spreads evenly.
    const auto peer = (lines == 3u) ? per_line : 0.0f;
    return {per_line, peer, peer};
}

} // namespace iso15118::ev
