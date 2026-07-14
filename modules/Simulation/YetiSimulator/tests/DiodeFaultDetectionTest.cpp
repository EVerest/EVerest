// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include "../util/cp_signal.hpp"

#include <catch2/catch_test_macros.hpp>

namespace {

using module::cp_signal::is_diode_fault;

} // namespace

// A real shorted diode removes the CP negative half, so the simulator mirrors the
// positive level onto the low sample (cpLo = -cpHi). While PWM is running and the
// car is connected (State B/C/D), that makes cpHi + cpLo == 0, which must be read
// as a diode fault.
TEST_CASE("diode fault is detected for a connected car with mirrored CP halves", "[yetisim][diode]") {
    // State B connected: cpHi ~ 9.0, mirrored low half -> diode fault present.
    CHECK(is_diode_fault(/*pwm_running=*/true, /*injected_fault=*/false, /*cpHi=*/9.0, /*cpLo=*/-9.0));
    // State C connected: cpHi ~ 6.0, mirrored low half.
    CHECK(is_diode_fault(/*pwm_running=*/true, /*injected_fault=*/false, /*cpHi=*/6.0, /*cpLo=*/-6.0));
}

// When the EV peer clears diode_fail (call_diode_fail(false)) but the car stays
// plugged in, the negative half returns to its nominal -12V and the fault must
// be reported as gone so the raised DiodeFault gets cleared.
TEST_CASE("diode fault clears once the negative CP half is restored", "[yetisim][diode]") {
    // State B connected, no diode fault: nominal negative half at -12V.
    CHECK_FALSE(is_diode_fault(/*pwm_running=*/true, /*injected_fault=*/false, /*cpHi=*/9.0, /*cpLo=*/-12.0));
    // State C connected, no diode fault.
    CHECK_FALSE(is_diode_fault(/*pwm_running=*/true, /*injected_fault=*/false, /*cpHi=*/6.0, /*cpLo=*/-12.0));
}

// A genuine CP-PE short (both samples near 0V) is State E, not a diode fault.
TEST_CASE("CP-PE short is not reported as a diode fault", "[yetisim][diode]") {
    CHECK_FALSE(is_diode_fault(/*pwm_running=*/true, /*injected_fault=*/false, /*cpHi=*/0.0, /*cpLo=*/0.0));
    // A genuine State-E short takes precedence over a still-set injection: it
    // must not be masked as a (mirrored) diode fault by the latch.
    CHECK_FALSE(is_diode_fault(/*pwm_running=*/false, /*injected_fault=*/true, /*cpHi=*/0.0, /*cpLo=*/0.0));
}

// An injected diode fault latches: EvseManager stops the PWM in response to the
// raised fault, but the fault must stay asserted (not clear merely because PWM
// went off) so downstream logic (e.g. a reservation) still observes it.
TEST_CASE("injected diode fault stays latched while PWM is off", "[yetisim][diode]") {
    CHECK(is_diode_fault(/*pwm_running=*/false, /*injected_fault=*/true, /*cpHi=*/9.0, /*cpLo=*/-9.0));
    CHECK(is_diode_fault(/*pwm_running=*/false, /*injected_fault=*/true, /*cpHi=*/6.0, /*cpLo=*/-6.0));
}

// Without an injection, a PWM-off mirrored sample is not a diode fault: there is
// no driving signal to evaluate and nothing latched.
TEST_CASE("PWM-off mirrored sample without injection is not a fault", "[yetisim][diode]") {
    CHECK_FALSE(is_diode_fault(/*pwm_running=*/false, /*injected_fault=*/false, /*cpHi=*/9.0, /*cpLo=*/-9.0));
}

// The latched fault clears once the negative CP half is genuinely restored to
// nominal (-12V) — the genuine-recovery path — even if the injected flag is set.
TEST_CASE("injected diode fault clears when the negative CP half is restored", "[yetisim][diode]") {
    CHECK_FALSE(is_diode_fault(/*pwm_running=*/false, /*injected_fault=*/true, /*cpHi=*/9.0, /*cpLo=*/-12.0));
    CHECK_FALSE(is_diode_fault(/*pwm_running=*/true, /*injected_fault=*/false, /*cpHi=*/9.0, /*cpLo=*/-12.0));
}
