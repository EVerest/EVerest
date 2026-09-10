# SPDX-License-Identifier: Apache-2.0
# Copyright Pionix GmbH and Contributors to EVerest

"""External API tests for the connection status of the ocpp_consumer_API bridge.

The bridge forwards the ocpp interface var connection_status to e2m/connection_status and
additionally derives the deprecated boolean e2m/is_connected from its connected property,
so external consumers written against is_connected keep working unchanged.
"""

import asyncio
import json
import os
from queue import Empty, Queue

import pytest
import pytest_asyncio

from everest.testing.core_utils.everest_core import EverestCore
from everest.testing.core_utils.probe_module import ProbeModule
from everest.testing.core_utils.fixtures import *

from test_system import AsyncApiMqttHandler

API_BASE = "everest_api/1/ocpp_consumer/ocpp_api"

CONNECTED_STATUS = {
    "connected": True,
    "csms_url": "ws://localhost:9000",
    "identity": "cp001",
    "security_profile": 1,
    "configuration_slot": 2,
    "ocpp_interface": "Wired0",
    "ocpp_transport": "JSON",
    "ocpp_version": "2.0.1",
}


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

    # the probe provides the ocpp interfaces the bridge requires, so it has to implement
    # all their commands even though this test only exercises published variables
    probe_module.implement_command("ProbeModuleOcpp", "stop", lambda arg: True)
    probe_module.implement_command("ProbeModuleOcpp", "restart", lambda arg: True)
    probe_module.implement_command(
        "ProbeModuleOcpp", "security_event", lambda arg: None)
    probe_module.implement_command(
        "ProbeModuleOcpp", "monitor_variables", lambda arg: None)
    probe_module.implement_command("ProbeModuleOcpp", "get_variables", lambda arg: [])
    probe_module.implement_command("ProbeModuleOcpp", "set_variables", lambda arg: [])
    probe_module.implement_command(
        "ProbeModuleOcpp", "change_availability", lambda arg: {"status": "Accepted"})
    probe_module.implement_command(
        "ProbeModuleOcppDataTransfer", "data_transfer", lambda arg: {"status": "Accepted"})

    probe_module.start()
    return probe_module


def _subscribe_to_queue(handler: AsyncApiMqttHandler, topic: str) -> Queue:
    """Collects the payloads published on an external topic."""
    queue = Queue()

    async def on_message(payload: str):
        queue.put(json.loads(payload))

    handler.register_handler(topic, on_message)
    return queue


async def _get_from_queue(queue: Queue, deadline: float = 10.0):
    loop = asyncio.get_event_loop()
    end = loop.time() + deadline
    while loop.time() < end:
        try:
            return await loop.run_in_executor(None, lambda: queue.get(timeout=0.5))
        except Empty:
            continue
    raise TimeoutError("no value received on the external topic")


async def _publish_status_until_received(probe_module: ProbeModule, queues, status: dict,
                                        deadline: float = 15.0):
    """Publishes the internal var until every external topic has answered.

    The internal publication is a one-shot event, so it is repeated until the bridge has
    subscribed and forwarded it. Returns the first payload received per queue.
    """
    loop = asyncio.get_event_loop()
    end = loop.time() + deadline
    received = [None] * len(queues)
    while loop.time() < end:
        probe_module.publish_variable(
            "ProbeModuleOcpp", "connection_status", status)
        for index, queue in enumerate(queues):
            if received[index] is not None:
                continue
            try:
                received[index] = await loop.run_in_executor(
                    None, lambda queue=queue: queue.get(timeout=0.5))
            except Empty:
                continue
        if all(value is not None for value in received):
            return received
    raise TimeoutError(f"connection status was not forwarded: {received}")


@pytest.mark.asyncio
@pytest.mark.everest_core_config("probe-ocpp-consumer.yaml")
async def test_connection_status_is_forwarded_with_legacy_is_connected(
    everest_core: EverestCore,
    async_api_mqtt_handler: AsyncApiMqttHandler,
    probe_module: ProbeModule,
):
    prefix = everest_core.mqtt_external_prefix
    status_queue = _subscribe_to_queue(
        async_api_mqtt_handler, f"{prefix}{API_BASE}/e2m/connection_status")
    is_connected_queue = _subscribe_to_queue(
        async_api_mqtt_handler, f"{prefix}{API_BASE}/e2m/is_connected")

    # the connection status is forwarded verbatim, is_connected repeats its connected flag
    status, is_connected = await _publish_status_until_received(
        probe_module, [status_queue, is_connected_queue], CONNECTED_STATUS)
    assert status == CONNECTED_STATUS
    assert is_connected is True

    # both variables are published on a disconnect as well, keeping the details of the
    # connection that was just lost
    disconnected_status = dict(CONNECTED_STATUS, connected=False)
    probe_module.publish_variable(
        "ProbeModuleOcpp", "connection_status", disconnected_status)
    assert await _get_from_queue(status_queue) == disconnected_status
    assert await _get_from_queue(is_connected_queue) is False


@pytest.mark.asyncio
@pytest.mark.everest_core_config("probe-ocpp-consumer.yaml")
async def test_latched_connection_status_and_is_connected_are_served(
    everest_core: EverestCore,
    async_api_mqtt_handler: AsyncApiMqttHandler,
    probe_module: ProbeModule,
):
    prefix = everest_core.mqtt_external_prefix
    status_queue = _subscribe_to_queue(
        async_api_mqtt_handler, f"{prefix}{API_BASE}/e2m/connection_status")
    is_connected_queue = _subscribe_to_queue(
        async_api_mqtt_handler, f"{prefix}{API_BASE}/e2m/is_connected")

    await _publish_status_until_received(
        probe_module, [status_queue, is_connected_queue], CONNECTED_STATUS)

    for variable, expected in (("connection_status", CONNECTED_STATUS),
                               ("is_connected", True)):
        reply_topic = f"{API_BASE}/e2m/{variable}/reply"
        reply_queue = _subscribe_to_queue(
            async_api_mqtt_handler, f"{prefix}{reply_topic}")
        await async_api_mqtt_handler.publish(
            f"{prefix}{API_BASE}/m2e/{variable}/get",
            json.dumps({"headers": {"replyTo": reply_topic}}))
        assert await _get_from_queue(reply_queue) == expected
