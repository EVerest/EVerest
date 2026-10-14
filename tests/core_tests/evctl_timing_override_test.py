#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright Pionix GmbH and Contributors to EVerest
"""Scenario timing override integration tests for EvController.

Exercises the `timing` field of `m2e/run_scenario` end-to-end:

1. `test_pause_resume_with_timing_override` — runs `AcIecPauseResume` with
   compressed timing (pause 5 s, resume 8 s, stop 11 s, unplug 12 s). Asserts
   the FSM reaches `Charging`, leaves it (pause), returns (resume), stays
   `Plugged` when the stop ends the session, then lands on `Unplugged` when
   the scenario's own unplug fires — all inside ~15 s wall-clock instead of
   the built-in 125 s preset.

2. `test_inapplicable_timing_override_rejected` — sends `AcIecBasic` with
   `pause_at_ms` set (AcIecBasic has no pause phase). Asserts a `Rejected`
   `command_ack` for `run_scenario` is emitted and the FSM stays `Unplugged`
   (no scenario ran).

Both tests load `config/config-sil.yaml` with the EvController adjustment (AC IEC stack, same as the
AC IEC smoke suite).
"""

import pytest

# Star-import pulls all transitive fixtures (everest_environment,
# core_config, evctl_test_controller, ...) into the test namespace.
from everest.testing.core_utils.fixtures import *  # noqa: F401,F403
from evctl_config_adjustment import (  # noqa: E402
    EvControllerConfigAdjustment,
    EVCTL_BATTERY_FULL_SESSION,
    EVCTL_DC_SESSION,
)

# Share smoke_tests.py's "ISO15118" loadgroup. The isolation plugin strips this marker
# whenever network isolation is active, which is the default and what CI runs: each
# xdist worker gets its own veth pair. So the group only binds when isolation is off.
pytestmark = pytest.mark.xdist_group(name="ISO15118")


# Compressed preset (ms): strictly increasing, all positive.
#
# The built-in AcIecPauseResume preset: pause 30 000, resume 60 000,
# stop 120 000, unplug 125 000 (total ~2 min).
# Override: pause 5 000, resume 8 000, stop 11 000, unplug 12 000 (~12 s).
# Leave time for authorization and the IEC charging transition before pausing.
# The extra per-state timeouts below absorb EVerest startup latency and
# CI scheduling jitter.
_TIMING = {
    "pause_at_ms": 5000,
    "resume_at_ms": 8000,
    "stop_after_ms": 11000,
    "unplug_after_ms": 12000,
}

_TIMEOUT_CHARGING = 30.0   # reaching Charging: AC IEC is fast, no ISO
_TIMEOUT_PAUSE = 15.0      # leaving Charging after override pause fires
_TIMEOUT_RESUME = 15.0     # returning to Charging after resume fires
_TIMEOUT_STOP = 15.0       # reaching Plugged after the stop fires
_TIMEOUT_UNPLUG = 15.0     # reaching Unplugged after the unplug fires


@pytest.mark.everest_core_config("config-sil.yaml")
@pytest.mark.everest_config_adaptions(EvControllerConfigAdjustment())
def test_pause_resume_with_timing_override(everest_core, evctl_test_controller):
    """AcIecPauseResume with compressed timing completes in ~15 s.

    The scenario dispatcher applies the `timing` overrides, so the pause,
    resume, stop, and unplug events fire at the user-supplied offsets rather
    than the built-in 30/60/120/125 s preset.
    """
    everest_core.start()

    evctl_test_controller.start()

    evctl_test_controller.run_scenario("AcIecPauseResume", timing=_TIMING)

    # --- Charging ----------------------------------------------------------
    assert evctl_test_controller.state_collector.wait_for_state(
        "Charging", timeout=_TIMEOUT_CHARGING
    ), (
        "EvController did not reach Charging after AcIecPauseResume with "
        f"timing override; saw {evctl_test_controller.state_collector.states}"
    )

    # --- Pause fires at pause_at_ms ----------------------------------------
    assert evctl_test_controller.state_collector.wait_for_state_not(
        "Charging", timeout=_TIMEOUT_PAUSE
    ), (
        "EvController did not leave Charging within the timing-override pause "
        f"window; saw {evctl_test_controller.state_collector.states}"
    )

    # --- Resume fires at resume_at_ms --------------------------------------
    assert evctl_test_controller.state_collector.wait_for_state(
        "Charging", timeout=_TIMEOUT_RESUME
    ), (
        "EvController did not resume Charging after timing-override resume; "
        f"saw {evctl_test_controller.state_collector.states}"
    )

    # --- Stop fires at stop_after_ms; the EV stays plugged ----------------
    assert evctl_test_controller.state_collector.wait_for_state(
        "Plugged", timeout=_TIMEOUT_STOP
    ), (
        "EvController did not end the session in Plugged after timing-override stop; "
        f"saw {evctl_test_controller.state_collector.states}"
    )

    # --- The scenario outlives the stop: its unplug fires at unplug_after_ms
    assert evctl_test_controller.state_collector.wait_for_state(
        "Unplugged", timeout=_TIMEOUT_UNPLUG
    ), (
        "EvController did not reach Unplugged after timing-override unplug; "
        f"saw {evctl_test_controller.state_collector.states}"
    )


@pytest.mark.everest_core_config("config-sil.yaml")
@pytest.mark.everest_config_adaptions(EvControllerConfigAdjustment())
def test_inapplicable_timing_override_rejected(everest_core, evctl_test_controller):
    """AcIecBasic with pause_at_ms is rejected; FSM stays Unplugged.

    `AcIecBasic` has no pause phase so `pause_at_ms` is inapplicable.
    The driver must surface a `Rejected` `command_ack` for
    `run_scenario` and not advance the FSM at all.
    """
    everest_core.start()

    evctl_test_controller.start()

    # AcIecBasic only consumes stop_after_ms; pause_at_ms is inapplicable.
    with pytest.raises(AssertionError, match="pause_at_ms"):
        evctl_test_controller.run_scenario(
            "AcIecBasic", timing={"pause_at_ms": 2000}
        )

    # The FSM must not have advanced: no scenario ran.
    assert evctl_test_controller.state_collector.wait_for_state(
        "Unplugged", timeout=2.0
    ), (
        "FSM left Unplugged after a rejected run_scenario — scenario must not "
        f"have run; states: {evctl_test_controller.state_collector.states}"
    )
    assert "Charging" not in evctl_test_controller.state_collector.states, (
        "FSM reached Charging after a Rejected run_scenario — scenario ran "
        "despite the inapplicable timing override"
    )
