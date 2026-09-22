// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#pragma once

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <limits>
#include <optional>

namespace iso15118::ev::params {

/**
 * start_charging's EAmount and DepartureTime. The request is static: energy delivered during the
 * session is not subtracted from it.
 */
struct EnergyRequest {
    // Wh. 0: not stated.
    float energy_wh{0.0f};
    // Unset: not stated.
    std::optional<std::chrono::steady_clock::time_point> departure{std::nullopt};

    std::optional<float> stated_energy_wh() const {
        return energy_wh > 0.0f ? std::make_optional(energy_wh) : std::nullopt;
    }

    // Whole seconds left until departure at \p now, clamped at 0; unset when not stated.
    std::optional<std::uint32_t> departure_in(std::chrono::steady_clock::time_point now) const {
        if (not departure.has_value()) {
            return std::nullopt;
        }
        const auto left = std::chrono::duration_cast<std::chrono::seconds>(*departure - now).count();
        return static_cast<std::uint32_t>(
            std::clamp<decltype(left)>(left, 0, std::numeric_limits<std::uint32_t>::max()));
    }
};

// Caps \p energy_wh at \p max_wh; true when it was above.
inline bool cap_energy(float& energy_wh, float max_wh) {
    if (not(energy_wh > max_wh)) {
        return false;
    }
    energy_wh = max_wh;
    return true;
}

} // namespace iso15118::ev::params
