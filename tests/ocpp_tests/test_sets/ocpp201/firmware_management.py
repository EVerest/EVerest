# SPDX-License-Identifier: Apache-2.0
# Copyright Pionix GmbH and Contributors to EVerest

import asyncio
import getpass
import logging
import os
import socket
import sys
from datetime import datetime, timezone
from pathlib import Path

import pytest
from everest.testing.core_utils.controller.test_controller_interface import (
    TestController,
)
from everest.testing.core_utils._configuration.everest_configuration_strategies.disable_reset_after_update_strategy import \
    DisableResetAfterUpdateStrategy

sys.path.append(os.path.abspath(
    os.path.join(os.path.dirname(__file__), "../..")))
from everest.testing.ocpp_utils.charge_point_utils import (
    TestUtility,
    ValidationMode,
    wait_for_and_validate,
)
from everest.testing.ocpp_utils.charge_point_v201 import ChargePoint201
from everest.testing.ocpp_utils.fixtures import (
    central_system_v201,
    charge_point_v201,
    ftp_server,
)
from everest_test_utils import OcppTestConfiguration
from ocpp.messages import MessageType, unpack
from ocpp.v201 import call as call201
from ocpp.v201 import call_result as call_result201
from ocpp.v201.datatypes import *
from ocpp.v201.enums import ConnectorStatusEnumType, UpdateFirmwareStatusEnumType
from validations import validate_status_notification_201

STATUS_TIMEOUT_S = 10


@pytest.mark.asyncio
@pytest.mark.ocpp_version("ocpp2.0.1")
@pytest.mark.everest_core_config("everest-config-ocpp201.yaml")
@pytest.mark.xdist_group(name="FTP")
async def test_L01_secure_firmware_update_disable_connectors(
    test_config: OcppTestConfiguration,
    charge_point_v201: ChargePoint201,
    test_controller: TestController,
    test_utility: TestUtility,
    ftp_server,
):
    """L01 - Secure Firmware Update"""
    logging.info(
        "######### test_L01_secure_firmware_update_disable_connectors #########")

    os.system(
        f"curl -T {Path(__file__).parent.parent / test_config.firmware_info.update_file} ftp://{getpass.getuser()}:12345@localhost:{ftp_server.port}"
    )

    location = f"ftp://{getpass.getuser()}:12345@localhost:{ftp_server.port}/firmware_update.pnx"
    retrieve_date_time = datetime.now(timezone.utc)
    mf_root_ca = open(test_config.certificate_info.mf_root_ca).read()
    fw_signature = open(test_config.firmware_info.update_file_signature).read()

    firmware = FirmwareType(
        location=location,
        retrieve_date_time=retrieve_date_time.isoformat(),
        signing_certificate=mf_root_ca,
        signature=fw_signature,
    )

    await charge_point_v201.update_firmware(request_id=1, firmware=firmware)

    assert await wait_for_and_validate(
        test_utility,
        charge_point_v201,
        "UpdateFirmware",
        call_result201.UpdateFirmware(
            status=UpdateFirmwareStatusEnumType.accepted,
        ),
    )

    assert await wait_for_and_validate(
        test_utility,
        charge_point_v201,
        "FirmwareStatusNotification",
        {"status": "Downloading", "requestId": 1},
    )

    assert await wait_for_and_validate(
        test_utility,
        charge_point_v201,
        "FirmwareStatusNotification",
        {"status": "Downloaded", "requestId": 1},
    )

    assert await wait_for_and_validate(
        test_utility,
        charge_point_v201,
        "FirmwareStatusNotification",
        {"status": "SignatureVerified", "requestId": 1},
    )

    # Verify that the connectors are being drained before the firmware is installed
    assert await wait_for_and_validate(
        test_utility,
        charge_point_v201,
        "StatusNotification",
        call201.StatusNotification(
            datetime.now().isoformat(),
            ConnectorStatusEnumType.unavailable,
            1,
            1,
        ),
        validate_status_notification_201,
    )

    assert await wait_for_and_validate(
        test_utility,
        charge_point_v201,
        "FirmwareStatusNotification",
        {"status": "Installing", "requestId": 1},
    )

    assert await wait_for_and_validate(
        test_utility,
        charge_point_v201,
        "FirmwareStatusNotification",
        {"status": "Installed", "requestId": 1},
    )

    assert await wait_for_and_validate(
        test_utility,
        charge_point_v201,
        "FirmwareStatusNotification",
        {"status": "InstallRebooting", "requestId": 1},
    )


