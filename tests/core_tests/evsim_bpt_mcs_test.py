#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright Pionix GmbH and Contributors to EVerest
"""DC ISO 15118-20 BPT and MCS session smokes driven by EvSimulator.

These tests dispatch named scenarios (`DcIsoBpt`, `DcIsoMcs`) through
the typed `m2e/run_scenario` API and assert the deployed
`Evse15118D20` accepts the resulting BPT parameters and MCS power
class envelope. Both load `config/config-sil-evsim-dc-bpt.yaml`, which
wires EvSimulator into the DC stack alongside `Evse15118D20`,
`EvseManager`, and the DC power supply / Yeti simulator stack.

Observability:
- FSM reaches `Charging` (no `Faulted` state) confirms the EVSE
  accepted the negotiated parameters.
- `e2m/bsp_event` publications confirm the EvSimulator surfaced the
  underlying BSP envelope (used by the MCS test).

Runtime gating: as with the other EvSimulator smokes, re-run
`ninja -C build install` before invoking pytest so the deployed
manager loads the matching EvSimulator binary.
"""

import json
import threading
import time

import pytest

# Star-import pulls all transitive fixtures (everest_environment,
# core_config, evsim_test_controller, ...) into the test namespace.
from everest.testing.core_utils.fixtures import *  # noqa: F401,F403

# Share smoke_tests.py's "ISO15118" loadgroup so --dist=loadgroup keeps
# every EvSimulator SIL test on one xdist worker. Concurrent SIL stacks
# contend for CPU and flake pause/resume teardown; the group name must
# match smoke_tests.py or the suites still race.
pytestmark = pytest.mark.xdist_group(name="ISO15118")


# Generous timeouts: a DC ISO 15118-20 BPT/MCS session involves SLAC,
# TLS, parameter discovery, cable check, pre-charge, then energy
# transfer before the FSM lands on `Charging`.
_TIMEOUT_CHARGING = 90.0
_TIMEOUT_BSP_EVENT = 30.0


class _BspEventCollector:
    """Capture `e2m/bsp_event` payloads from the MQTT loop thread."""

    def __init__(self, mqtt_client, topic: str) -> None:
        self._topic = topic
        self.events = []
        self._cv = threading.Condition()
        mqtt_client.message_callback_add(topic, self._on_message)
        mqtt_client.subscribe(topic)

    def _on_message(self, _client, _userdata, msg) -> None:
        try:
            value = json.loads(msg.payload.decode())
        except (json.JSONDecodeError, UnicodeDecodeError):
            return
        if not isinstance(value, dict):
            return
        with self._cv:
            self.events.append(value)
            self._cv.notify_all()

    def wait_for_any(self, timeout: float) -> bool:
        deadline = time.time() + timeout
        with self._cv:
            while not self.events:
                remaining = deadline - time.time()
                if remaining <= 0:
                    return bool(self.events)
                self._cv.wait(timeout=remaining)
            return True


def _assert_not_faulted(evsim_test_controller) -> None:
    """Fail if the FSM ever transitioned through `Faulted`."""
    states = evsim_test_controller.state_collector.states
    assert "Faulted" not in states, (
        f"EvSimulator entered Faulted during session; saw {states}"
    )


@pytest.mark.everest_core_config("config-sil-evsim-dc-bpt.yaml")
def test_dc_iso_bpt_end_to_end(everest_core, evsim_test_controller):
    """Dispatch the `DcIsoBpt` scenario; assert Evse15118D20 accepts BPT params."""
    everest_core.start()

    evsim_test_controller.start()
    evsim_test_controller.run_scenario("DcIsoBpt")

    assert evsim_test_controller.state_collector.wait_for_state(
        "Charging", timeout=_TIMEOUT_CHARGING
    ), (
        "EvSimulator did not reach Charging under DcIsoBpt; "
        f"saw {evsim_test_controller.state_collector.states}"
    )

    _assert_not_faulted(evsim_test_controller)

    # "Charging + not Faulted" alone passes vacuously if the negotiated
    # BPT envelope was ignored. Require real energy transfer (the tick
    # integrator advancing SoC) as an affirmative signal the session is
    # live end to end.
    # NOTE: directional BPT discharge (negative current) is not
    # observable from the EV side over the current e2m topic set; a true
    # "BPT params applied" assertion needs an EVSE-side probe and is
    # tracked separately.
    assert evsim_test_controller.state_collector.wait_for_soc_progress(
        min_increase=0.01, timeout=_TIMEOUT_BSP_EVENT
    ), "SoC did not advance under DcIsoBpt; session not charging end to end"


@pytest.mark.everest_core_config("config-sil-evsim-dc-bpt.yaml")
def test_dc_iso_mcs_end_to_end(everest_core, evsim_test_controller):
    """Dispatch the `DcIsoMcs` scenario; assert MCS envelope is observable."""
    bsp_event_topic = f"{evsim_test_controller.base_e2m}/bsp_event"
    bsp_collector = _BspEventCollector(
        evsim_test_controller._mqtt_client, bsp_event_topic
    )

    everest_core.start()

    evsim_test_controller.start()
    evsim_test_controller.run_scenario("DcIsoMcs")

    assert evsim_test_controller.state_collector.wait_for_state(
        "Charging", timeout=_TIMEOUT_CHARGING
    ), (
        "EvSimulator did not reach Charging under DcIsoMcs; "
        f"saw {evsim_test_controller.state_collector.states}"
    )

    _assert_not_faulted(evsim_test_controller)

    assert bsp_collector.wait_for_any(timeout=_TIMEOUT_BSP_EVENT), (
        "EvSimulator did not publish any bsp_event under DcIsoMcs; "
        "MCS power class envelope not observable"
    )

    # Real energy transfer under the MCS envelope, not just FSM state.
    assert evsim_test_controller.state_collector.wait_for_soc_progress(
        min_increase=0.01, timeout=_TIMEOUT_BSP_EVENT
    ), "SoC did not advance under DcIsoMcs; session not charging end to end"
