#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright Pionix GmbH and Contributors to EVerest
"""AC IEC 61851 session smokes driven by the EvSimulator typed API.

The AC IEC charge mode takes a different FSM route than the DC/ISO
smokes: no V2G negotiation (no SLAC match, no ISO 15118), so the path
is plug -> Unplugged -> Plugged -> Charging straight through the BSP
CP/PWM handshake. The DC ISO-2 / D20 / BPT / MCS / curve smokes never
exercise this branch, so it gets its own coverage here.

Each test loads `config/config-sil-evsim.yaml` (the AC IEC SIL config,
same one used by `evsimulator_smoke_test.py`), which wires EvSimulator
into an AC EvseManager stack.

Runtime gating: the deployed EVerest manager binary must be current
with the EvSimulator changes on this branch. Re-run
`ninja -C build install` before invoking pytest; otherwise the manager
loads a stale EvSimulator that does not match the controller's typed
API and the tests hang waiting for `e2m/state` publications.
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


# AC IEC has no SLAC/TLS/ISO 15118 handshake, so reaching Charging is
# fast relative to the DC ISO smokes; the timeouts stay generous to
# absorb CI scheduling jitter.
_TIMEOUT_CHARGING = 30.0
_TIMEOUT_FINISH = 30.0


@pytest.mark.everest_core_config("config-sil-evsim.yaml")
def test_ac_iec_basic_session(everest_core, evsim_test_controller):
    """AcIecBasic: plug in, reach Charging via the BSP path, then unplug."""
    everest_core.start()

    evsim_test_controller.start()
    # plug_in() publishes configure_session{mode: AcIec} + plug and blocks
    # until the FSM reports Charging (or its internal timeout elapses).
    evsim_test_controller.plug_in()

    assert evsim_test_controller.state_collector.wait_for_state(
        "Charging", timeout=_TIMEOUT_CHARGING
    ), (
        "EvSimulator did not reach Charging under AcIecBasic; "
        f"saw {evsim_test_controller.state_collector.states}"
    )

    # Reaching Charging is not enough: a regression that lands in
    # Charging at 0 A would pass a state-only check. Require the SoC
    # integrator to actually advance.
    assert evsim_test_controller.state_collector.wait_for_soc_progress(
        min_increase=0.01, timeout=_TIMEOUT_CHARGING
    ), "SoC did not advance while in Charging under AcIecBasic"

    evsim_test_controller.plug_out()

    assert evsim_test_controller.state_collector.wait_for_state(
        "Unplugged", timeout=_TIMEOUT_FINISH
    ), (
        "EvSimulator did not return to Unplugged after unplug; "
        f"saw {evsim_test_controller.state_collector.states}"
    )


@pytest.mark.everest_core_config("config-sil-evsim.yaml")
def test_ac_iec_pause_resume(everest_core, evsim_test_controller):
    """AcIecPauseResume: charge, pause, resume, then unplug.

    Mirrors the DC ISO-2 pause/resume smoke but on the AC IEC route,
    which leaves Charging on a PWM pause rather than an ISO 15118
    pause-from-charger.
    """
    everest_core.start()

    evsim_test_controller.start()
    evsim_test_controller.plug_in()

    assert evsim_test_controller.state_collector.wait_for_state(
        "Charging", timeout=_TIMEOUT_CHARGING
    ), (
        "EvSimulator did not reach Charging before pause; "
        f"saw {evsim_test_controller.state_collector.states}"
    )

    evsim_test_controller.pause_session()

    assert evsim_test_controller.state_collector.wait_for_state_not(
        "Charging", timeout=_TIMEOUT_FINISH
    ), (
        "EvSimulator stayed in Charging after AC IEC pause; "
        f"saw {evsim_test_controller.state_collector.states}"
    )

    evsim_test_controller.resume_session()

    assert evsim_test_controller.state_collector.wait_for_state(
        "Charging", timeout=_TIMEOUT_CHARGING
    ), (
        "EvSimulator did not resume Charging after AC IEC resume; "
        f"saw {evsim_test_controller.state_collector.states}"
    )

    evsim_test_controller.plug_out()

    assert evsim_test_controller.state_collector.wait_for_state(
        "Unplugged", timeout=_TIMEOUT_FINISH
    ), (
        "EvSimulator did not return to Unplugged after unplug; "
        f"saw {evsim_test_controller.state_collector.states}"
    )