@pytest.mark.asyncio
@pytest.mark.ocpp_version("ocpp2.0.1")
@pytest.mark.everest_core_config("everest-config-ocpp201.yaml")
@pytest.mark.everest_config_adaptions(DisableResetAfterUpdateStrategy())
@pytest.mark.xdist_group(name="FTP")
async def test_L01_secure_firmware_update_keep_connectors_available(
    test_config: OcppTestConfiguration,
    charge_point_v201: ChargePoint201,
    test_controller: TestController,
    test_utility: TestUtility,
    ftp_server,
):
    """L01 - Secure Firmware Update: connectors are NOT made unavailable when the
    firmware metadata carries disable_connectors_during_install=false.
    """
    logging.info(
        "######### test_L01_secure_firmware_update_keep_connectors_available #########")

    os.system(
        f"curl -T {Path(__file__).parent.parent / test_config.firmware_info.update_file_keep_connectors_available} ftp://{getpass.getuser()}:12345@localhost:{ftp_server.port}"
    )

    location = f"ftp://{getpass.getuser()}:12345@localhost:{ftp_server.port}/firmware_update_keep_connectors_available.pnx"
    retrieve_date_time = datetime.now(timezone.utc)
    mf_root_ca = open(test_config.certificate_info.mf_root_ca).read()
    fw_signature = open(
        test_config.firmware_info.update_file_keep_connectors_available_signature).read()

    firmware = FirmwareType(
        location=location,
        retrieve_date_time=retrieve_date_time.isoformat(),
        signing_certificate=mf_root_ca,
        signature=fw_signature,
    )

    await charge_point_v201.update_firmware(request_id=1, firmware=firmware)

    assert await wait_for_and_validate(
        test_utility,
        charge_point_v201,
        "UpdateFirmware",
        call_result201.UpdateFirmware(
            status=UpdateFirmwareStatusEnumType.accepted),
    )

    assert await wait_for_and_validate(
        test_utility,
        charge_point_v201,
        "FirmwareStatusNotification",
        {"status": "Downloading", "requestId": 1},
    )

    assert await wait_for_and_validate(
        test_utility,
        charge_point_v201,
        "FirmwareStatusNotification",
        {"status": "Downloaded", "requestId": 1},
    )

    assert await wait_for_and_validate(
        test_utility,
        charge_point_v201,
        "FirmwareStatusNotification",
        {"status": "SignatureVerified", "requestId": 1},
    )

    # Verify that the connectors are not made unavailable.
    # Drop the message buffer so that no StatusNotification sent before this point
    # causes a test failure, then forbid any StatusNotification during install.
    test_utility.messages.clear()
    test_utility.forbidden_actions.append("StatusNotification")

    assert await wait_for_and_validate(
        test_utility,
        charge_point_v201,
        "FirmwareStatusNotification",
        {"status": "Installing", "requestId": 1},
    )

    test_utility.forbidden_actions.remove("StatusNotification")

    assert await wait_for_and_validate(
        test_utility,
        charge_point_v201,
        "FirmwareStatusNotification",
        {"status": "Installed", "requestId": 1},
    )


