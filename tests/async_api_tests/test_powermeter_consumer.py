# SPDX-License-Identifier: Apache-2.0
# Copyright Pionix GmbH and Contributors to EVerest

"""External API tests for the powermeter_consumer_API bridge.

The bridge forwards everything a powermeter module publishes to e2m/<var> under
everest_api/1/powermeter_consumer/<module_id>, and answers m2e/<var>/get with the
latched value (JSON null before anything was published). The probe module plays
the powermeter.
"""

import asyncio
import json
import os
import uuid
from queue import Empty, Queue

import pytest
import pytest_asyncio

from everest.testing.core_utils.everest_core import EverestCore
from everest.testing.core_utils.probe_module import ProbeModule
from everest.testing.core_utils.fixtures import *

from test_system import AsyncApiMqttHandler

API_BASE = "everest_api/1/powermeter_consumer/powermeter_api"
IMPLEMENTATION = "ProbeModulePowermeter"

POWERMETER = {
    "timestamp": "2026-09-28T10:00:00.000Z",
    "meter_id": "EM340",
    "energy_Wh_import": {"total": 12345.5},
    "energy_Wh_export": {"total": 0.0},
    "power_W": {"total": 6900.0},
    "voltage_V": {"L1": 230.1, "L2": 229.8, "L3": 230.4},
    "current_A": {"L1": 10.0, "L2": 10.5, "L3": 9.5},
    "frequency_Hz": {"L1": 50.0},
}

CAPABILITIES = {"min_import_current_A": 0.25}


@pytest_asyncio.fixture
async def async_api_mqtt_handler(everest_core: EverestCore) -> AsyncApiMqttHandler:
    broker = os.environ.get("MQTT_SERVER_ADDRESS", "localhost")
    handler = AsyncApiMqttHandler(broker, 1883, everest_core.mqtt_external_prefix)
    await handler.start()
    yield handler
    await handler.stop()


@pytest.fixture
def probe_module(everest_core: EverestCore) -> ProbeModule:
    everest_core.start(standalone_module="probe")
    probe_module = ProbeModule(everest_core.get_runtime_session())
    # The powermeter interface's commands. The bridge forwards no command, so
    # these only have to exist.
    probe_module.implement_command(IMPLEMENTATION, "start_transaction",
                                   lambda args: {"status": "NOT_SUPPORTED"})
    probe_module.implement_command(IMPLEMENTATION, "stop_transaction",
                                   lambda args: {"status": "NOT_SUPPORTED"})
    probe_module.start()
    return probe_module


def _subscribe_to_queue(handler: AsyncApiMqttHandler, topic: str) -> Queue:
    """Collects the payloads published on an external topic."""
    queue = Queue()

    async def on_message(payload: str):
        queue.put(json.loads(payload))

    handler.register_handler(topic, on_message)
    return queue


async def _publish_until_forwarded(handler: AsyncApiMqttHandler, probe_module: ProbeModule, prefix: str,
                                   var: str, value, deadline: float = 15.0):
    """Publishes a variable from the probe until the bridge forwards it, and returns what it forwarded.

    The external subscription may not be in place when the first publication happens, so the
    variable is published again until it arrives.
    """
    queue = _subscribe_to_queue(handler, f"{prefix}{API_BASE}/e2m/{var}")
    loop = asyncio.get_event_loop()
    end = loop.time() + deadline
    while loop.time() < end:
        probe_module.publish_variable(IMPLEMENTATION, var, value)
        try:
            return await loop.run_in_executor(None, lambda: queue.get(timeout=0.5))
        except Empty:
            continue
    raise TimeoutError(f"{var} was not forwarded")


