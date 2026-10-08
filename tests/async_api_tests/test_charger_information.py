# SPDX-License-Identifier: Apache-2.0
# Copyright Pionix GmbH and Contributors to EVerest

"""External API tests for the charger_information_API bridge.

The bridge forwards get_charger_information as a request on e2m/get_charger_information and
returns the ChargerInformation the external client publishes on the replyTo address.
"""

import json
import os

import pytest
import pytest_asyncio

from everest.testing.core_utils.everest_core import EverestCore
from everest.testing.core_utils.probe_module import ProbeModule
from everest.testing.core_utils.fixtures import *

from test_system import AsyncApiMqttHandler

REQUEST_TOPIC = "everest_api/1/charger_information/charger_information_api/e2m/get_charger_information"

CHARGER_INFORMATION = {
    "vendor": "Pionix",
    "model": "BelayBox",
    "chargepoint_serial": "SH4CAWN00123",
    "chargebox_serial": "CB123456",
    "friendly_name": "Pionix BelayBox [SH4CAWN00123]",
    "manufacturer": "Pionix GmbH",
    "manufacturer_url": "https://pionix.com",
    "model_url": "https://pionix.com/belaybox",
    "model_number": "BB-1",
    "model_revision": "B",
    "board_revision": "1.2",
    "firmware_version": "2026.10.0",
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
    probe_module.start()
    return probe_module


@pytest.mark.asyncio
@pytest.mark.everest_core_config('probe-charger-information.yaml')
async def test_get_charger_information_cmd(everest_core: EverestCore, async_api_mqtt_handler: AsyncApiMqttHandler, probe_module: ProbeModule):

    mqtt_prefix = everest_core.mqtt_external_prefix
    received = {}

    async def on_get_charger_information(payload: str):
        request = json.loads(payload)
        received['request'] = request
        response_topic = request['headers']['replyTo']
        await async_api_mqtt_handler.publish(f"{mqtt_prefix}{response_topic}", json.dumps(CHARGER_INFORMATION))

    async_api_mqtt_handler.register_handler(f"{mqtt_prefix}{REQUEST_TOPIC}", on_get_charger_information)

    result = await probe_module.call_command('charger_information', 'get_charger_information', {})

    assert result == CHARGER_INFORMATION
    # the request carries only the reply address, no payload
    assert 'payload' not in received['request']


@pytest.mark.asyncio
@pytest.mark.everest_core_config('probe-charger-information.yaml')
async def test_get_charger_information_cmd_required_fields_only(everest_core: EverestCore, async_api_mqtt_handler: AsyncApiMqttHandler, probe_module: ProbeModule):

    mqtt_prefix = everest_core.mqtt_external_prefix
    minimal = {"vendor": "Pionix", "model": "BelayBox"}

    async def on_get_charger_information(payload: str):
        request = json.loads(payload)
        await async_api_mqtt_handler.publish(f"{mqtt_prefix}{request['headers']['replyTo']}", json.dumps(minimal))

    async_api_mqtt_handler.register_handler(f"{mqtt_prefix}{REQUEST_TOPIC}", on_get_charger_information)

    result = await probe_module.call_command('charger_information', 'get_charger_information', {})

    assert result == minimal


@pytest.mark.asyncio
@pytest.mark.everest_core_config('probe-charger-information-fast-timeout.yaml')
async def test_get_charger_information_empty_on_timeout(everest_core: EverestCore, async_api_mqtt_handler: AsyncApiMqttHandler, probe_module: ProbeModule):

    # No e2m/get_charger_information handler registered -> the request times out after
    # cfg_request_reply_to_s (=1s in this config) and degrades to empty vendor/model.
    result = await probe_module.call_command('charger_information', 'get_charger_information', {})

    assert result == {"vendor": "", "model": ""}