@pytest.mark.asyncio
@pytest.mark.ocpp_version("ocpp2.0.1")
@pytest.mark.everest_core_config("everest-config-ocpp201.yaml")
@pytest.mark.everest_config_adaptions(DisableResetAfterUpdateStrategy())
async def test_L01_update_firmware_cancels_running_download(
    test_config: OcppTestConfiguration,
    charge_point_v201: ChargePoint201,
    test_utility: TestUtility,
):
    """L01: a second UpdateFirmware cancels the running download, which then reports only DownloadFailed."""
    logging.info(
        "######### test_L01_update_firmware_cancels_running_download #########")

    firmware_bytes = Path(test_config.firmware_info.update_file).read_bytes()
    first_download_connected = asyncio.Event()
    release_first_download = asyncio.Event()
    connections = 0

    async def handle_download(reader: asyncio.StreamReader, writer: asyncio.StreamWriter):
        nonlocal connections
        connections += 1
        is_first = connections == 1
        await reader.read(65536)
        if is_first:
            first_download_connected.set()
            await release_first_download.wait()
        writer.write(
            f"HTTP/1.1 200 OK\r\nContent-Length: {len(firmware_bytes)}\r\nConnection: close\r\n\r\n".encode()
            + firmware_bytes)
        await writer.drain()
        writer.close()

    server = await asyncio.start_server(handle_download, "127.0.0.1", 0)
    port = server.sockets[0].getsockname()[1]
    mf_root_ca = open(test_config.certificate_info.mf_root_ca).read()
    fw_signature = open(test_config.firmware_info.update_file_signature).read()

    def firmware() -> FirmwareType:
        return FirmwareType(
            location=f"http://127.0.0.1:{port}/firmware_update.pnx",
            retrieve_date_time=datetime.now(timezone.utc).isoformat(),
            signing_certificate=mf_root_ca,
            signature=fw_signature,
        )

    try:
        r = await charge_point_v201.update_firmware(request_id=1, firmware=firmware())
        assert UpdateFirmwareStatusEnumType(r.status) == UpdateFirmwareStatusEnumType.accepted
        assert await wait_for_and_validate(
            test_utility, charge_point_v201, "FirmwareStatusNotification", {
                "status": "Downloading", "requestId": 1}
        )
        await asyncio.wait_for(first_download_connected.wait(), timeout=STATUS_TIMEOUT_S)

        history = charge_point_v201.message_history.messages
        cancel_index = len(history)
        r = await charge_point_v201.update_firmware(request_id=2, firmware=firmware())
        assert UpdateFirmwareStatusEnumType(r.status) == UpdateFirmwareStatusEnumType.accepted_canceled

        assert await wait_for_and_validate(
            test_utility, charge_point_v201, "FirmwareStatusNotification", {
                "status": "DownloadFailed", "requestId": 1},
            timeout=STATUS_TIMEOUT_S,
        )
        for status in ["Downloading", "Downloaded", "SignatureVerified"]:
            assert await wait_for_and_validate(
                test_utility, charge_point_v201, "FirmwareStatusNotification", {
                    "status": status, "requestId": 2},
                timeout=STATUS_TIMEOUT_S,
            )

        received = [unpack(m.message)
                    for m in history[cancel_index:] if m.initiator == "Chargepoint"]
        cancelled_request_statuses = [
            msg.payload["status"]
            for msg in received
            if msg.message_type_id == MessageType.Call
            and msg.action == "FirmwareStatusNotification"
            and msg.payload.get("requestId") == 1
        ]
        assert cancelled_request_statuses == ["DownloadFailed"]
    finally:
        release_first_download.set()
        server.close()
        await server.wait_closed()


@pytest.mark.asyncio
@pytest.mark.ocpp_version("ocpp2.0.1")
@pytest.mark.everest_core_config("everest-config-ocpp201.yaml")
@pytest.mark.everest_config_adaptions(DisableResetAfterUpdateStrategy())
async def test_L01_update_firmware_cancels_retry_wait(
    test_config: OcppTestConfiguration,
    charge_point_v201: ChargePoint201,
    test_utility: TestUtility,
):
    """L01: a second UpdateFirmware cancels a download waiting to retry, which reports nothing more."""
    logging.info(
        "######### test_L01_update_firmware_cancels_retry_wait #########")

    closed = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    closed.bind(("127.0.0.1", 0))
    closed_port = closed.getsockname()[1]
    closed.close()
    mf_root_ca = open(test_config.certificate_info.mf_root_ca).read()
    fw_signature = open(test_config.firmware_info.update_file_signature).read()

    def firmware() -> FirmwareType:
        return FirmwareType(
            location=f"http://127.0.0.1:{closed_port}/firmware_update.pnx",
            retrieve_date_time=datetime.now(timezone.utc).isoformat(),
            signing_certificate=mf_root_ca,
            signature=fw_signature,
        )

    r = await charge_point_v201.update_firmware(
        request_id=1, firmware=firmware(), retries=2, retry_interval=60)
    assert UpdateFirmwareStatusEnumType(r.status) == UpdateFirmwareStatusEnumType.accepted
    assert await wait_for_and_validate(
        test_utility, charge_point_v201, "FirmwareStatusNotification", {
            "status": "DownloadFailed", "requestId": 1},
        timeout=3 * STATUS_TIMEOUT_S,
    )

    history = charge_point_v201.message_history.messages
    cancel_index = len(history)
    r = await charge_point_v201.update_firmware(request_id=2, firmware=firmware(), retries=1)
    assert UpdateFirmwareStatusEnumType(r.status) == UpdateFirmwareStatusEnumType.accepted_canceled

    # Request 2 starts only once request 1 has stopped reporting.
    assert await wait_for_and_validate(
        test_utility, charge_point_v201, "FirmwareStatusNotification", {
            "status": "Downloading", "requestId": 2},
        timeout=STATUS_TIMEOUT_S,
    )

    received = [unpack(m.message)
                for m in history[cancel_index:] if m.initiator == "Chargepoint"]
    cancelled_request_statuses = [
        msg.payload["status"]
        for msg in received
        if msg.message_type_id == MessageType.Call
        and msg.action == "FirmwareStatusNotification"
        and msg.payload.get("requestId") == 1
    ]
    assert cancelled_request_statuses == []
