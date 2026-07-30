#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright Pionix GmbH and Contributors to EVerest

"""Raw-MQTT helpers for the manager's lifecycle management API.

The management APIs live on fixed, unprefixed MQTT topics (no per-test
external prefix is applied to them, unlike module external MQTT), see
lib/everest/framework/src/management_api/lifecycle_api.cpp. Requests use the
generic RequestReply envelope: {"headers": {"replyTo": <topic>}, "payload": "{}"}.
"""

import json
import logging
import os
import threading
import time
import uuid
from typing import Callable, List, Optional

import paho.mqtt.client as mqtt
from paho.mqtt import __version__ as paho_mqtt_version

from mqtt_rpc import perform_rpc

LIFECYCLE_API_VERSION = 1
LIFECYCLE_TOPIC_BASE = f"everest_api/{LIFECYCLE_API_VERSION}/lifecycle"
LIFECYCLE_STATUS_TOPIC = f"{LIFECYCLE_TOPIC_BASE}/e2m/status"


def lifecycle_command_topic(command: str) -> str:
    return f"{LIFECYCLE_TOPIC_BASE}/m2e/{command}"


def make_mqtt_client(client_id: str) -> mqtt.Client:
    if paho_mqtt_version < '2.0':
        return mqtt.Client(client_id)
    return mqtt.Client(callback_api_version=mqtt.CallbackAPIVersion.VERSION1, client_id=client_id)


class LifecycleApiClient:
    """Drives the lifecycle API over raw MQTT and records its status stream."""

    def __init__(self, mqtt_client: mqtt.Client):
        self._client = mqtt_client
        self._lock = threading.Lock()
        self._status_updates: List[dict] = []

    def subscribe_status(self) -> None:
        self._client.message_callback_add(LIFECYCLE_STATUS_TOPIC, self._on_status)
        self._client.subscribe(LIFECYCLE_STATUS_TOPIC, qos=2)

    def _on_status(self, _client, _userdata, msg) -> None:
        if not msg.payload:
            # zero-length payloads only clear the retained message
            return
        update = json.loads(msg.payload)
        logging.info(f"lifecycle status update: {update}")
        with self._lock:
            self._status_updates.append(update)

    @property
    def status_updates(self) -> List[dict]:
        with self._lock:
            return list(self._status_updates)

    def module_statuses(self) -> List[str]:
        return [u["module_status"] for u in self.status_updates if "module_status" in u]

    def mark(self) -> int:
        """Return the current stream position, for waiting on updates newer than 'now'."""
        return len(self.status_updates)

    def wait_for_status(self, predicate: Callable[[dict], bool], timeout_s: float = 60.0,
                        after_index: int = 0) -> dict:
        deadline = time.monotonic() + timeout_s
        while time.monotonic() < deadline:
            for update in self.status_updates[after_index:]:
                if predicate(update):
                    return update
            time.sleep(0.05)
        raise TimeoutError(
            f"Timeout waiting for lifecycle status update; "
            f"received (from index {after_index}): {self.status_updates[after_index:]}"
        )

    def wait_for_module_status(self, module_status: str, timeout_s: float = 60.0,
                               after_index: int = 0) -> dict:
        return self.wait_for_status(
            lambda u: u.get("module_status") == module_status, timeout_s, after_index)

    def rpc(self, command: str, timeout_s: float = 10.0) -> dict:
        """Send a lifecycle API command and return the parsed reply."""
        return perform_rpc(self._client, lifecycle_command_topic(command), {}, timeout_s)

    def clear_retained_status(self) -> None:
        """Clear the broker-retained status message (e.g. the LWT of a killed manager)."""
        info = self._client.publish(LIFECYCLE_STATUS_TOPIC, payload=None, qos=1, retain=True)
        info.wait_for_publish(timeout=5)


def read_retained_status(timeout_s: float = 10.0) -> Optional[dict]:
    """Connect a fresh MQTT client and return the retained status message, if any."""
    host = os.environ.get("MQTT_SERVER_ADDRESS", "127.0.0.1")
    port = int(os.environ.get("MQTT_SERVER_PORT", "1883"))
    received: List[dict] = []
    event = threading.Event()

    def on_message(_client, _userdata, msg):
        if msg.payload:
            received.append(json.loads(msg.payload))
            event.set()

    client = make_mqtt_client(f"management_api_tests_{uuid.uuid4().hex}")
    client.on_message = on_message
    client.connect(host, port)
    client.subscribe(LIFECYCLE_STATUS_TOPIC, qos=1)
    client.loop_start()
    try:
        if event.wait(timeout_s):
            return received[0]
        return None
    finally:
        client.loop_stop()
        client.disconnect()
