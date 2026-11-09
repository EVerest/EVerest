// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include "../util/cp_signal.hpp"

#include <catch2/catch_test_macros.hpp>

namespace {

using module::cp_signal::DiodeFaultLatch;
using module::cp_signal::is_diode_fault;
using Action = DiodeFaultLatch::Action;

} // namespace

// A real shorted diode removes the CP negative half, so the simulator mirrors the
// positive level onto the low sample (cpLo = -cpHi). While PWM is running and the
// car is connected (State B/C/D), that makes cpHi + cpLo == 0, which must be read
// as a diode fault.
TEST_CASE("diode fault is detected for a connected car with mirrored CP halves", "[yetisim][diode]") {
    CHECK(is_diode_fault(/*pwm_running=*/true, /*latched=*/false, /*cpHi=*/9.0, /*cpLo=*/-9.0));
    CHECK(is_diode_fault(/*pwm_running=*/true, /*latched=*/false, /*cpHi=*/6.0, /*cpLo=*/-6.0));
}

TEST_CASE("diode fault clears once the negative CP half is restored", "[yetisim][diode]") {
    CHECK_FALSE(is_diode_fault(/*pwm_running=*/true, /*latched=*/false, /*cpHi=*/9.0, /*cpLo=*/-12.0));
    CHECK_FALSE(is_diode_fault(/*pwm_running=*/true, /*latched=*/true, /*cpHi=*/6.0, /*cpLo=*/-12.0));
}

TEST_CASE("CP-PE short is not reported as a diode fault", "[yetisim][diode]") {
    CHECK_FALSE(is_diode_fault(/*pwm_running=*/true, /*latched=*/false, /*cpHi=*/0.0, /*cpLo=*/0.0));
    CHECK_FALSE(is_diode_fault(/*pwm_running=*/false, /*latched=*/true, /*cpHi=*/0.0, /*cpLo=*/0.0));
}

// The diode is only tested by the PWM negative half, as on the real board.
TEST_CASE("a mirrored sample before any PWM raises nothing", "[yetisim][diode]") {
    DiodeFaultLatch latch;
    CHECK(latch.step(/*pwm_running=*/false, /*cpHi=*/9.0, /*cpLo=*/-9.0, /*active=*/false) == Action::None);
    CHECK(latch.step(/*pwm_running=*/true, /*cpHi=*/9.0, /*cpLo=*/-9.0, /*active=*/false) == Action::Raise);
}

// EvseManager stops the PWM in response to the raised fault; the fault stays
// until the negative half is restored.
TEST_CASE("a raised diode fault stays latched while PWM is off", "[yetisim][diode]") {
    DiodeFaultLatch latch;
    REQUIRE(latch.step(true, 9.0, -9.0, false) == Action::Raise);
    CHECK(latch.step(false, 9.0, -9.0, true) == Action::None);
    // Without PWM there is no negative half to test the diode with.
    CHECK(latch.step(false, 9.0, 9.0, true) == Action::None);
    CHECK(latch.step(true, 9.0, -12.0, true) == Action::Clear);
    CHECK(latch.step(true, 9.0, -12.0, false) == Action::None);
}

TEST_CASE("a raised diode fault clears on unplug", "[yetisim][diode]") {
    DiodeFaultLatch latch;
    REQUIRE(latch.step(true, 9.0, -9.0, false) == Action::Raise);
    CHECK(latch.step(false, 12.0, 12.0, true) == Action::Clear);
}

TEST_CASE("a raised diode fault clears on a CP-PE short", "[yetisim][diode]") {
    DiodeFaultLatch latch;
    REQUIRE(latch.step(true, 9.0, -9.0, false) == Action::Raise);
    CHECK(latch.step(true, 0.0, 0.0, true) == Action::Clear);
}

// A DiodeFault raised through the error API is not the simulator's to clear.
TEST_CASE("a diode fault the simulator did not raise is left alone", "[yetisim][diode]") {
    DiodeFaultLatch latch;
    CHECK(latch.step(true, 9.0, -12.0, true) == Action::None);
    CHECK(latch.step(false, 9.0, 9.0, true) == Action::None);
    CHECK(latch.step(false, 12.0, 12.0, true) == Action::None);
}

// A fault the simulator detects while another raise is active is not claimed,
// so restoring the signal does not clear the other raise.
TEST_CASE("a detected fault already raised elsewhere is not claimed", "[yetisim][diode]") {
    DiodeFaultLatch latch;
    CHECK(latch.step(true, 9.0, -9.0, true) == Action::None);
    CHECK(latch.step(true, 9.0, -12.0, true) == Action::None);
}
