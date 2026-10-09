#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright Pionix GmbH and Contributors to EVerest

"""Malformed messages on internal MQTT topics are dropped without terminating a module or the manager."""

import json

import pytest

from everest.testing.core_utils.everest_core import EverestCore, ManagerStatusFifo
from everest.testing.core_utils.fixtures import *

# A module that terminates on a message does so within milliseconds; the manager then reports the crash.
CRASH_WINDOW_S = 3.0

# id: (topic below the everest prefix, payload)
MALFORMED_MESSAGES = {
    "number_out_of_double_range": ("ready", "1e999"),
    "unknown_msg_type": ("ready", json.dumps({"msg_type": "bogus"})),
    "global_ready_without_data": ("ready", json.dumps({"msg_type": "GlobalReady"})),
    "nested_deeper_than_limit": ("ready", "[" * 200000 + "]" * 200000),
    "non_boolean_module_ready": (
        "modules/exit_simulator/ready",
        json.dumps({"msg_type": "ModuleReady", "data": "yes"}),
    ),
}


@pytest.mark.everest_core_config("config-sil-manager-lifecycle.yaml")
@pytest.mark.parametrize("topic_suffix, payload", list(MALFORMED_MESSAGES.values()), ids=list(MALFORMED_MESSAGES))
def test_malformed_internal_message_is_dropped(
    everest_core: EverestCore, connected_mqtt_client, topic_suffix, payload
):
    everest_core.start()

    topic = f"everest_{everest_core.everest_uuid}/{topic_suffix}"
    connected_mqtt_client.publish(topic, payload, qos=1).wait_for_publish(timeout=5)

    everest_core.assert_no_manager_status(
        ManagerStatusFifo.MANAGER_CRASH_SHUTDOWN_IN_PROGRESS, timeout_s=CRASH_WINDOW_S
    )
    assert everest_core.process.poll() is None, "manager exited"
