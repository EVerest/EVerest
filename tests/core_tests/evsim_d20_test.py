#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright Pionix GmbH and Contributors to EVerest
"""DC ISO 15118-20 end-to-end smoke driven by the EvSimulator typed API.

Loads `config/config-sil-evsim-dc-d20.yaml`, which wires EvSimulator
into the DC ISO-20 stack, then runs the canned `DcIsoD20Basic`
scenario through `evsim_test_controller.run_scenario`. The
StateCollector on `e2m/state` is used to verify the FSM walks through
`V2GNegotiating` and lands in `Charging`.

Runtime gating: the suite needs the deployed EVerest manager binary
to be current with the EvSimulator changes on this branch. Re-run
`ninja -C build install` before invoking pytest; without that step
the EvSimulator module the manager loads may not match the
controller's typed API and tests will hang waiting for `e2m/state`
publications.
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


# Generous timeout: a DC ISO 15118-20 session involves SLAC, TLS
# handshake, schedule negotiation, cable check, pre-charge, and the
# energy transfer loop before the FSM lands on `Charging`. Matches the
# 90 s used by the sibling DC-D20 BPT/MCS suite for the same session
# class; 30 s flaked under CI load.
_TIMEOUT_CHARGING = 90.0


@pytest.mark.everest_core_config("config-sil-evsim-dc-d20.yaml")
def test_dc_iso_d20_basic_session_end_to_end(everest_core, evsim_test_controller):
    """Run the `DcIsoD20Basic` scenario and verify FSM reaches Charging.

    Asserts that the e2m/state stream walks through `V2GNegotiating`
    and reaches `Charging` within `_TIMEOUT_CHARGING` seconds. The
    StateCollector retains every state observed, so the
    `V2GNegotiating` check is satisfied either by it being the
    current state when polled or by it being present in the rolling
    history once `Charging` arrives.
    """
    everest_core.start()

    evsim_test_controller.start()
    evsim_test_controller.run_scenario("DcIsoD20Basic")

    assert evsim_test_controller.state_collector.wait_for_state(
        "Charging", timeout=_TIMEOUT_CHARGING
    ), (
        "EvSimulator did not reach Charging; "
        f"saw {evsim_test_controller.state_collector.states}"
    )

    states_seen = evsim_test_controller.state_collector.states
    assert "V2GNegotiating" in states_seen, (
        "EvSimulator did not pass through V2GNegotiating; "
        f"saw {states_seen}"
    )
