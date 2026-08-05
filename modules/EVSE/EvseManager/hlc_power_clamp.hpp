// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#pragma once

#include <optional>

namespace module {

struct HlcPowerClampResult {
    double current;
    bool limit_exceeded;
};

/**
 * @brief Reduce @p requested_current so the resulting power stays within @p maximum_power.
 *
 * @param requested_current [A] what the EV asked for, already clamped against current limits.
 * @param target_voltage [V] the voltage the session is working towards, used when the EV has
 *        reported no usable measurement.
 * @param present_voltage [V] what the EV reports measuring, if anything.
 * @param maximum_power [W] what the EVSE can deliver.
 *
 * A present voltage of zero or below is treated as no measurement rather than as a measured
 * zero. Taking it literally makes the computed power zero, so the limit can never be exceeded
 * and the clamp silently stops working for the whole session. With no usable voltage from
 * either source there is no power to compare, so the request passes through unchanged.
 */
HlcPowerClampResult clamp_hlc_power(double requested_current, double target_voltage,
                                    std::optional<double> present_voltage, double maximum_power);

} // namespace module
