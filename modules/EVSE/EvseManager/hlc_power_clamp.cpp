// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include "hlc_power_clamp.hpp"

namespace module {

HlcPowerClampResult clamp_hlc_power(double requested_current, double target_voltage,
                                    std::optional<double> present_voltage, double maximum_power) {
    const auto measured = present_voltage.value_or(0.0);
    const auto voltage = measured > 0.0 ? measured : target_voltage;

    if (voltage <= 0.0) {
        return {requested_current, false};
    }

    if (requested_current * voltage > maximum_power) {
        return {maximum_power / voltage, true};
    }

    return {requested_current, false};
}

} // namespace module
