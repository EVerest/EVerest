// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#pragma once

#include <algorithm>
#include <cmath>
#include <optional>

#include <iso15118/ev/params/dc.hpp>
#include <iso15118/message/common_types.hpp>

namespace iso15118::ev::d20 {

// The DisplayParameters of a charge loop request: the present SoC, the battery capacity and
// whether the battery is full. None until a SoC has been reported.
inline std::optional<message_20::datatypes::DisplayParameters>
make_display_parameters(const params::DcChargeParams& params) {
    if (not params.present_soc_known) {
        return std::nullopt;
    }
    const auto soc = std::clamp(std::round(params.present_soc), 0.0, 100.0);
    message_20::datatypes::DisplayParameters display;
    display.present_soc = static_cast<message_20::datatypes::PercentValue>(soc);
    display.charging_complete = soc >= 100.0;
    if (params.energy_capacity > 0.0F) {
        display.battery_energy_capacity = message_20::datatypes::from_float(params.energy_capacity);
    }
    return display;
}

} // namespace iso15118::ev::d20
