# SPDX-License-Identifier: Apache-2.0
# Copyright Pionix GmbH and Contributors to EVerest

# OCPP 2.0.1 L02 - Non-Secure (unsigned) firmware update test.
#
# Staged for PR 3. The OCPP 2.0.1 path cannot currently reach the System
# module's unsigned firmware handler, so for now this asserts that an unsigned
# UpdateFirmware is Rejected. Once the routing is fixed (see notes.md "PR 3"),
# rewrite the test to drive a real unsigned update. See the docstring below.

from datetime import datetime, timezone
import logging
import getpass
import pytest
# fmt: off
import sys
import os
from pathlib import Path

from everest.testing.core_utils.controller.test_controller_interface import TestController

sys.path.append(os.path.abspath(
    os.path.join(os.path.dirname(__file__), "../..")))
from everest.testing.ocpp_utils.charge_point_utils import wait_for_and_validate, TestUtility, ValidationMode
# Import fixtures explicitly (not `import *`): the wildcard pulls in an empty
# `test_config` fixture from everest-testing that shadows the populated one in
# tests/ocpp_tests/conftest.py (which loads test_config.json). The OCPP 1.6
# firmware tests use the same explicit-import pattern.
from everest.testing.ocpp_utils.fixtures import charge_point_v201, central_system_v201, ftp_server
from everest.testing.ocpp_utils.charge_point_v201 import ChargePoint201
from ocpp.v201 import call as call201, call_result as call_result201
from ocpp.v201.enums import *
from ocpp.v201.datatypes import *
from validations import validate_status_notification_201
from everest_test_utils import *
# fmt: on


@pytest.mark.asyncio
@pytest.mark.ocpp_version("ocpp2.0.1")
@pytest.mark.everest_core_config("everest-config-ocpp201.yaml")
@pytest.mark.xdist_group(name="FTP")
async def test_L02_non_secure_firmware_update(
    test_config: OcppTestConfiguration,
    charge_point_v201: ChargePoint201,
    test_controller: TestController,
    test_utility: TestUtility,
    ftp_server,
):
    """L02 - Non-Secure (unsigned) Firmware Update.

    This currently asserts `Rejected`, which is the *current* behaviour of the
    OCPP 2.0.1 path, NOT a limitation of the System module. The System module
    DOES support unsigned updates via handle_standard_firmware_update(), but its
    dispatcher routes on `request_id == -1` (the OCPP 1.6 unsigned sentinel).
    OCPP201.cpp always forwards the mandatory OCPP 2.0.1 requestId (>= 0), so
    every v201 UpdateFirmware is routed to handle_signed_fimware_update(), which
    rejects it when no signing certificate is present.

    TODO (PR 3): once the OCPP201 routing is fixed (see notes.md "PR 3"), rewrite
    this to drive a real unsigned update and assert the standard
    Downloading/Downloaded/Installing/Installed sequence (mirror the OCPP 1.6
    `test_firwmare_update_retries` in
    tests/ocpp_tests/test_sets/ocpp16/firmware_and_diagnostics_tests.py).
    """
    logging.info(
        "######### test_L02_non_secure_firmware_update #########")

    location = f"ftp://{getpass.getuser()}:12345@localhost:{ftp_server.port}/firmware_update.pnx"
    retrieve_date_time = datetime.now(timezone.utc)

    firmware = FirmwareType(
        location=location,
        retrieve_date_time=retrieve_date_time.isoformat(),
    )

    await charge_point_v201.update_firmware(request_id=1, firmware=firmware)

    # Current behaviour: the request_id-based dispatcher sends this to the signed
    # handler, which rejects it for the missing signing certificate.
    assert await wait_for_and_validate(
        test_utility,
        charge_point_v201,
        "UpdateFirmware",
        call_result201.UpdateFirmware(
            status=UpdateFirmwareStatusEnumType.rejected),
    )
