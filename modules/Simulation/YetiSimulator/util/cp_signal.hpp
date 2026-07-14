// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#pragma once

namespace module::cp_signal {

/// Decides whether the simulated control-pilot signal currently represents a
/// diode fault. A shorted diode removes the CP negative half, so the simulator
/// mirrors the positive level onto the low sample (cpLo == -cpHi); while PWM is
/// running and the car is connected this makes cpHi + cpLo == 0.
///
/// An injected diode fault (injected_fault) latches the result across PWM-off: a
/// mirrored signal is reported as a fault when PWM is running OR an injection is
/// active, so a raised DiodeFault survives EvseManager stopping the PWM in
/// response. Returns false when both samples are near 0V (a CP-PE short / State
/// E) and once the negative half is restored to its nominal -12V level (the
/// genuine-recovery path), so the caller can clear a previously raised DiodeFault.
bool is_diode_fault(bool pwm_running, bool injected_fault, double cp_hi, double cp_lo);

} // namespace module::cp_signal
