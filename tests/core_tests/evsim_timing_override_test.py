#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright Pionix GmbH and Contributors to EVerest
"""Scenario timing override integration tests for EvSimulator.

Exercises the `timing` field of `m2e/run_scenario` end-to-end:

1. `test_pause_resume_with_timing_override` — runs `AcIecPauseResume` with
   compressed timing (pause 2 s, resume 4 s, stop 6 s, unplug 7 s). Asserts
   the FSM reaches `Charging`, leaves it (pause), returns (resume), then
   lands on `Unplugged` — all inside ~15 s wall-clock instead of the
   built-in 125 s preset.

2. `test_inapplicable_timing_override_rejected` — sends `AcIecBasic` with
   `pause_at_ms` set (AcIecBasic has no pause phase). Asserts a `Rejected`
   `command_ack` for `run_scenario` is emitted and the FSM stays `Unplugged`
   (no scenario ran).

Both tests load `config/config-sil-evsim.yaml` (AC IEC stack, same as the
AC IEC smoke suite). The WS-B dispatcher must be installed before invoking
pytest; the compressed timing is validated only once the deployment is
current with the WS-A/WS-B branch changes.
"""

import pytest

# Star-import pulls all transitive fixtures (everest_environment,
# core_config, evsim_test_controller, ...) into the test namespace.
from everest.testing.core_utils.fixtures import *  # noqa: F401,F403

# Share smoke_tests.py's "ISO15118" loadgroup so --dist=loadgroup keeps
# every EvSimulator SIL test on one xdist worker. Concurrent SIL stacks
# contend for CPU and flake pause/resume teardown; the group name must
# match smoke_tests.py or the suites still race.
pytestmark = pytest.mark.xdist_group(name="ISO15118")


# Compressed preset (ms) — strictly increasing, all positive.
#
# The built-in AcIecPauseResume preset: pause 30 000, resume 60 000,
# stop 120 000, unplug 125 000 (total ~2 min).
# Override: pause 2 000, resume 4 000, stop 6 000, unplug 7 000 (~7 s).
# The extra per-state timeouts below absorb EVerest startup latency and
# CI scheduling jitter.
_TIMING = {
    "pause_at_ms": 2000,
    "resume_at_ms": 4000,
    "stop_after_ms": 6000,
    "unplug_after_ms": 7000,
}

_TIMEOUT_CHARGING = 30.0   # reaching Charging: AC IEC is fast, no ISO
_TIMEOUT_PAUSE = 15.0      # leaving Charging after override pause fires
_TIMEOUT_RESUME = 15.0     # returning to Charging after resume fires
_TIMEOUT_UNPLUG = 15.0     # reaching Unplugged after stop + unplug fires
_TIMEOUT_REJECT_ACK = 10.0 # command_ack for a rejected run_scenario


@pytest.mark.everest_core_config("config-sil-evsim.yaml")
def test_pause_resume_with_timing_override(everest_core, evsim_test_controller):
    """AcIecPauseResume with compressed timing completes in ~15 s.

    The scenario dispatcher applies the `timing` overrides, so the pause,
    resume, stop, and unplug events fire at the user-supplied offsets rather
    than the built-in 30/60/120/125 s preset.
    """
    everest_core.start()

    evsim_test_controller.start()

    evsim_test_controller.run_scenario("AcIecPauseResume", timing=_TIMING)

    # --- Charging ----------------------------------------------------------
    assert evsim_test_controller.state_collector.wait_for_state(
        "Charging", timeout=_TIMEOUT_CHARGING
    ), (
        "EvSimulator did not reach Charging after AcIecPauseResume with "
        f"timing override; saw {evsim_test_controller.state_collector.states}"
    )

    # --- Pause fires at pause_at_ms ----------------------------------------
    assert evsim_test_controller.state_collector.wait_for_state_not(
        "Charging", timeout=_TIMEOUT_PAUSE
    ), (
        "EvSimulator did not leave Charging within the timing-override pause "
        f"window; saw {evsim_test_controller.state_collector.states}"
    )

    # --- Resume fires at resume_at_ms --------------------------------------
    assert evsim_test_controller.state_collector.wait_for_state(
        "Charging", timeout=_TIMEOUT_RESUME
    ), (
        "EvSimulator did not resume Charging after timing-override resume; "
        f"saw {evsim_test_controller.state_collector.states}"
    )

    # --- Stop + unplug fires at stop_after_ms / unplug_after_ms -----------
    assert evsim_test_controller.state_collector.wait_for_state(
        "Unplugged", timeout=_TIMEOUT_UNPLUG
    ), (
        "EvSimulator did not reach Unplugged after timing-override stop/unplug; "
        f"saw {evsim_test_controller.state_collector.states}"
    )


@pytest.mark.everest_core_config("config-sil-evsim.yaml")
def test_inapplicable_timing_override_rejected(everest_core, evsim_test_controller):
    """AcIecBasic with pause_at_ms is rejected; FSM stays Unplugged.

    `AcIecBasic` has no pause phase so `pause_at_ms` is inapplicable.
    WS-B validation must surface a `Rejected` `command_ack` for
    `run_scenario` and not advance the FSM at all.
    """
    everest_core.start()

    evsim_test_controller.start()

    # AcIecBasic only consumes stop_after_ms; pause_at_ms is inapplicable.
    evsim_test_controller.run_scenario(
        "AcIecBasic", timing={"pause_at_ms": 2000}
    )

    ack = evsim_test_controller.state_collector.wait_for_command_ack(
        "run_scenario", "Rejected", timeout=_TIMEOUT_REJECT_ACK
    )
    assert ack is not None, (
        "Expected a Rejected command_ack for run_scenario with inapplicable "
        f"pause_at_ms override on AcIecBasic; "
        f"acks seen: {evsim_test_controller.state_collector.command_acks}"
    )
    assert "pause_at_ms" in ack.get("reason", ""), (
        f"Rejection reason did not mention the inapplicable field: {ack}"
    )

    # The FSM must not have advanced — no scenario ran.
    assert evsim_test_controller.state_collector.wait_for_state(
        "Unplugged", timeout=2.0
    ), (
        "FSM left Unplugged after a rejected run_scenario — scenario must not "
        f"have run; states: {evsim_test_controller.state_collector.states}"
    )
    assert "Charging" not in evsim_test_controller.state_collector.states, (
        "FSM reached Charging after a Rejected run_scenario — scenario ran "
        "despite the inapplicable timing override"
    )
