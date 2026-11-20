# SPDX-License-Identifier: Apache-2.0
# Copyright Pionix GmbH and Contributors to EVerest

"""An external application that owns device model variables through the ocpp_device_model_API module, as an
integrator writes it, and the configuration strategies that put the API module between it and OCPPmulti."""

import asyncio
import json
import logging
import os
import threading
import time
from copy import deepcopy
from pathlib import Path
from typing import Callable, Dict, List, Optional, Union

import paho.mqtt.client as mqtt
import yaml

from everest.testing.core_utils import EverestConfigAdjustmentStrategy
from everest.testing.core_utils.controller.everest_test_controller import EverestTestController
from everest.testing.ocpp_utils.central_system import CentralSystem

log = logging.getLogger("deviceModelApiClient")

API_MODULE_ID = "device_model_api"
OWNER = "cooling-unit"

# Short timeouts so that a test that waits for a deadline stays short
API_TIMEOUTS = {
    "cfg_expected_owners_timeout_s": 10,
    "cfg_registration_timeout_s": 2,
    "cfg_request_reply_to_s": 2,
    "cfg_communication_check_to_s": 0,
}


class DeviceModelApiConfigAdjustment(EverestConfigAdjustmentStrategy):
    """Adds ocpp_device_model_API modules and connects them, in the given order, to the device_model requirement of
    OCPPmulti"""

    def __init__(self, config_module: Optional[Dict] = None, modules: Optional[Dict[str, Dict]] = None,
                 ocpp_config_module: Optional[Dict] = None):
        self.modules = {module_id: {**API_TIMEOUTS, **config}
                        for module_id, config in (modules or {API_MODULE_ID: config_module or {}}).items()}
        self.ocpp_config_module = ocpp_config_module or {}

    def adjust_everest_configuration(self, everest_config: Dict):
        adjusted = deepcopy(everest_config)
        modules = adjusted["active_modules"]
        for module_id, config in self.modules.items():
            modules[module_id] = {"module": "ocpp_device_model_API", "config_module": dict(config)}
        ocpp = modules["ocpp"]
        ocpp["connections"]["device_model"] = [
            {"module_id": module_id, "implementation_id": "main"} for module_id in self.modules
        ]
        ocpp.setdefault("config_module", {}).update(self.ocpp_config_module)
        return adjusted


class _NoAliasDumper(yaml.SafeDumper):
    """Writes shared objects in full: the mapping file loader does not resolve YAML aliases"""

    def ignore_aliases(self, data):
        return True


def write_ocpp16_mappings(directory: Path, mappings: Union[List[Dict], str]) -> Path:
    """Writes an OCPP 1.6 key mapping file, from a list of mappings or as given text"""
    path = directory / "ocpp16-key-mappings.yaml"
    text = mappings if isinstance(mappings, str) else yaml.dump({"mappings": mappings}, Dumper=_NoAliasDumper,
                                                                sort_keys=False)
    path.write_text(text)
    return path


def ocpp16_mapping(key: str, component: str, variable: str, instance: Optional[str] = None,
                   evse_id: Optional[int] = None) -> Dict:
    target_component = {"name": component}
    if instance is not None:
        target_component["instance"] = instance
    if evse_id is not None:
        target_component["evse"] = {"id": evse_id}
    return {"ocpp16_key": key, "component": target_component, "variable": {"name": variable}}


def cooling_unit_component(evse_id: Optional[int] = 1) -> Dict:
    component = {"name": "CoolingUnit", "instance": "1"}
    if evse_id is not None:
        component["evse"] = {"id": evse_id}
    return component


def temperature_variable(value="45.5", monitors=True) -> Dict:
    variable = {
        "variable": {"name": "Temperature"},
        "characteristics": {"data_type": "decimal", "supports_monitoring": True, "unit": "Celsius"},
        "attributes": [{"type": "Actual", "value": value, "mutability": "ReadOnly"}],
    }
    if monitors:
        variable["monitors"] = [
            {"type": "UpperThreshold", "value": 70, "severity": 4, "monitor_type": "PreconfiguredMonitor"}
        ]
    return variable


def fan_speed_limit_variable(value="80", mutability: Optional[str] = "ReadWrite") -> Dict:
    attribute = {"type": "Actual", "value": value, "persistent": True}
    if mutability is not None:
        attribute["mutability"] = mutability
    return {
        "variable": {"name": "FanSpeedLimit"},
        "characteristics": {"data_type": "integer", "supports_monitoring": False, "unit": "Percent",
                            "min_limit": 0, "max_limit": 100},
        "attributes": [attribute],
    }


def mode_variable(value="Normal") -> Dict:
    return {
        "variable": {"name": "Mode"},
        "characteristics": {"data_type": "OptionList", "supports_monitoring": False,
                            "values_list": "Eco,Normal,Boost"},
        "attributes": [{"type": "Actual", "value": value, "mutability": "ReadWrite"}],
    }


def cooling_unit_registration(owner: Optional[str] = OWNER, variables: Optional[List[Dict]] = None,
                              component: Optional[Dict] = None) -> Dict:
    registration = {
        "components": [{
            "component": component or cooling_unit_component(),
            "variables": variables if variables is not None else
            [temperature_variable(), fan_speed_limit_variable(), mode_variable()],
        }]
    }
    if owner is not None:
        registration["owner"] = owner
    return registration


def release(owner: str = OWNER) -> Dict:
    return {"owner": owner, "components": []}


Reply = Union[None, str, Callable[[Dict], Optional[str]]]


