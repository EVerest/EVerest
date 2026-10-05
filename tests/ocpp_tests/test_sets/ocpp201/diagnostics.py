# SPDX-License-Identifier: Apache-2.0
# Copyright Pionix GmbH and Contributors to EVerest

import asyncio
import logging

import pytest
from everest.testing.ocpp_utils.charge_point_utils import (
    TestUtility,
    wait_for_and_validate,
)
from everest.testing.ocpp_utils.charge_point_v201 import ChargePoint201
from everest.testing.ocpp_utils.fixtures import central_system_v201, charge_point_v201
from ocpp.messages import MessageType, unpack
from ocpp.v201.datatypes import LogParametersType
from ocpp.v201.enums import LogEnumType, LogStatusEnumType

STATUS_TIMEOUT_S = 10

UPLOAD_OK = b"HTTP/1.1 200 OK\r\nContent-Length: 0\r\nConnection: close\r\n\r\n"


@pytest.mark.asyncio
@pytest.mark.ocpp_version("ocpp2.0.1")
@pytest.mark.everest_core_config("everest-config-ocpp201.yaml")
async def test_N01_get_log_cancels_running_upload(
    charge_point_v201: ChargePoint201,
    test_utility: TestUtility,
):
    """N01.FR.20: a second GetLog cancels the running upload, which then reports only AcceptedCanceled."""
    logging.info("######### test_N01_get_log_cancels_running_upload #########")

    release_first_upload = asyncio.Event()
    connections = 0

    async def handle_upload(reader: asyncio.StreamReader, writer: asyncio.StreamWriter):
        nonlocal connections
        connections += 1
        is_first = connections == 1
        await reader.read(65536)
        if is_first:
            await release_first_upload.wait()
        writer.write(UPLOAD_OK)
        await writer.drain()
        writer.close()

    server = await asyncio.start_server(handle_upload, "127.0.0.1", 0)
    port = server.sockets[0].getsockname()[1]
    log = LogParametersType(remote_location=f"http://127.0.0.1:{port}/diagnostics")

    try:
        r = await charge_point_v201.get_log_req(log=log, log_type=LogEnumType.diagnostics_log, request_id=1)
        assert LogStatusEnumType(r.status) == LogStatusEnumType.accepted
        assert await wait_for_and_validate(
            test_utility, charge_point_v201, "LogStatusNotification", {"status": "Uploading", "requestId": 1}
        )

        history = charge_point_v201.message_history.messages
        cancel_index = len(history)
        r = await charge_point_v201.get_log_req(log=log, log_type=LogEnumType.diagnostics_log, request_id=2)
        assert LogStatusEnumType(r.status) == LogStatusEnumType.accepted_canceled

        # The cancel stops the uploader script, but its curl keeps the output pipe open until the held
        # upload ends, so release it.
        release_first_upload.set()

        assert await wait_for_and_validate(
            test_utility,
            charge_point_v201,
            "LogStatusNotification",
            {"status": "AcceptedCanceled", "requestId": 1},
            timeout=STATUS_TIMEOUT_S,
        )
        assert await wait_for_and_validate(
            test_utility,
            charge_point_v201,
            "LogStatusNotification",
            {"status": "Uploading", "requestId": 2},
            timeout=STATUS_TIMEOUT_S,
        )

        received = [unpack(m.message) for m in history[cancel_index:] if m.initiator == "Chargepoint"]
        cancelled_request_statuses = [
            msg.payload["status"]
            for msg in received
            if msg.message_type_id == MessageType.Call
            and msg.action == "LogStatusNotification"
            and msg.payload["requestId"] == 1
        ]
        assert cancelled_request_statuses == ["AcceptedCanceled"]

        assert await wait_for_and_validate(
            test_utility,
            charge_point_v201,
            "LogStatusNotification",
            {"status": "Uploaded", "requestId": 2},
            timeout=STATUS_TIMEOUT_S,
        )
    finally:
        release_first_upload.set()
        server.close()
        await server.wait_closed()