async def _request_until_reply(handler: AsyncApiMqttHandler, prefix: str, var: str, deadline: float = 15.0):
    """Asks for a latched value until the reply arrives, and returns the reply.

    The bridge subscribes its m2e topics while starting up, so a request published before that is
    lost. The request is repeated until the reply topic answers.
    """
    reply_topic = f"{API_BASE}/e2m/{var}/get/{uuid.uuid4()}"
    queue = _subscribe_to_queue(handler, f"{prefix}{reply_topic}")
    request = {"headers": {"replyTo": reply_topic}}

    loop = asyncio.get_event_loop()
    end = loop.time() + deadline
    while loop.time() < end:
        await handler.publish(f"{prefix}{API_BASE}/m2e/{var}/get", json.dumps(request))
        try:
            return await loop.run_in_executor(None, lambda: queue.get(timeout=0.5))
        except Empty:
            continue
    raise TimeoutError(f"no reply received for {var}/get")


def _assert_same_powermeter(forwarded: dict, published: dict):
    assert forwarded["timestamp"] == published["timestamp"]
    assert forwarded["meter_id"] == published["meter_id"]
    assert forwarded["energy_Wh_import"]["total"] == pytest.approx(published["energy_Wh_import"]["total"])
    assert forwarded["power_W"]["total"] == pytest.approx(published["power_W"]["total"])
    for phase in ("L1", "L2", "L3"):
        assert forwarded["current_A"][phase] == pytest.approx(published["current_A"][phase])
        assert forwarded["voltage_V"][phase] == pytest.approx(published["voltage_V"][phase])


@pytest.mark.asyncio
@pytest.mark.everest_core_config("probe-powermeter-consumer.yaml")
async def test_powermeter_values_are_forwarded(everest_core: EverestCore,
                                               async_api_mqtt_handler: AsyncApiMqttHandler,
                                               probe_module: ProbeModule):
    forwarded = await _publish_until_forwarded(async_api_mqtt_handler, probe_module,
                                               everest_core.mqtt_external_prefix, "powermeter", POWERMETER)
    _assert_same_powermeter(forwarded, POWERMETER)


@pytest.mark.asyncio
@pytest.mark.everest_core_config("probe-powermeter-consumer.yaml")
async def test_get_powermeter_answers_null_before_the_first_value(everest_core: EverestCore,
                                                                   async_api_mqtt_handler: AsyncApiMqttHandler,
                                                                   probe_module: ProbeModule):
    reply = await _request_until_reply(async_api_mqtt_handler, everest_core.mqtt_external_prefix, "powermeter")
    assert reply is None


@pytest.mark.asyncio
@pytest.mark.everest_core_config("probe-powermeter-consumer.yaml")
async def test_get_powermeter_answers_the_latched_value(everest_core: EverestCore,
                                                        async_api_mqtt_handler: AsyncApiMqttHandler,
                                                        probe_module: ProbeModule):
    prefix = everest_core.mqtt_external_prefix
    await _publish_until_forwarded(async_api_mqtt_handler, probe_module, prefix, "powermeter", POWERMETER)

    reply = await _request_until_reply(async_api_mqtt_handler, prefix, "powermeter")
    _assert_same_powermeter(reply, POWERMETER)


@pytest.mark.asyncio
@pytest.mark.everest_core_config("probe-powermeter-consumer.yaml")
async def test_public_key_and_capabilities_are_forwarded(everest_core: EverestCore,
                                                         async_api_mqtt_handler: AsyncApiMqttHandler,
                                                         probe_module: ProbeModule):
    prefix = everest_core.mqtt_external_prefix

    key = await _publish_until_forwarded(async_api_mqtt_handler, probe_module, prefix, "public_key_ocmf",
                                         "3059301306072a8648ce3d020106082a8648ce3d030107")
    assert key == "3059301306072a8648ce3d020106082a8648ce3d030107"

    capabilities = await _publish_until_forwarded(async_api_mqtt_handler, probe_module, prefix, "capabilities",
                                                  CAPABILITIES)
    assert capabilities["min_import_current_A"] == pytest.approx(CAPABILITIES["min_import_current_A"])
