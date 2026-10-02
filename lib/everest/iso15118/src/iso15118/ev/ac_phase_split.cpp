// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#include <iso15118/ev/ac_phase_split.hpp>

namespace iso15118::ev {

namespace dt = message_20::datatypes;

AcPhaseLimits split_ac_limit(float total, uint8_t phase_count, dt::AcConnector connector) {
    // The domain is 1 or 3; anything else rounds up so a line is never overstated.
    const auto lines = (phase_count >= 2) ? 3u : 1u;

    if (connector != dt::AcConnector::ThreePhase) {
        return {total / static_cast<float>(lines), std::nullopt, std::nullopt};
    }

    if (lines == 3u) {
        return {total, std::nullopt, std::nullopt};
    }

    return {total, 0.0f, 0.0f};
}

} // namespace iso15118::ev
