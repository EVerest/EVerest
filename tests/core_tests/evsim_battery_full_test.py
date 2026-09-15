#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright Pionix GmbH and Contributors to EVerest
"""End-to-end coverage for the EvSimulator `on_battery_full` policy.

The four policies live in `SocIntegrator::soc_step` and are selected by
`cfg.on_battery_full` with `cfg.battery_full_threshold_pct`. Unit tests
in `modules/EV/EvSimulator/tests/SocIntegratorTest.cpp` cover the four
behaviors directly; this suite is what keeps the *config* path live, so
the policy is not merely reachable from a unit fixture but actually
selected by something the manager loads.

`config/config-sil-evsim-battery-full.yaml` sets
`on_battery_full: stop_session` at an 80 % threshold on a deliberately
small 1000 Wh pack starting at 78 %, so the rising edge fires a few
seconds into Charging rather than hours in.

`stop_session` is the policy with an FSM-observable effect. In AcIec mode
`Charging::feed` routes `StopSession` straight to `Unplugged`
(`states/Charging.cpp:58-62`), so a session that ends with no plug_out()
and no stop_session() from the test is attributable to the policy alone:
nothing else in this config terminates a session on its own.

Runtime gating: the deployed EVerest manager binary must be current with
the EvSimulator changes on this branch. Re-run `ninja -C build install`
before invoking pytest; otherwise the manager loads a stale EvSimulator
and the tests hang waiting for `e2m/state` publications.
"""

import pytest

# Star-import pulls all transitive fixtures (everest_environment,
# core_config, evsim_test_controller, ...) into the test namespace.
from everest.testing.core_utils.fixtures import *  # noqa: F401,F403

# Share the "ISO15118" loadgroup so --dist=loadgroup keeps every
# EvSimulator SIL test on one xdist worker; concurrent SIL stacks contend
# for CPU and flake teardown.
pytestmark = pytest.mark.xdist_group(name="ISO15118")

_TIMEOUT_CHARGING = 30.0

# The pack needs 20 Wh to climb from 78 % to the 80 % threshold, which is
# ~6.5 s at 16 A * 230 V * 3 phases. The bound is generous because the
# delivered current depends on what the EVSE side offers, and a
# single-phase 6 A fallback would still cross inside a minute.
_TIMEOUT_POLICY_FIRES = 120.0

_THRESHOLD_PCT = 80.0


@pytest.mark.everest_core_config("config-sil-evsim-battery-full.yaml")
def test_stop_session_policy_ends_the_session_at_the_threshold(
    everest_core, evsim_test_controller
):
    """The battery-full edge terminates the session with no test command."""
    everest_core.start()

    evsim_test_controller.start()
    evsim_test_controller.plug_in()

    collector = evsim_test_controller.state_collector

    assert collector.wait_for_state("Charging", timeout=_TIMEOUT_CHARGING), (
        "EvSimulator did not reach Charging; " f"saw {collector.states}"
    )

    # Reaching Charging is not enough: a stack sitting in Charging at 0 A
    # would never cross the threshold, and the assertion below would then
    # be testing the timeout rather than the policy.
    assert collector.wait_for_soc_progress(
        min_increase=0.01, timeout=_TIMEOUT_CHARGING
    ), "SoC did not advance while in Charging"

    # The policy, and only the policy, ends this session: the test issues
    # no plug_out() and no stop_session().
    assert collector.wait_for_state(
        "Unplugged", timeout=_TIMEOUT_POLICY_FIRES
    ), (
        "on_battery_full=stop_session did not end the session at the "
        f"{_THRESHOLD_PCT} % threshold; saw {collector.states}"
    )

    # A fault would also end the session and would otherwise satisfy the
    # assertion above, so rule that route out before attributing the ending
    # to the policy.
    assert not collector.faults, (
        "session ended with a fault, so the ending is not attributable to "
        f"on_battery_full; saw {collector.faults}"
    )

    soc_samples = [
        info["soc_pct"]
        for info in collector.ev_infos
        if isinstance(info.get("soc_pct"), (int, float))
    ]
    assert soc_samples, "no ev_info carried a soc_pct sample"

    # Crossed the threshold, which is what armed the edge...
    assert max(soc_samples) >= _THRESHOLD_PCT - 0.5, (
        f"SoC never reached the {_THRESHOLD_PCT} % threshold; "
        f"peak was {max(soc_samples)}"
    )
    # ...and was trimmed there rather than running on toward 100 %, which
    # is the observable that separates stop_session from clamp.
    assert max(soc_samples) <= _THRESHOLD_PCT + 1.0, (
        f"SoC ran past the {_THRESHOLD_PCT} % threshold to "
        f"{max(soc_samples)}; the policy trim did not engage"
    )


@pytest.mark.everest_core_config("config-sil-evsim-battery-full.yaml")
def test_query_state_answers_over_the_real_topic(
    everest_core, evsim_test_controller
):
    """`m2e/query_state` answers on `e2m/state`, the topic a UI reads.

    The command is operator-facing (logging, current-state display), so
    its coverage has to be the wire round trip, not a cache read.
    """
    everest_core.start()

    evsim_test_controller.start()

    answer = evsim_test_controller.query_state(timeout=15.0)

    assert answer is not None, (
        "m2e/query_state produced no e2m/state answer; "
        f"saw {evsim_test_controller.state_collector.states}"
    )
    assert answer == "Unplugged", (
        f"query_state answered {answer!r}, expected the idle state after "
        "start()"
    )
