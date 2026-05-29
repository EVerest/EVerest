#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright Pionix GmbH and Contributors to EVerest
"""
Smoke test for EvSimulator (modules/EV/EvSimulator).

Boots a real EVerest manager via config-sil-evsim.yaml, then drives the
typed `m2e/*` API (enable, plug, unplug) over MQTT and asserts the
`e2m/state` publications transition through the FSM:

    Disabled -> Unplugged -> Plugged -> Unplugged

Topic shape (set by EvSimulator::init via Topics::setup):

    everest_api/1/ev_simulator/<module_id>/<e2m|m2e>/<var>

where module_id == YAML block name == "ev_manager" in config-sil-evsim.yaml.
"""

import json
import time

import pytest

# Star-import pulls all transitive fixtures (everest_environment, core_config,
# etc.) into the test namespace — same pattern as core_tests/smoke_tests.py.
from everest.testing.core_utils.fixtures import *  # noqa: F401,F403

API_SUFFIX = "everest_api/1/ev_simulator/ev_manager"


class StateCollector:
    """Capture e2m/state payloads from the mqtt loop thread."""

    def __init__(self, state_topic: str):
        self.state_topic = state_topic
        self.states = []

    def on_message(self, _client, _userdata, msg):
        if msg.topic == self.state_topic:
            # Payload is a JSON-encoded string (e.g. b'"Disabled"').
            value = json.loads(msg.payload.decode())
            if isinstance(value, str):
                self.states.append(value)


def _wait_for_state_at_index(collector: StateCollector, state: str,
                              min_index: int, timeout: float) -> int:
    """Wait until `state` appears in `collector.states` at index >= min_index.

    Returns the index where it first appears. Raises AssertionError on timeout.
    """
    deadline = time.time() + timeout
    while time.time() < deadline:
        for idx in range(min_index, len(collector.states)):
            if collector.states[idx] == state:
                return idx
        time.sleep(0.1)
    raise AssertionError(
        f"timeout waiting for state '{state}' at index >= {min_index}, "
        f"saw {collector.states}"
    )


@pytest.mark.everest_core_config("config-sil-evsim.yaml")
def test_basic_plug_unplug(everest_core, connected_mqtt_client):
    """Drive enable + plug + unplug; assert FSM state transitions in order."""
    # The everest_core fixture rewrites the config to prefix every external
    # MQTT topic with `external_<uuid>` (see EverestMqttConfigurationAdjustmentStrategy),
    # so external clients must apply the same prefix.
    base = f"{everest_core.mqtt_external_prefix}{API_SUFFIX}"
    state_topic = f"{base}/e2m/state"

    collector = StateCollector(state_topic)
    connected_mqtt_client.on_message = collector.on_message
    connected_mqtt_client.subscribe(state_topic)

    # The `everest_core` fixture builds the runtime config but does not start
    # the manager subprocess — start it now so EvSimulator boots and begins
    # publishing on the e2m topics.
    everest_core.start()

    # Give the broker a moment to deliver the initial Disabled publication
    # that EvSimulator emits on startup.
    time.sleep(0.5)

    connected_mqtt_client.publish(f"{base}/m2e/enable", json.dumps(True))
    unplugged_idx_1 = _wait_for_state_at_index(
        collector, "Unplugged", 0, timeout=5
    )

    connected_mqtt_client.publish(f"{base}/m2e/plug", json.dumps(None))
    plugged_idx = _wait_for_state_at_index(
        collector, "Plugged", unplugged_idx_1 + 1, timeout=5
    )

    connected_mqtt_client.publish(f"{base}/m2e/unplug", json.dumps(None))
    _wait_for_state_at_index(
        collector, "Unplugged", plugged_idx + 1, timeout=5
    )
