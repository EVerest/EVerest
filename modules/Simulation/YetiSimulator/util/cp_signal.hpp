// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#pragma once

namespace module::cp_signal {

/// Decides whether the simulated control-pilot signal currently represents a
/// diode fault. A shorted diode removes the CP negative half, so the simulator
/// mirrors the positive level onto the low sample (cpLo == -cpHi); while PWM is
/// running and the car is connected this makes cpHi + cpLo == 0.
///
/// Without PWM there is no negative half to test, so the result is `latched`,
/// the fault seen under PWM, as on the real board. Returns false for a CP-PE
/// short (State E), an open circuit (State A) and a nominal -12 V negative half.
bool is_diode_fault(bool pwm_running, bool latched, double cp_hi, double cp_lo);

/// Raises a DiodeFault seen under PWM and clears only the one it raised, so a
/// DiodeFault raised through the error API stays set until disconnect.
class DiodeFaultLatch {
public:
    enum class Action {
        None,
        Raise,
        Clear
    };
    /// `active` is whether board support reports a DiodeFault, whoever raised it.
    Action step(bool pwm_running, double cp_hi, double cp_lo, bool active);

private:
    bool m_raised{false};
};

} // namespace module::cp_signal
