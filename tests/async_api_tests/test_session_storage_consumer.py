# SPDX-License-Identifier: Apache-2.0
# Copyright Pionix GmbH and Contributors to EVerest

"""External API tests for the session_storage_consumer_API bridge.

The bridge exposes the three session_storage commands as request/reply topics under
everest_api/1/session_storage_consumer/<module_id>. Smoke tested here is the full
round trip external request -> internal command -> external reply, plus the
unwrapping the bridge does on top of the internal interface: an absent request payload
is the default page request, get_session replies with the bare session record or with
JSON null when nothing matches, and clear_sessions replies with the deleted count.
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

API_BASE = "everest_api/1/session_storage_consumer/session_storage_api"

# a finished session with everything the record can carry apart from cost, which needs
# no coverage beyond its own codec tests
SESSION = {
    "session_id": "550e8400-e29b-41d4-a716-446655440000",
    "evse_id": 2,
    "evse_id_string": "DE*PNX*E12345*2",
    "connector_id": 1,
    "state": "Finished",
    "timestamp_start": "2026-08-21T09:59:30.000Z",
    "timestamp_stop": "2026-08-21T11:00:10.000Z",
    "start_reason": "EVConnected",
    "transaction": {
        "timestamp_start": "2026-08-21T10:00:00.000Z",
        "timestamp_stop": "2026-08-21T11:00:00.000Z",
        "energy_Wh_import_start": 1234.5,
        "energy_Wh_import_stop": 5678.25,
        "id_token_hash": "3d0c1a9f4b7e2d5608a1c3f9e7b4d2601f8a5c3e9b7d4a2f6c0e8b1d5a9f7c3e",
        "id_token_type": "ISO14443",
        "authorization_type": "RFID",
        "stop_reason": "Local",
        "signed_meter_value_start": {
            "signed_meter_data": "OCMF|{\"RD\":[{\"TM\":\"2026-08-21T10:00:00,000+0000 S\"}]}",
            "signing_method": "ECDSA-secp256r1-SHA256",
            "encoding_method": "OCMF",
            "public_key": "3059301306072a8648ce3d020106082a8648ce3d030107",
            "timestamp": "2026-08-21T10:00:00.000Z",
        },
        "signed_meter_value_stop": {
            "signed_meter_data": "OCMF|{\"RD\":[{\"TM\":\"2026-08-21T11:00:00,000+0000 S\"}]}",
            "signing_method": "ECDSA-secp256r1-SHA256",
            "encoding_method": "OCMF",
        },
    },
    "ocpp_transaction_id": "1742",
    "ocpp_transaction_timestamp_start": "2026-08-21T10:00:01.000Z",
    "ocpp_transaction_timestamp_stop": "2026-08-21T11:00:02.000Z",
}

_NO_PAYLOAD = object()


class SessionStorageBackend:
    """Records the internal requests and serves the replies a test configures."""

    def __init__(self):
        self.requests = {}
        self.session_list = {"sessions": []}
        self.session_result = {}
        self.cleared = 0

    def get_sessions(self, args: dict) -> dict:
        self.requests["get_sessions"] = args
        return self.session_list

    def get_session(self, args: dict) -> dict:
        self.requests["get_session"] = args
        return self.session_result

    def clear_sessions(self, args: dict) -> dict:
        self.requests["clear_sessions"] = args
        return {"cleared": self.cleared}


@pytest_asyncio.fixture
async def async_api_mqtt_handler(everest_core: EverestCore) -> AsyncApiMqttHandler:
    broker = os.environ.get("MQTT_SERVER_ADDRESS", "localhost")
    handler = AsyncApiMqttHandler(broker, 1883, everest_core.mqtt_external_prefix)
    await handler.start()
    yield handler
    await handler.stop()


@pytest.fixture
def backend() -> SessionStorageBackend:
    return SessionStorageBackend()


@pytest.fixture
def probe_module(everest_core: EverestCore, backend: SessionStorageBackend) -> ProbeModule:
    everest_core.start(standalone_module="probe")
    probe_module = ProbeModule(everest_core.get_runtime_session())

    probe_module.implement_command(
        "ProbeModuleSessionStorage", "get_sessions", backend.get_sessions)
    probe_module.implement_command(
        "ProbeModuleSessionStorage", "get_session", backend.get_session)
    probe_module.implement_command(
        "ProbeModuleSessionStorage", "clear_sessions", backend.clear_sessions)

    probe_module.start()
    return probe_module


def _subscribe_to_queue(handler: AsyncApiMqttHandler, topic: str) -> Queue:
    """Collects the payloads published on an external topic."""
    queue = Queue()

    async def on_message(payload: str):
        queue.put(json.loads(payload))

    handler.register_handler(topic, on_message)
    return queue


async def _request_until_reply(handler: AsyncApiMqttHandler, prefix: str, command: str,
                               payload=_NO_PAYLOAD, deadline: float = 15.0):
    """Sends a request until its reply arrives and returns the reply.

    The bridge subscribes its m2e topics in ready(), so a request published before that
    is lost. The request is repeated until the reply topic answers.
    """
    reply_topic = f"{API_BASE}/e2m/{command}/{uuid.uuid4()}"
    queue = _subscribe_to_queue(handler, f"{prefix}{reply_topic}")

    request = {"headers": {"replyTo": reply_topic}}
    if payload is not _NO_PAYLOAD:
        request["payload"] = payload

    loop = asyncio.get_event_loop()
    end = loop.time() + deadline
    while loop.time() < end:
        await handler.publish(
            f"{prefix}{API_BASE}/m2e/{command}", json.dumps(request))
        try:
            return await loop.run_in_executor(None, lambda: queue.get(timeout=0.5))
        except Empty:
            continue
    raise TimeoutError(f"no reply received for {command}")


@pytest.mark.asyncio
@pytest.mark.everest_core_config("probe-session-storage-consumer.yaml")
async def test_get_sessions_forwards_the_request_and_returns_the_page(
    everest_core: EverestCore,
    async_api_mqtt_handler: AsyncApiMqttHandler,
    backend: SessionStorageBackend,
    probe_module: ProbeModule,
):
    page = {"sessions": [SESSION], "continuation_token": "5f2a9c31d4e8b0a7:184"}
    backend.session_list = page

    external_request = {
        "limit": 25,
        "filter": {
            "state": "Finished",
            "evse_id": 2,
            "started_after": "2026-08-01T00:00:00Z",
        },
    }
    reply = await _request_until_reply(
        async_api_mqtt_handler, everest_core.mqtt_external_prefix,
        "get_sessions", external_request)

    assert backend.requests["get_sessions"] == {"request": external_request}
    assert reply == page


@pytest.mark.asyncio
@pytest.mark.everest_core_config("probe-session-storage-consumer.yaml")
async def test_get_sessions_without_payload_requests_the_default_page(
    everest_core: EverestCore,
    async_api_mqtt_handler: AsyncApiMqttHandler,
    backend: SessionStorageBackend,
    probe_module: ProbeModule,
):
    reply = await _request_until_reply(
        async_api_mqtt_handler, everest_core.mqtt_external_prefix, "get_sessions")

    # no member set means first page, default page size, no filter
    assert backend.requests["get_sessions"] == {"request": {}}
    assert reply == {"sessions": []}


@pytest.mark.asyncio
@pytest.mark.everest_core_config("probe-session-storage-consumer.yaml")
async def test_get_session_replies_with_the_bare_record_or_null(
    everest_core: EverestCore,
    async_api_mqtt_handler: AsyncApiMqttHandler,
    backend: SessionStorageBackend,
    probe_module: ProbeModule,
):
    prefix = everest_core.mqtt_external_prefix
    backend.session_result = {"session": SESSION}

    identifier = {"session_id": SESSION["session_id"]}
    reply = await _request_until_reply(
        async_api_mqtt_handler, prefix, "get_session", identifier)

    assert backend.requests["get_session"] == {"identifier": identifier}
    # the internal SessionResult wrapper is unwrapped by the bridge
    assert reply == SESSION

    # a lookup without a match answers with the JSON literal null
    backend.session_result = {}
    reply = await _request_until_reply(
        async_api_mqtt_handler, prefix, "get_session",
        {"ocpp_transaction_id": "does-not-exist"})
    assert reply is None


@pytest.mark.asyncio
@pytest.mark.everest_core_config("probe-session-storage-consumer.yaml")
async def test_clear_sessions_replies_with_the_deleted_count(
    everest_core: EverestCore,
    async_api_mqtt_handler: AsyncApiMqttHandler,
    backend: SessionStorageBackend,
    probe_module: ProbeModule,
):
    backend.cleared = 7

    reply = await _request_until_reply(
        async_api_mqtt_handler, everest_core.mqtt_external_prefix, "clear_sessions")

    assert reply == {"cleared": 7}
