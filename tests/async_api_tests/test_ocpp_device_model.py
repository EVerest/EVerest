# SPDX-License-Identifier: Apache-2.0
# Copyright Pionix GmbH and Contributors to EVerest

"""External API tests for the ocpp_device_model_API bridge.

The probe module stands in for the OCPP module and calls the commands of ocpp_device_model; the test acts as the
external client on the MQTT topics of the API.
"""

import asyncio
import json
import os
import time

import pytest
import pytest_asyncio

from everest.testing.core_utils.everest_core import EverestCore
from everest.testing.core_utils.probe_module import ProbeModule
from everest.testing.core_utils.fixtures import *

from test_system import AsyncApiMqttHandler

API_PREFIX = "everest_api/1/ocpp_device_model/device_model_api"
CONNECTION = "device_model"


def registration(owner, component="Powermeter", evse_id=1):
    return {
        "owner": owner,
        "components": [{
            "component": {"name": component, "evse": {"id": evse_id}},
            "variables": [{
                "variable": {"name": "Enabled"},
                "characteristics": {"data_type": "boolean", "supports_monitoring": False},
                "attributes": [{"type": "Actual", "value": "true", "mutability": "ReadWrite"}],
            }],
        }],
    }


WRITE_REQUEST = {
    "owner": "meter_1",
    "component": {"name": "Powermeter", "evse": {"id": 1}},
    "variable": {"name": "Enabled"},
    "attribute_type": "Actual",
    "value": "false",
    "source": "csms",
}


@pytest_asyncio.fixture
async def client(everest_core: EverestCore) -> AsyncApiMqttHandler:
    broker = os.environ.get("MQTT_SERVER_ADDRESS", "localhost")
    handler = AsyncApiMqttHandler(broker, 1883, everest_core.mqtt_external_prefix)
    await handler.start()
    yield handler
    await handler.stop()


@pytest_asyncio.fixture
async def probe_module(everest_core: EverestCore) -> ProbeModule:
    everest_core.start(standalone_module="probe")
    probe_module = ProbeModule(everest_core.get_runtime_session())
    probe_module.start()
    await probe_module.wait_to_be_ready()
    return probe_module


def topic(everest_core: EverestCore, suffix: str) -> str:
    return f"{everest_core.mqtt_external_prefix}{API_PREFIX}/{suffix}"


async def publish(client: AsyncApiMqttHandler, everest_core: EverestCore, suffix: str, payload) -> None:
    await client.publish(topic(everest_core, suffix), json.dumps(payload))


async def capture(client: AsyncApiMqttHandler, everest_core: EverestCore, suffix: str) -> asyncio.Queue:
    received = asyncio.Queue()

    async def on_message(payload: str):
        await received.put(json.loads(payload))

    await client.register_handler(topic(everest_core, suffix), on_message)
    return received


def owners_of(registrations: dict) -> list:
    return sorted(r.get("owner", "device_model_api") for r in registrations["registrations"])


@pytest.mark.asyncio
@pytest.mark.everest_core_config('probe-ocpp-device-model.yaml')
async def test_get_registrations_answers_when_every_expected_owner_registered(
        everest_core: EverestCore, client: AsyncApiMqttHandler, probe_module: ProbeModule):
    prompts = asyncio.Queue()

    async def on_prompt(payload: str):
        await prompts.put(json.loads(payload))
        await publish(client, everest_core, "m2e/registration", registration("meter_1"))
        await publish(client, everest_core, "m2e/registration", registration("meter_2", evse_id=2))

    await client.register_handler(topic(everest_core, "e2m/get_registrations"), on_prompt)

    start = time.monotonic()
    result = await probe_module.call_command(CONNECTION, "get_registrations", {})
    waited = time.monotonic() - start

    assert await asyncio.wait_for(prompts.get(), 1) == {"expected_owners": ["meter_1", "meter_2"]}
    assert owners_of(result) == ["meter_1", "meter_2"]
    assert waited < 2, "answered at the deadline instead of when the last owner registered"


