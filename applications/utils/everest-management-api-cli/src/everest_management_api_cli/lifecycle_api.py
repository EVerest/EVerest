# SPDX-License-Identifier: Apache-2.0
# Copyright Pionix GmbH and Contributors to EVerest

"""Client for the manager's lifecycle management API (everest_api/<version>/lifecycle).

The management APIs live on fixed, unprefixed MQTT topics; see
lib/everest/framework/src/management_api/lifecycle_api.cpp and
docs/source/reference/EVerest_API/lifecycle_API.yaml in everest-core. The status topic is
published retained, so the current status is delivered right after subscribing.
"""

import json
import logging
import os
import threading
import time
import uuid
from typing import Callable, List, Optional, Tuple

import paho.mqtt.client as mqtt
from paho.mqtt import __version__ as paho_mqtt_version

from .mqtt_rpc import NoticeListener, perform_rpc, subscribe_and_wait

LIFECYCLE_API_VERSION = 1
LIFECYCLE_TOPIC_BASE = f"everest_api/{LIFECYCLE_API_VERSION}/lifecycle"
LIFECYCLE_STATUS_TOPIC = f"{LIFECYCLE_TOPIC_BASE}/e2m/status"


def lifecycle_command_topic(command: str) -> str:
    return f"{LIFECYCLE_TOPIC_BASE}/m2e/{command}"


def default_broker(host: Optional[str] = None, port: Optional[int] = None) -> Tuple[str, int]:
    """Resolve the broker address: explicit arguments, else the MQTT_SERVER_ADDRESS /
    MQTT_SERVER_PORT variables the manager itself uses, else localhost:1883."""
    if host is None:
        host = os.environ.get("MQTT_SERVER_ADDRESS", "127.0.0.1")
    if port is None:
        text = os.environ.get("MQTT_SERVER_PORT", "1883")
        try:
            port = int(text)
        except ValueError:
            raise ValueError(f"MQTT_SERVER_PORT must be an integer port number, got {text!r}") from None
    return host, port


def _paho_major_version() -> int:
    return int(paho_mqtt_version.split(".")[0])


def make_mqtt_client(client_id: str) -> mqtt.Client:
    """Create a paho client with the callback signatures this package uses, on paho 1.x and 2.x."""
    if _paho_major_version() < 2:
        return mqtt.Client(client_id)
    return mqtt.Client(callback_api_version=mqtt.CallbackAPIVersion.VERSION1, client_id=client_id)


class LifecycleApiClient:
    """Drives the lifecycle API over MQTT and records its status stream."""

    def __init__(self, mqtt_client: mqtt.Client, listener: Optional[NoticeListener] = None):
        self._client = mqtt_client
        self._listener = listener
        self._lock = threading.Lock()
        self._status_updates: List[dict] = []
        self._decode_errors: List[str] = []

    def subscribe_status(self) -> None:
        self._client.message_callback_add(LIFECYCLE_STATUS_TOPIC, self._on_status)
        subscribe_and_wait(self._client, LIFECYCLE_STATUS_TOPIC, qos=2)

    def _on_status(self, _client, _userdata, msg) -> None:
        if not msg.payload:
            # zero-length payloads only clear the retained message
            return
        # runs on paho's network thread: an uncaught exception here must not propagate
        try:
            update = json.loads(msg.payload)
        except (ValueError, TypeError) as exc:
            error = f"failed to decode lifecycle status payload: {exc}; raw={msg.payload!r}"
            logging.error(error)
            with self._lock:
                self._decode_errors.append(error)
            return
        with self._lock:
            self._status_updates.append(update)
        if self._listener is not None:
            self._listener(LIFECYCLE_STATUS_TOPIC, update)

    @property
    def status_updates(self) -> List[dict]:
        with self._lock:
            return list(self._status_updates)

    @property
    def latest_status(self) -> Optional[dict]:
        with self._lock:
            return self._status_updates[-1] if self._status_updates else None

    @property
    def decode_errors(self) -> List[str]:
        with self._lock:
            return list(self._decode_errors)

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
        errors = self.decode_errors
        detail = f"; decode errors: {errors}" if errors else ""
        raise TimeoutError(
            f"Timeout waiting for lifecycle status update; "
            f"received (from index {after_index}): {self.status_updates[after_index:]}{detail}")

    def wait_for_module_status(self, module_status: str, timeout_s: float = 60.0,
                               after_index: int = 0) -> dict:
        return self.wait_for_status(
            lambda u: u.get("module_status") == module_status, timeout_s, after_index)

    # --- commands ---

    def rpc(self, command: str, timeout_s: float = 10.0) -> dict:
        """Send a lifecycle API command (all take an empty payload) and return the parsed reply."""
        return perform_rpc(self._client, lifecycle_command_topic(command), {}, timeout_s)

    def get_everest_version(self, timeout_s: float = 10.0) -> dict:
        return self.rpc("get_everest_version", timeout_s)

    def stop_modules(self, timeout_s: float = 10.0) -> dict:
        return self.rpc("stop_modules", timeout_s)

    def start_modules(self, timeout_s: float = 10.0) -> dict:
        return self.rpc("start_modules", timeout_s)

    def clear_retained_status(self, timeout_s: float = 5.0) -> None:
        """Clear the broker-retained status message (e.g. the LWT of a killed manager)."""
        info = self._client.publish(LIFECYCLE_STATUS_TOPIC, payload=None, qos=1, retain=True)
        info.wait_for_publish(timeout=timeout_s)


def clear_retained_lifecycle_status(host: Optional[str] = None, port: Optional[int] = None,
                                    timeout_s: float = 5.0) -> None:
    """Clear the broker-retained lifecycle status via a short-lived, independent connection.

    For callers that have no shared connection at hand (or must not rely on one still being
    alive), e.g. a test fixture finalizer that runs after the manager process has exited.
    """
    host, port = default_broker(host, port)
    client = make_mqtt_client(f"everest_management_api_cli_clear_{uuid.uuid4().hex}")
    client.connect(host, port)
    client.loop_start()
    try:
        info = client.publish(LIFECYCLE_STATUS_TOPIC, payload=None, qos=1, retain=True)
        info.wait_for_publish(timeout=timeout_s)
    finally:
        client.loop_stop()
        client.disconnect()


def read_retained_status(host: Optional[str] = None, port: Optional[int] = None,
                         timeout_s: float = 10.0) -> Optional[dict]:
    """Connect a fresh MQTT client and return the retained status message, if any arrives in time."""
    host, port = default_broker(host, port)
    received: List[dict] = []
    event = threading.Event()

    def on_message(_client, _userdata, msg):
        if msg.payload:
            received.append(json.loads(msg.payload))
            event.set()

    client = make_mqtt_client(f"everest_management_api_cli_read_{uuid.uuid4().hex}")
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