class ExternalOwnerApp:
    """An external process that owns variables of the OCPP device model.

    It keeps its registrations and publishes them again whenever the API module asks with get_registrations, as
    the API documentation asks of every client. It answers writes with the reply policy: a status, None for no
    reply, or a callable that returns either.
    """

    def __init__(self, mqtt_external_prefix: str, module_id: str = API_MODULE_ID, auto_republish: bool = True):
        self.prefix = mqtt_external_prefix
        self.base = f"everest_api/1/ocpp_device_model/{module_id}/"
        self.auto_republish = auto_republish
        self.registrations: Dict[str, Dict] = {}
        self.reply: Reply = "Accepted"
        self.writes: List[Dict] = []
        self.reply_topics: List[str] = []
        self.prompts: List[Dict] = []
        self.results: List[Dict] = []
        self.received_at: Dict[str, List[float]] = {}
        self._retained: List[str] = []
        self._loop: Optional[asyncio.AbstractEventLoop] = None
        self._changed: Optional[asyncio.Condition] = None
        self._lock = threading.Lock()
        self._client = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2)
        self._client.on_message = self._on_message

    @property
    def connected(self) -> bool:
        return self._loop is not None

    def topic(self, suffix: str) -> str:
        return f"{self.prefix}{self.base}{suffix}"

    async def connect(self):
        self._loop = asyncio.get_running_loop()
        self._changed = asyncio.Condition()
        connected = threading.Event()
        self._client.on_connect = lambda *args: connected.set()
        self._client.connect(os.environ.get("MQTT_SERVER_ADDRESS", "localhost"),
                             int(os.environ.get("MQTT_SERVER_PORT", "1883")))
        self._client.loop_start()
        assert await asyncio.to_thread(connected.wait, 5), "MQTT connection timeout"
        subscribed = threading.Event()
        self._client.on_subscribe = lambda *args: subscribed.set()
        self._client.subscribe([(self.topic(f"e2m/{name}"), 1)
                                for name in ("get_registrations", "set_registration_result", "set_value")])
        assert await asyncio.to_thread(subscribed.wait, 5), "MQTT subscription timeout"

    async def disconnect(self):
        self._client.loop_stop()
        self._client.disconnect()

    def clear_retained(self):
        for topic in self._retained:
            self._client.publish(topic, None, retain=True).wait_for_publish(5)
        self._retained.clear()

    def publish(self, suffix: str, payload, retain: bool = False, wait: bool = True):
        topic = self.topic(suffix)
        data = payload if isinstance(payload, str) else json.dumps(payload)
        info = self._client.publish(topic, data, qos=1, retain=retain)
        # the network thread that runs the callbacks would wait for itself
        if wait:
            info.wait_for_publish(5)
        if retain:
            self._retained.append(topic)

    def register(self, registration: Dict, retain: bool = False):
        owner = registration.get("owner", API_MODULE_ID)
        if registration["components"]:
            self.registrations[owner] = registration
        else:
            self.registrations.pop(owner, None)
        self.publish("m2e/registration", registration, retain)

    def report(self, component: Dict, variable: str, value: str, owner: Optional[str] = OWNER):
        change = {"component": component, "variable": {"name": variable}, "attribute_type": "Actual", "value": value}
        if owner is not None:
            change["owner"] = owner
        self.publish("m2e/value_changed", change)

    def send_reply(self, index: int, reply: str):
        self._client.publish(self.reply_topics[index], json.dumps(reply), qos=1).wait_for_publish(5)

    def results_of(self, owner: str) -> List[Dict]:
        with self._lock:
            return [result for result in self.results if result["owner"] == owner]

    async def wait_for(self, predicate: Callable[[], bool], timeout: float, what: str):
        async def _wait():
            async with self._changed:
                await self._changed.wait_for(predicate)

        try:
            await asyncio.wait_for(_wait(), timeout)
        except asyncio.TimeoutError:
            raise AssertionError(f"timeout after {timeout} s waiting for {what}")

    async def wait_for_result(self, owner: str = OWNER, timeout: float = 30, count: int = 1) -> Dict:
        await self.wait_for(lambda: len(self.results_of(owner)) >= count, timeout, f"registration result of {owner}")
        return self.results_of(owner)[count - 1]

    def _on_message(self, client, userdata, msg):
        name = msg.topic.rsplit("/", 1)[-1]
        payload = json.loads(msg.payload) if msg.payload else None
        log.info(f"{name}: {payload}")
        with self._lock:
            self.received_at.setdefault(name, []).append(time.monotonic())
        if name == "get_registrations":
            with self._lock:
                self.prompts.append(payload)
            if self.auto_republish:
                for registration in list(self.registrations.values()):
                    self.publish("m2e/registration", registration, wait=False)
        elif name == "set_registration_result":
            with self._lock:
                self.results.append(payload)
        elif name == "set_value":
            with self._lock:
                self.writes.append(payload["payload"])
                self.reply_topics.append(f"{self.prefix}{payload['headers']['replyTo']}")
            reply = self.reply(payload["payload"]) if callable(self.reply) else self.reply
            if reply is not None:
                self._client.publish(self.reply_topics[-1], json.dumps(reply), qos=1)
        self._loop.call_soon_threadsafe(lambda: asyncio.ensure_future(self._notify()))

    async def _notify(self):
        async with self._changed:
            self._changed.notify_all()


async def start_everest(test_controller: EverestTestController, central_system: CentralSystem,
                        timeout: float = 60):
    """Starts EVerest without blocking the event loop and returns the charge point once it booted"""
    await asyncio.to_thread(test_controller.start)
    return await central_system.wait_for_chargepoint(timeout=timeout)