@pytest.mark.asyncio
@pytest.mark.everest_core_config('probe-ocpp-device-model.yaml')
async def test_get_registrations_answers_without_missing_owner_at_deadline(
        everest_core: EverestCore, client: AsyncApiMqttHandler, probe_module: ProbeModule):
    registrations = probe_module.subscribe_variable_to_queue(CONNECTION, "registration")
    # a registration beyond a length limit of the interface is dropped instead of being part of the answer,
    # which fails as a whole when EVerest validates it (validate_schema)
    too_long = registration("meter_2", evse_id=2)
    too_long["components"][0]["variables"][0]["characteristics"]["unit"] = "x" * 17
    await publish(client, everest_core, "m2e/registration", too_long)
    await publish(client, everest_core, "m2e/registration", registration("meter_1"))

    start = time.monotonic()
    result = await probe_module.call_command(CONNECTION, "get_registrations", {})
    waited = time.monotonic() - start

    assert result is not None, "get_registrations failed"
    assert owners_of(result) == ["meter_1"]
    assert waited >= 2

    # an owner that missed the deadline still registers at runtime
    late = registration("meter_2", evse_id=2)
    await publish(client, everest_core, "m2e/registration", late)
    assert await asyncio.to_thread(registrations.get, timeout=5) == late
    assert registrations.empty(), "a registration before the answer was passed on to the var"


@pytest.mark.asyncio
@pytest.mark.everest_core_config('probe-ocpp-device-model-no-owners.yaml')
async def test_get_registrations_collects_for_the_registration_timeout_without_expected_owners(
        everest_core: EverestCore, client: AsyncApiMqttHandler, probe_module: ProbeModule):
    prompts = asyncio.Queue()

    async def on_prompt(payload: str):
        await prompts.put(json.loads(payload))
        await publish(client, everest_core, "m2e/registration", registration("fan_1", "AuxiliaryFan"))

    await client.register_handler(topic(everest_core, "e2m/get_registrations"), on_prompt)

    start = time.monotonic()
    result = await probe_module.call_command(CONNECTION, "get_registrations", {})
    waited = time.monotonic() - start

    assert await asyncio.wait_for(prompts.get(), 1) == {"expected_owners": []}
    assert owners_of(result) == ["fan_1"]
    assert waited >= 1


@pytest.mark.asyncio
@pytest.mark.everest_core_config('probe-ocpp-device-model.yaml')
async def test_set_registration_result_is_published(
        everest_core: EverestCore, client: AsyncApiMqttHandler, probe_module: ProbeModule):
    results = await capture(client, everest_core, "e2m/set_registration_result")
    result = {
        "owner": "meter_1",
        "status": "Accepted",
        "rejected": [{
            "component": {"name": "Powermeter", "evse": {"id": 1}},
            "variable": {"name": "Enabled"},
            "reason": "AlreadyDefined",
            "defined_by": "COMPONENT_CONFIG",
        }],
    }

    await probe_module.call_command(CONNECTION, "set_registration_result", {"result": result})

    assert await asyncio.wait_for(results.get(), 5) == result


@pytest.mark.asyncio
@pytest.mark.everest_core_config('probe-ocpp-device-model.yaml')
async def test_set_value_returns_the_reply_of_the_client(
        everest_core: EverestCore, client: AsyncApiMqttHandler, probe_module: ProbeModule):
    requests = asyncio.Queue()

    async def on_set_value(payload: str):
        request = json.loads(payload)
        await requests.put(request)
        await client.publish(f"{everest_core.mqtt_external_prefix}{request['headers']['replyTo']}",
                             json.dumps("RebootRequired"))

    await client.register_handler(topic(everest_core, "e2m/set_value"), on_set_value)

    result = await probe_module.call_command(CONNECTION, "set_value", {"request": WRITE_REQUEST})

    assert result == "RebootRequired"
    assert (await asyncio.wait_for(requests.get(), 1))["payload"] == WRITE_REQUEST


@pytest.mark.asyncio
@pytest.mark.everest_core_config('probe-ocpp-device-model.yaml')
async def test_set_value_is_rejected_without_a_reply(
        everest_core: EverestCore, client: AsyncApiMqttHandler, probe_module: ProbeModule):
    result = await probe_module.call_command(CONNECTION, "set_value", {"request": WRITE_REQUEST})

    assert result == "Rejected"


@pytest.mark.asyncio
@pytest.mark.everest_core_config('probe-ocpp-device-model.yaml')
async def test_value_changed_is_passed_on(
        everest_core: EverestCore, client: AsyncApiMqttHandler, probe_module: ProbeModule):
    changes = probe_module.subscribe_variable_to_queue(CONNECTION, "value_changed")
    change = {
        "owner": "meter_1",
        "component": {"name": "Powermeter", "evse": {"id": 1}},
        "variable": {"name": "Enabled"},
        "value": "false",
    }

    await publish(client, everest_core, "m2e/value_changed", change)

    assert await asyncio.to_thread(changes.get, timeout=5) == change
