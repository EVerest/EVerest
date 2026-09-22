// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#pragma once

#include <cstdint>
#include <optional>

#include <iso15118/message/common_types.hpp>

namespace iso15118::ev::d20 {

// start_charging's request as a message sent now states it.
struct EnergyRequest {
    // Seconds left until departure, above 0 ([V2G20-2103]); unset when not stated or passed.
    std::optional<std::uint32_t> departure_time{std::nullopt};
    // EAmount in Wh; unset when not stated.
    std::optional<float> energy_wh{std::nullopt};
    // Dynamic mode's mandatory target and maximum energy when EAmount is not stated.
    float energy_capacity{0.0f};

    float dynamic_energy_wh() const {
        return energy_wh.value_or(energy_capacity);
    }
};

// A Dynamic charge-loop request's energy window and departure time; the minimum stays 0.
inline void fill_energy_request(message_20::datatypes::Dynamic_CLReqControlMode& mode, const EnergyRequest& request) {
    const auto energy = message_20::datatypes::from_float(request.dynamic_energy_wh());
    mode.departure_time = request.departure_time;
    mode.target_energy_request = energy;
    mode.max_energy_request = energy;
    mode.min_energy_request = {0, 0};
}

} // namespace iso15118::ev::d20
