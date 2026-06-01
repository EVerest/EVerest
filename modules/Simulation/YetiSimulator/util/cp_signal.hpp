// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#pragma once

namespace module::cp_signal {

/// Decides whether the simulated control-pilot signal currently represents a
/// diode fault. A shorted diode removes the CP negative half, so the simulator
/// mirrors the positive level onto the low sample (cpLo == -cpHi); while PWM is
/// running and the car is connected this makes cpHi + cpLo == 0.
///
/// Returns false when PWM is off, when both samples are near 0V (a CP-PE short /
/// State E), and once the negative half is restored to its nominal level, so the
/// caller can clear a previously raised DiodeFault.
bool is_diode_fault(bool pwm_running, double cp_hi, double cp_lo);

} // namespace module::cp_signal
