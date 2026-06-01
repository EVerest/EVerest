// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include "cp_signal.hpp"

namespace module::cp_signal {

namespace {
bool is_voltage_in_range(double voltage, double center) {
    constexpr auto interval = 1.1;
    return voltage > center - interval and voltage < center + interval;
}
} // namespace

bool is_diode_fault(bool pwm_running, double cp_hi, double cp_lo) {
    // Only meaningful while PWM is running with a present negative half.
    if (not pwm_running or is_voltage_in_range(cp_lo, -12.0)) {
        return false;
    }
    // Both samples near 0V is a CP-PE short (State E), not a diode fault.
    if (is_voltage_in_range(cp_lo, 0.0) and is_voltage_in_range(cp_hi, 0.0)) {
        return false;
    }
    // Mirrored halves (cp_hi + cp_lo == 0) indicate a missing diode negative half.
    return is_voltage_in_range(cp_hi + cp_lo, 0.0);
}

} // namespace module::cp_signal
