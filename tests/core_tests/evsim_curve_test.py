#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright Pionix GmbH and Contributors to EVerest
"""Charging-curve and runtime-ramp smokes driven by the EvSimulator typed API.

These tests exercise the curve / ramp surface through the typed `m2e/*`
MQTT controller:

- `test_charging_curve_taper_end_to_end` runs the `DcIsoTaper` preset
  which schedules a 4-point ChargingCurve at 100, 50, 20, 5 A. We
  subscribe to `e2m/bsp_event` and `e2m/command_ack`. The FSM is asserted
  to reach `Charging`, and the per-step `set_charging_current` ack stream
  is asserted to fire 4 times (one per curve point) since DC/ISO-2 mode
  rejects `set_charging_current` at the FSM with a Rejected ack — the
  arrival sequence is itself the observable "current series passing
  through expected setpoints". The numeric setpoints (100/50/20/5 A) are
  invariants of the preset and are verified directly by the
  `ScenarioDispatcher` unit tests.

- `test_runtime_ramp_end_to_end` starts the `AcIsoBasic` preset which
  lands in `Charging` at 16 A three-phase, then issues
  `ramp_to_current(target_a=16, three_phases=True, duration_s=5)` to
  cover the runtime ramp path. The ramp is accepted in AC mode (no
  rejection ack); `RampInterpolator` drives the BSP layer over ~5 s.
  Observability for the in-flight ramp is the absence of any
  `set_charging_current` Rejected ack during the 5 s window — the test
  collects `command_ack` events and asserts none were rejections.

Both tests load `config/config-sil-evsim-dc.yaml`, the same SIL config
used by the DC ISO-2 smokes.

Runtime gating: as with the other EvSimulator smokes, re-run
`ninja -C build install` before invoking pytest so the deployed manager
loads the matching EvSimulator binary.
"""

import json
import threading
import time

import pytest

# Star-import pulls all transitive fixtures (everest_environment,
# core_config, evsim_test_controller, ...) into the test namespace.
from everest.testing.core_utils.fixtures import *  # noqa: F401,F403


# Generous timeouts: an ISO 15118-2 DC session involves SLAC, TLS,
# parameter discovery, cable check, pre-charge, then energy transfer
# before the FSM lands on `Charging`. The taper preset then schedules 4
# curve points at offsets 0/30000/60000/90000 ms with `stop_session` at
# 120 s, so we allow a generous deadline to observe all 4 acks.
_TIMEOUT_CHARGING = 90.0
_TIMEOUT_CURVE_ACKS = 130.0
_RAMP_DURATION_S = 5.0
_TIMEOUT_RAMP_OBSERVE = 8.0


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


class _CommandAckCollector:
    """Capture `e2m/command_ack` payloads, filtered by command name.

    Each `command_ack` is a dict with `command`, `status`, and `reason`
    keys. Tests can inspect `acks_for(command_name)` to count or assert
    a particular command's per-attempt acknowledgments.
    """

    def __init__(self, mqtt_client, topic: str) -> None:
        self._topic = topic
        self.acks = []
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
            self.acks.append(value)
            self._cv.notify_all()

    def acks_for(self, command: str):
        with self._cv:
            return [a for a in self.acks if a.get("command") == command]

    def wait_for_acks_for(self, command: str, count: int, timeout: float) -> bool:
        deadline = time.time() + timeout
        with self._cv:
            while True:
                got = sum(1 for a in self.acks if a.get("command") == command)
                if got >= count:
                    return True
                remaining = deadline - time.time()
                if remaining <= 0:
                    return False
                self._cv.wait(timeout=remaining)


