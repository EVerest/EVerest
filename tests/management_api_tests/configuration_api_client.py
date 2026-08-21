#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright Pionix GmbH and Contributors to EVerest

"""Raw-MQTT client for the manager's configuration management API.

Topics and payloads per lib/everest/framework/src/management_api/configuration_api.cpp
and lib/everest/everest_api_types/src/everest_api_types/configuration/json_codec.cpp.
Unlike the lifecycle status topic, the monitor topics (active_slot, config_updates) are
published QoS0 and NOT retained — subscribe before the manager starts.
"""

import json
import logging
import threading
import time
from typing import Callable, List, Optional, Tuple

import paho.mqtt.client as mqtt

from mqtt_rpc import perform_rpc, subscribe_and_wait

CONFIGURATION_API_VERSION = 1
CONFIGURATION_TOPIC_BASE = f"everest_api/{CONFIGURATION_API_VERSION}/configuration"
ACTIVE_SLOT_TOPIC = f"{CONFIGURATION_TOPIC_BASE}/e2m/active_slot"
CONFIG_UPDATES_TOPIC = f"{CONFIGURATION_TOPIC_BASE}/e2m/config_updates"

# (module_id, parameter_name, implementation_id or None for module-level parameters)
ParameterId = Tuple[str, str, Optional[str]]


def configuration_command_topic(command: str) -> str:
    return f"{CONFIGURATION_TOPIC_BASE}/m2e/{command}"


def _cfg_param_id(param: ParameterId) -> dict:
    module_id, parameter_name, implementation_id = param
    identifier = {"module_id": module_id, "parameter_name": parameter_name}
    if implementation_id is not None:
        identifier["implementation_id"] = implementation_id
    return identifier


