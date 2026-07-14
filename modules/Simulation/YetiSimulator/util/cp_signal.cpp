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

bool is_diode_fault(bool pwm_running, bool injected_fault, double cp_hi, double cp_lo) {
    // A nominal negative half (cp_lo ~ -12V) means no fault. This is the
    // genuine-recovery path: when the EV peer drops diode_fail the negative
    // half returns to -12V and the fault clears regardless of pwm/injection.
    if (is_voltage_in_range(cp_lo, -12.0)) {
        return false;
    }
    // Both samples near 0V is a CP-PE short (State E), not a diode fault.
    if (is_voltage_in_range(cp_lo, 0.0) and is_voltage_in_range(cp_hi, 0.0)) {
        return false;
    }
    // Mirrored halves (cp_hi + cp_lo == 0) indicate a missing diode negative half.
    if (not is_voltage_in_range(cp_hi + cp_lo, 0.0)) {
        return false;
    }
    // A mirrored signal is a diode fault while PWM is running. An injected fault
    // latches: it stays asserted even after EvseManager stops the PWM in
    // response, so it survives until the negative half is genuinely restored
    // (the cp_lo ~ -12V early return above).
    return pwm_running or injected_fault;
}

} // namespace module::cp_signal