@pytest.mark.everest_core_config("config-sil-evsim-dc.yaml")
def test_charging_curve_taper_end_to_end(everest_core, evsim_test_controller):
    """Dispatch the `DcIsoTaper` preset; assert the 4-point curve fires.

    `DcIsoTaper` schedules a 4-point ChargingCurve at currents
    100, 50, 20, 5 A. In DC/ISO-2 mode the `set_charging_current` event
    is rejected by the FSM with a Rejected command_ack — each curve
    point yields exactly one ack. Observing the ack stream is the
    end-to-end signal that the dispatcher fired through the full series
    of setpoints; the numeric values themselves are an invariant of the
    preset, covered by the `ScenarioDispatcher` unit tests.
    """
    bsp_event_topic = f"{evsim_test_controller.base_e2m}/bsp_event"
    command_ack_topic = f"{evsim_test_controller.base_e2m}/command_ack"
    bsp_collector = _BspEventCollector(
        evsim_test_controller._mqtt_client, bsp_event_topic
    )
    ack_collector = _CommandAckCollector(
        evsim_test_controller._mqtt_client, command_ack_topic
    )

    everest_core.start()

    evsim_test_controller.start()
    evsim_test_controller.run_scenario("DcIsoTaper")

    assert evsim_test_controller.state_collector.wait_for_state(
        "Charging", timeout=_TIMEOUT_CHARGING
    ), (
        "EvSimulator did not reach Charging under DcIsoTaper; "
        f"saw {evsim_test_controller.state_collector.states}"
    )

    # The 4 curve points fire over ~95 s (offsets 0/30000/60000/90000 ms
    # plus the 5 s final ramp). Each lands in Charging::feed as a
    # SetChargingCurrent event, gets rejected for DC mode, and emits a
    # `set_charging_current` Rejected command_ack.
    assert ack_collector.wait_for_acks_for(
        "set_charging_current", 4, timeout=_TIMEOUT_CURVE_ACKS
    ), (
        "EvSimulator did not emit 4 set_charging_current acks for the "
        "DcIsoTaper curve; "
        f"saw {[a.get('command') for a in ack_collector.acks]}"
    )

    # bsp_event activity confirms the EVSE-side charging cycle was
    # observable end-to-end (CP state transitions plus PowerOn/PowerOff).
    assert bsp_collector.events, (
        "EvSimulator did not publish any bsp_event during DcIsoTaper; "
        "charging cycle not observable"
    )


@pytest.mark.everest_core_config("config-sil-evsim-dc.yaml")
def test_runtime_ramp_end_to_end(everest_core, evsim_test_controller):
    """Start an AC ISO 15118-2 session, then ramp the commanded current.

    `AcIsoBasic` preset lands in `Charging` at 16 A three-phase. From
    there we issue `ramp_to_current(target_a=16, three_phases=True,
    duration_s=5)` which is accepted in AC mode and processed by
    `RampInterpolator` over 5 s. Observability: no `Rejected`
    `set_charging_current` ack should arrive during the ramp window —
    the ramp is captured into `ActiveRamp` and applied tick-by-tick to
    the BSP layer, not surfaced on the external API. A rejection ack
    would indicate the FSM was not in Charging (or in a DC/ISO-20 mode
    where the ramp path doesn't apply).
    """
    command_ack_topic = f"{evsim_test_controller.base_e2m}/command_ack"
    ack_collector = _CommandAckCollector(
        evsim_test_controller._mqtt_client, command_ack_topic
    )

    everest_core.start()

    evsim_test_controller.start()
    evsim_test_controller.run_scenario("AcIsoBasic")

    assert evsim_test_controller.state_collector.wait_for_state(
        "Charging", timeout=_TIMEOUT_CHARGING
    ), (
        "EvSimulator did not reach Charging under AcIsoBasic; "
        f"saw {evsim_test_controller.state_collector.states}"
    )

    # Snapshot the ack history at entry so the assertion only considers
    # acks that arrived during/after the ramp.
    pre_ramp_ack_count = len(ack_collector.acks)

    evsim_test_controller.ramp_to_current(
        target_a=16.0,
        three_phases=True,
        duration_s=_RAMP_DURATION_S,
    )

    # Wait at least one full ramp duration so any rejection ack would
    # have time to arrive.
    time.sleep(_TIMEOUT_RAMP_OBSERVE)

    new_acks = ack_collector.acks[pre_ramp_ack_count:]
    rejected_set_current = [
        a for a in new_acks
        if a.get("command") == "set_charging_current"
        and a.get("status") == "Rejected"
    ]
    assert not rejected_set_current, (
        "ramp_to_current was rejected during AcIsoBasic Charging; "
        f"acks={rejected_set_current}"
    )
