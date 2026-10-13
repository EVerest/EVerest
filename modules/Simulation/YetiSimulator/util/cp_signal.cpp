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

bool is_diode_fault(bool pwm_running, bool latched, double cp_hi, double cp_lo) {
    if (is_voltage_in_range(cp_lo, -12.0)) {
        return false;
    }
    // Both samples near 0V is a CP-PE short (State E), not a diode fault.
    if (is_voltage_in_range(cp_lo, 0.0) and is_voltage_in_range(cp_hi, 0.0)) {
        return false;
    }
    // +12V is an open circuit (State A): the car is gone.
    if (is_voltage_in_range(cp_hi, 12.0)) {
        return false;
    }
    if (not pwm_running) {
        return latched;
    }
    // Mirrored halves (cp_hi + cp_lo == 0) indicate a missing diode negative half.
    return is_voltage_in_range(cp_hi + cp_lo, 0.0);
}

DiodeFaultLatch::Action DiodeFaultLatch::step(bool pwm_running, double cp_hi, double cp_lo, bool active) {
    const bool fault = is_diode_fault(pwm_running, m_raised, cp_hi, cp_lo);
    if (fault and not active) {
        m_raised = true;
        return Action::Raise;
    }
    if (not fault and m_raised) {
        m_raised = false;
        return active ? Action::Clear : Action::None;
    }
    return Action::None;
}

} // namespace module::cp_signal