class ConfigurationApiClient:
    """Drives the configuration API over raw MQTT and records its notification streams."""

    def __init__(self, mqtt_client: mqtt.Client):
        self._client = mqtt_client
        self._lock = threading.Lock()
        self._active_slot_notices: List[dict] = []
        self._config_update_notices: List[dict] = []
        self._decode_errors: List[str] = []

    def subscribe_notices(self) -> None:
        self._client.message_callback_add(ACTIVE_SLOT_TOPIC, self._on_active_slot)
        self._client.message_callback_add(CONFIG_UPDATES_TOPIC, self._on_config_update)
        subscribe_and_wait(self._client, [(ACTIVE_SLOT_TOPIC, 2), (CONFIG_UPDATES_TOPIC, 2)])

    def _decode(self, what: str, msg) -> Optional[dict]:
        # runs on paho's network thread: an uncaught exception here must not
        # propagate, or the notice never gets recorded and the waiter below
        # dies later with a misleading timeout instead of the real cause.
        try:
            return json.loads(msg.payload)
        except (ValueError, TypeError) as exc:
            error = f"failed to decode {what} payload: {exc}; raw={msg.payload!r}"
            logging.error(error)
            with self._lock:
                self._decode_errors.append(error)
            return None

    def _on_active_slot(self, _client, _userdata, msg) -> None:
        notice = self._decode("active_slot", msg)
        if notice is None:
            return
        logging.info(f"configuration active_slot notice: {notice}")
        with self._lock:
            self._active_slot_notices.append(notice)

    def _on_config_update(self, _client, _userdata, msg) -> None:
        notice = self._decode("config_updates", msg)
        if notice is None:
            return
        logging.info(f"configuration config_updates notice: {notice}")
        with self._lock:
            self._config_update_notices.append(notice)

    @property
    def active_slot_notices(self) -> List[dict]:
        with self._lock:
            return list(self._active_slot_notices)

    @property
    def config_update_notices(self) -> List[dict]:
        with self._lock:
            return list(self._config_update_notices)

    @property
    def decode_errors(self) -> List[str]:
        with self._lock:
            return list(self._decode_errors)

    def mark_active_slot_stream(self) -> int:
        """Return the current active_slot notice stream position, for waiting on
        notices newer than 'now' (see LifecycleApiClient.mark()). Deliberately not
        named mark_active_slot(...): that name is already the mark_active_slot RPC
        wrapper below, and reusing it here for the notice stream would be confusing."""
        return len(self.active_slot_notices)

    def mark_config_update_stream(self) -> int:
        """Return the current config_updates notice stream position, for waiting on
        notices newer than 'now' (see LifecycleApiClient.mark())."""
        return len(self.config_update_notices)

    def _wait_for(self, notices: Callable[[], List[dict]], predicate: Callable[[dict], bool],
                  timeout_s: float, after_index: int, what: str) -> dict:
        deadline = time.monotonic() + timeout_s
        while time.monotonic() < deadline:
            for notice in notices()[after_index:]:
                if predicate(notice):
                    return notice
            time.sleep(0.05)
        errors = self.decode_errors
        detail = f"; decode errors: {errors}" if errors else ""
        raise TimeoutError(
            f"Timeout waiting for {what} notice; "
            f"received (from index {after_index}): {notices()[after_index:]}{detail}"
        )

    def wait_for_active_slot(self, predicate: Callable[[dict], bool], timeout_s: float = 60.0,
                             after_index: int = 0) -> dict:
        return self._wait_for(lambda: self.active_slot_notices, predicate, timeout_s,
                              after_index, "active_slot")

    def wait_for_active_slot_status(self, status: str, timeout_s: float = 60.0,
                                    after_index: int = 0) -> dict:
        return self.wait_for_active_slot(lambda n: n.get("status") == status, timeout_s, after_index)

    def wait_for_config_update(self, predicate: Callable[[dict], bool], timeout_s: float = 60.0,
                               after_index: int = 0) -> dict:
        return self._wait_for(lambda: self.config_update_notices, predicate, timeout_s,
                              after_index, "config_updates")

    # --- commands ---

    def _rpc(self, command: str, payload: dict, timeout_s: float = 10.0) -> dict:
        return perform_rpc(self._client, configuration_command_topic(command), payload, timeout_s)

    def list_all_slots(self) -> dict:
        return self._rpc("list_all_slots", {})

    def get_active_slot(self) -> dict:
        return self._rpc("get_active_slot", {})

    def mark_active_slot(self, slot_id: int) -> dict:
        return self._rpc("mark_active_slot", {"slot_id": slot_id})

    def delete_slot(self, slot_id: int) -> dict:
        return self._rpc("delete_slot", {"slot_id": slot_id})

    def duplicate_slot(self, slot_id: int, description: Optional[str] = None) -> dict:
        payload = {"slot_id": slot_id}
        if description is not None:
            payload["new_description"] = description
        return self._rpc("duplicate_slot", payload)

    def load_from_yaml(self, raw_yaml: str, description: Optional[str] = None,
                       slot_id: Optional[int] = None) -> dict:
        payload = {"raw_yaml": raw_yaml}
        if description is not None:
            payload["description"] = description
        if slot_id is not None:
            payload["slot_id"] = slot_id
        return self._rpc("load_from_yaml", payload)

    def set_description(self, slot_id: int, description: str) -> dict:
        return self._rpc("set_description", {"slot_id": slot_id, "description": description})

    def get_configuration(self, slot_id: int, force_read_from_db: bool = False) -> dict:
        return self._rpc("get_configuration",
                         {"slot_id": slot_id, "force_read_from_db": force_read_from_db})

    def set_config_parameters(self, slot_id: int,
                              updates: List[Tuple[str, str, Optional[str], str]]) -> dict:
        """updates: list of (module_id, parameter_name, implementation_id or None, value)."""
        parameter_updates = [
            {"cfg_param_id": _cfg_param_id((module_id, parameter_name, implementation_id)),
             "value": value}
            for module_id, parameter_name, implementation_id, value in updates
        ]
        return self._rpc("set_config_parameters",
                         {"slot_id": slot_id, "parameter_updates": parameter_updates})

    def get_config_parameters(self, slot_id: int, parameters: List[ParameterId],
                              force_read_from_db: bool = False) -> dict:
        return self._rpc("get_config_parameters",
                         {"slot_id": slot_id,
                          "parameters": [_cfg_param_id(p) for p in parameters],
                          "force_read_from_db": force_read_from_db})
