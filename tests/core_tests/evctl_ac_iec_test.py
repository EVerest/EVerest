#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright Pionix GmbH and Contributors to EVerest
"""AC IEC 61851 session smokes driven by the EvController typed API.

The AC IEC charge mode takes a different FSM route than the DC/ISO
smokes: no V2G negotiation (no SLAC match, no ISO 15118), so the path
is plug -> Unplugged -> Plugged -> Charging straight through the BSP
CP/PWM handshake. The DC ISO-2 / D20 / BPT / MCS / curve smokes never
exercise this branch, so it gets its own coverage here.

Each test loads `config/config-sil.yaml` with the EvController adjustment
applied, which wires EvController into an AC EvseManager stack.

Runtime gating: the deployed EVerest manager binary must be current
with the EvController changes on this branch. Re-run
`ninja -C build install` before invoking pytest; otherwise the manager
loads a stale EvController that does not match the controller's typed
API and the tests hang waiting for `e2m/state` publications.
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


# AC IEC has no SLAC/TLS/ISO 15118 handshake, so reaching Charging is
# fast relative to the DC ISO smokes; the timeouts stay generous to
# absorb CI scheduling jitter.
_TIMEOUT_CHARGING = 30.0
_TIMEOUT_FINISH = 30.0


# A plug with nothing latched is the only path that exercises the FSM synthesizing a session from
# the module's own config defaults. Every plug_in* helper configures first, so without this the
# default path has no coverage at all and a change to those defaults would be silent.
@pytest.mark.everest_core_config("config-sil.yaml")
@pytest.mark.everest_config_adaptions(EvControllerConfigAdjustment())
def test_bare_plug_synthesizes_a_session_from_the_config_defaults(
    everest_core, evctl_test_controller
):
    """Plug with no configure_session first; the FSM still leaves Unplugged."""
    everest_core.start()

    evctl_test_controller.start()

    assert evctl_test_controller.state_collector.wait_for_state(
        "Unplugged", timeout=_TIMEOUT_FINISH
    ), "EvController did not report Unplugged after start"

    evctl_test_controller.plug()

    assert evctl_test_controller.state_collector.wait_for_state_not(
        "Unplugged", timeout=_TIMEOUT_CHARGING
    ), (
        "a bare plug left the FSM in Unplugged, so no session was synthesized from the "
        f"config defaults; saw {evctl_test_controller.state_collector.states}"
    )

    evctl_test_controller.plug_out()

    assert evctl_test_controller.state_collector.wait_for_state(
        "Unplugged", timeout=_TIMEOUT_FINISH
    ), (
        "EvController did not return to Unplugged after unplug; "
        f"saw {evctl_test_controller.state_collector.states}"
    )


@pytest.mark.everest_core_config("config-sil.yaml")
@pytest.mark.everest_config_adaptions(EvControllerConfigAdjustment())
def test_ac_iec_basic_session(everest_core, evctl_test_controller):
    """AcIecBasic: plug in, reach Charging via the BSP path, then unplug."""
    everest_core.start()

    evctl_test_controller.start()
    # plug_in() publishes configure_session{mode: AcIec} + plug and blocks
    # until the FSM reports Charging (or its internal timeout elapses).
    evctl_test_controller.plug_in()

    assert evctl_test_controller.state_collector.wait_for_state(
        "Charging", timeout=_TIMEOUT_CHARGING
    ), (
        "EvController did not reach Charging under AcIecBasic; "
        f"saw {evctl_test_controller.state_collector.states}"
    )

    # Reaching Charging is not enough: a regression that lands in
    # Charging at 0 A would pass a state-only check. Require the SoC
    # integrator to actually advance.
    assert evctl_test_controller.state_collector.wait_for_soc_progress(
        min_increase=0.01, timeout=_TIMEOUT_CHARGING
    ), "SoC did not advance while in Charging under AcIecBasic"

    evctl_test_controller.plug_out()

    assert evctl_test_controller.state_collector.wait_for_state(
        "Unplugged", timeout=_TIMEOUT_FINISH
    ), (
        "EvController did not return to Unplugged after unplug; "
        f"saw {evctl_test_controller.state_collector.states}"
    )


@pytest.mark.everest_core_config("config-sil.yaml")
@pytest.mark.everest_config_adaptions(EvControllerConfigAdjustment())
def test_ac_iec_pause_resume(everest_core, evctl_test_controller):
    """AcIecPauseResume: charge, pause, resume, then unplug.

    Mirrors the DC ISO-2 pause/resume smoke but on the AC IEC route,
    which leaves Charging on a PWM pause rather than an ISO 15118
    pause-from-charger.
    """
    everest_core.start()

    evctl_test_controller.start()
    evctl_test_controller.plug_in()

    assert evctl_test_controller.state_collector.wait_for_state(
        "Charging", timeout=_TIMEOUT_CHARGING
    ), (
        "EvController did not reach Charging before pause; "
        f"saw {evctl_test_controller.state_collector.states}"
    )

    evctl_test_controller.pause_session()

    assert evctl_test_controller.state_collector.wait_for_state_not(
        "Charging", timeout=_TIMEOUT_FINISH
    ), (
        "EvController stayed in Charging after AC IEC pause; "
        f"saw {evctl_test_controller.state_collector.states}"
    )

    evctl_test_controller.resume_session()

    assert evctl_test_controller.state_collector.wait_for_state(
        "Charging", timeout=_TIMEOUT_CHARGING
    ), (
        "EvController did not resume Charging after AC IEC resume; "
        f"saw {evctl_test_controller.state_collector.states}"
    )

    evctl_test_controller.plug_out()

    assert evctl_test_controller.state_collector.wait_for_state(
        "Unplugged", timeout=_TIMEOUT_FINISH
    ), (
        "EvController did not return to Unplugged after unplug; "
        f"saw {evctl_test_controller.state_collector.states}"
    )
