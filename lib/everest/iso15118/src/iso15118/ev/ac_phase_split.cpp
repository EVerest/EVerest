// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#include <algorithm>

#include <iso15118/ev/ac_phase_split.hpp>

namespace iso15118::ev {

namespace dt = message_20::datatypes;

AcPhaseLimits split_ac_limit(float total, uint8_t phase_count, dt::AcConnector connector) {
    // No AC connector offers more than three lines, and a zero count would divide by zero.
    const auto lines = std::clamp<unsigned>(phase_count, 1u, 3u);
    const auto per_line = total / static_cast<float>(lines);

    if (connector != dt::AcConnector::ThreePhase) {
        return {per_line, std::nullopt, std::nullopt};
    }

    // Peers present, so the base element now means L1 and each line must carry its own share.
    // The EV draws on as many lines as it has and nothing on the rest; zeroing both peers
    // whenever the count is not three drops everything past L1 from the advertised total.
    const auto l2 = (lines >= 2u) ? per_line : 0.0f;
    const auto l3 = (lines >= 3u) ? per_line : 0.0f;
    return {per_line, l2, l3};
}

} // namespace iso15118::ev
