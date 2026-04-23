#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright Pionix GmbH and Contributors to EVerest

import time
from collections import defaultdict

import pytest

from everest.testing.core_utils.everest_core import EverestCore
from everest.testing.core_utils.fixtures import *


def wait_for_topic_count(topic_counts, topic: str, expected_count: int, timeout_s: float = 60.0):
    deadline = time.monotonic() + timeout_s
    while time.monotonic() < deadline:
        if topic_counts[topic] >= expected_count:
            return
        time.sleep(0.2)
    raise TimeoutError(
        f"Timeout waiting for '{topic}' to reach count {expected_count}. "
        f"Current count is {topic_counts[topic]}."
    )


@pytest.mark.everest_core_config("config-sil-immortal_manager.yaml")
def test_manager_restarts_modules_after_unexpected_exit(
    everest_core: EverestCore, connected_mqtt_client
):
    topic_counts = defaultdict(int)

    def on_message(_client, _userdata, msg):
        topic_counts[msg.topic] += 1

    connected_mqtt_client.on_message = on_message
    connected_mqtt_client.subscribe("everest/exit_simulator/ready")

    everest_core.start()
    wait_for_topic_count(topic_counts, "everest/exit_simulator/ready", 1, timeout_s=20.0)
    baseline_ready = topic_counts["everest/exit_simulator/ready"]

    connected_mqtt_client.publish("everest_api/exit_simulator/cmd/exit", "1")

    wait_for_topic_count(
        topic_counts,
        "everest/exit_simulator/ready",
        baseline_ready + 1,
        timeout_s=60.0,
    )


@pytest.mark.everest_core_config("config-sil-immortal_manager.yaml")
def test_manager_recovers_after_crash_with_blocked_module_timeout(
    everest_core: EverestCore, connected_mqtt_client
):
    topic_counts = defaultdict(int)

    def on_message(_client, _userdata, msg):
        topic_counts[msg.topic] += 1

    connected_mqtt_client.on_message = on_message
    connected_mqtt_client.subscribe("everest/block_simulator/ready")

    everest_core.start()
    wait_for_topic_count(topic_counts, "everest/block_simulator/ready", 1, timeout_s=20.0)
    baseline_ready = topic_counts["everest/block_simulator/ready"]

    connected_mqtt_client.publish("everest_api/block_simulator/cmd/block", "")
    time.sleep(0.5)
    connected_mqtt_client.publish("everest_api/exit_simulator/cmd/exit", "1")

    wait_for_topic_count(
        topic_counts,
        "everest/block_simulator/ready",
        baseline_ready + 1,
        timeout_s=90.0,
    )
