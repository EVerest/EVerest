# SPDX-License-Identifier: Apache-2.0
# Copyright Pionix GmbH and Contributors to EVerest

import getpass
import logging
import os
import sys
from datetime import datetime, timezone
from pathlib import Path

import pytest
from everest.testing.core_utils.controller.test_controller_interface import (
    TestController,
)

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
from ocpp.v201 import call as call201
from ocpp.v201 import call_result as call_result201
from ocpp.v201.datatypes import *
from ocpp.v201.enums import ConnectorStatusEnumType, UpdateFirmwareStatusEnumType
from validations import validate_status_notification_201


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


@pytest.mark.asyncio
@pytest.mark.ocpp_version("ocpp2.0.1")
@pytest.mark.everest_core_config("everest-config-ocpp201.yaml")
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
