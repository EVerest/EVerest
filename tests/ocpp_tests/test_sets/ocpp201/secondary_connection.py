# SPDX-License-Identifier: Apache-2.0
# Copyright Pionix GmbH and Contributors to EVerest

"""Secondary OCPP connections.

A charging station may keep additional, deliberately limited OCPP connections alongside its primary
CSMS connection, as described in the OCA application note "Secondary OCPP Connection on a Charging
Station". These tests cover the connection itself: that it comes up, that it is kept apart from the
primary connection, and above all that it can never disturb the primary connection.
"""

import asyncio
import json
import logging

import pytest

# fmt: off
from everest.testing.core_utils.controller.test_controller_interface import TestController
from everest.testing.ocpp_utils.central_system import CentralSystem
from everest.testing.ocpp_utils.charge_point_utils import TestUtility, wait_for_and_validate
from everest.testing.ocpp_utils.fixtures import *
from everest_test_utils import *  # noqa: F401,F403  overrides the v201 Action enum, must precede the import below
from ocpp.v201.enums import Action
# fmt: on

log = logging.getLogger("secondaryConnectionTest")

_ID_TOKEN = "8BADF00D"


@pytest.mark.asyncio
@pytest.mark.ocpp_version("ocpp2.0.1")
@pytest.mark.ocpp_multi_only
@pytest.mark.everest_core_config("everest-config-ocpp201.yaml")
@pytest.mark.secondary_csms("EMS")
async def test_secondary_connection_connects(
    central_system: CentralSystem,
    secondary_central_systems,
    test_controller: TestController,
    test_utility: TestUtility,
):
    """Both OCPP connections come up, each to its own endpoint and with its own identity."""

    test_controller.start()

    primary = await central_system.wait_for_chargepoint()
    secondary = await secondary_central_systems["EMS"].wait_for_chargepoint(
        wait_for_bootnotification=False
    )

    assert primary is not None
    assert secondary is not None
    # Distinct endpoints, and distinct identities towards them.
    assert central_system.port != secondary_central_systems["EMS"].port
    assert primary.id != secondary.id

    # The secondary announces itself so the far end knows who it is, even though its
    # BootNotificationResponse is ignored by the charging station.
    assert await wait_for_and_validate(
        test_utility, secondary, "BootNotification", {}, validate_boot_notification_shape
    )


@pytest.mark.asyncio
@pytest.mark.ocpp_version("ocpp2.0.1")
@pytest.mark.ocpp_multi_only
@pytest.mark.everest_core_config("everest-config-ocpp201.yaml")
@pytest.mark.secondary_csms("EMS")
async def test_secondary_connection_is_sent_no_station_business(
    central_system: CentralSystem,
    secondary_central_systems,
    test_controller: TestController,
    test_utility: TestUtility,
):
    """A secondary connection with no granted upstream messages receives none.

    Application note section 3.6: a secondary system gets nothing it has not been granted. Only the
    messages that keep its own link alive reach it.
    """

    test_controller.start()

    await central_system.wait_for_chargepoint()
    secondary = await secondary_central_systems["EMS"].wait_for_chargepoint(
        wait_for_bootnotification=False
    )

    test_controller.plug_in()
    await asyncio.sleep(3)

    # The pipeline holds the raw frames the secondary CSMS received. Only CALLs matter here: a
    # response is never withheld, but a request is only sent if the connection was granted it.
    actions = []
    for raw in secondary.pipeline:
        frame = json.loads(raw)
        if frame[0] == 2:  # CALL
            actions.append(frame[2])

    allowed = {"BootNotification", "Heartbeat"}
    unexpected = [action for action in actions if action not in allowed]
    assert not unexpected, f"secondary connection was sent {unexpected}, which it was never granted"


@pytest.mark.asyncio
@pytest.mark.ocpp_version("ocpp2.0.1")
@pytest.mark.ocpp_multi_only
@pytest.mark.everest_core_config("everest-config-ocpp201.yaml")
@pytest.mark.secondary_csms("EMS")
async def test_unreachable_secondary_does_not_disturb_the_primary(
    central_system: CentralSystem,
    secondary_central_systems,
    test_controller: TestController,
    test_utility: TestUtility,
):
    """Closing the secondary CSMS leaves the primary connection completely unaffected.

    The single most important property of the whole feature: a secondary connection's failures,
    retries and timeouts must never reach the primary connection.
    """

    test_controller.start()

    primary = await central_system.wait_for_chargepoint()
    await secondary_central_systems["EMS"].wait_for_chargepoint(
        wait_for_bootnotification=False
    )

    # Take the secondary CSMS away mid-run.
    secondary_central_systems["EMS"].ws_server.close()
    await secondary_central_systems["EMS"].ws_server.wait_closed()

    # The primary connection carries on: a transaction runs to completion on it.
    test_controller.plug_in()
    test_controller.swipe(_ID_TOKEN)
    assert await wait_for_and_validate(
        test_utility, primary, "TransactionEvent", {"eventType": "Started"}
    )
    test_controller.plug_out()
    assert await wait_for_and_validate(
        test_utility, primary, "TransactionEvent", {"eventType": "Ended"}
    )


@pytest.mark.asyncio
@pytest.mark.ocpp_version("ocpp2.0.1")
@pytest.mark.ocpp_multi_only
@pytest.mark.everest_core_config("everest-config-ocpp201.yaml")
@pytest.mark.secondary_csms("EMS", central_system_port=1)
async def test_secondary_that_never_connects_does_not_delay_the_primary(
    central_system: CentralSystem,
    test_controller: TestController,
    test_utility: TestUtility,
):
    """A secondary connection pointed at a dead port never holds up the primary connection.

    Declared without a CentralSystem, so nothing is listening on its slot. The primary must still
    boot and run normally, which is what proves the connections dial independently rather than in
    sequence.
    """

    test_controller.start()

    primary = await central_system.wait_for_chargepoint()
    assert primary is not None

    test_controller.plug_in()
    test_controller.swipe(_ID_TOKEN)
    assert await wait_for_and_validate(
        test_utility, primary, "TransactionEvent", {"eventType": "Started"}
    )


def validate_boot_notification_shape(_meta_data, msg, _exp_payload):
    """A BootNotification is a BootNotification whatever it is sent to."""
    return "chargingStation" in msg.payload and "reason" in msg.payload
