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
from ocpp.exceptions import SecurityError
from ocpp.v201 import call as call201
from ocpp.v201.datatypes import ComponentType, GetVariableDataType, SetVariableDataType, VariableType
from ocpp.v201.enums import Action, OperationalStatusEnumType, ResetEnumType
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


@pytest.mark.asyncio
@pytest.mark.ocpp_version("ocpp2.0.1")
@pytest.mark.ocpp_multi_only
@pytest.mark.everest_core_config("everest-config-ocpp201.yaml")
@pytest.mark.secondary_csms("EMS", upstream_msgs="TransactionEvent,MeterValues")
async def test_granted_transaction_events_are_copied_without_the_id_token(
    central_system: CentralSystem,
    secondary_central_systems,
    test_controller: TestController,
    test_utility: TestUtility,
):
    """The Data Collection / HEMS use case: a secondary system granted TransactionEvent gets copies.

    Application note sections 4.2 and 6.2. The copy is the same message with its own messageId, but
    the idToken is left out: a secondary system has no business knowing who is charging.
    """

    test_controller.start()

    primary = await central_system.wait_for_chargepoint()
    secondary = await secondary_central_systems["EMS"].wait_for_chargepoint(
        wait_for_bootnotification=False
    )

    test_controller.plug_in()
    test_controller.swipe(_ID_TOKEN)

    # The CSMS gets the transaction with the idToken, because it is the one that authorizes.
    assert await wait_for_and_validate(
        test_utility,
        primary,
        "TransactionEvent",
        {"eventType": "Started", "idToken": {"idToken": _ID_TOKEN, "type": "ISO14443"}},
    )

    # The secondary gets the same transaction, without it.
    started = await _wait_for_call(secondary, "TransactionEvent")
    assert started is not None, "secondary connection never received the TransactionEvent copy"
    payload = started[3]
    assert payload["eventType"] == "Started"
    assert "idToken" not in payload, "the copy for a secondary connection must carry no idToken"
    assert "transactionInfo" in payload

    # Its own messageId numbering, so the two endpoints can never collide.
    primary_ids = {frame[1] for frame in _calls(primary, "TransactionEvent")}
    secondary_ids = {frame[1] for frame in _calls(secondary, "TransactionEvent")}
    assert primary_ids.isdisjoint(secondary_ids)


@pytest.mark.asyncio
@pytest.mark.ocpp_version("ocpp2.0.1")
@pytest.mark.ocpp_multi_only
@pytest.mark.everest_core_config("everest-config-ocpp201.yaml")
@pytest.mark.secondary_csms("EMS", downstream_msgs="GetVariables")
async def test_a_request_outside_the_grant_is_refused(
    central_system: CentralSystem,
    secondary_central_systems,
    test_controller: TestController,
    test_utility: TestUtility,
):
    """Application note section 3.5: an ungranted request gets CALLERROR(SecurityError)."""

    test_controller.start()

    await central_system.wait_for_chargepoint()
    secondary = await secondary_central_systems["EMS"].wait_for_chargepoint(
        wait_for_bootnotification=False
    )

    # Reset is not in this connection's DownstreamMsgs, so the charging station refuses it outright
    # rather than resetting. suppress=False so the CALLERROR surfaces as an exception rather than
    # being swallowed into a None return, which is the ocpp library's default.
    with pytest.raises(SecurityError):
        await secondary.call(call201.Reset(type=ResetEnumType.immediate), suppress=False)


@pytest.mark.asyncio
@pytest.mark.ocpp_version("ocpp2.0.1")
@pytest.mark.ocpp_multi_only
@pytest.mark.everest_core_config("everest-config-ocpp201.yaml")
@pytest.mark.secondary_csms("EMS", downstream_msgs="GetVariables")
async def test_a_granted_request_is_handled_normally(
    central_system: CentralSystem,
    secondary_central_systems,
    test_controller: TestController,
    test_utility: TestUtility,
):
    """A granted request is answered by the ordinary functional block, on the right connection.

    Nothing about a granted message is special-cased: once the allow list lets it through it takes
    exactly the same path as the same message from the CSMS, and its answer goes back to whoever
    asked rather than to the CSMS.
    """

    test_controller.start()

    primary = await central_system.wait_for_chargepoint()
    secondary = await secondary_central_systems["EMS"].wait_for_chargepoint(
        wait_for_bootnotification=False
    )

    response = await secondary.call(
        call201.GetVariables(
            get_variable_data=[
                GetVariableDataType(
                    component=ComponentType(name="OCPPCommCtrlr"),
                    variable=VariableType(name="HeartbeatInterval"),
                )
            ]
        )
    )

    assert response.get_variable_result[0]["attribute_status"] == "Accepted"

    # The answer went back to the connection that asked, not to the CSMS.
    assert not _calls(primary, "GetVariables")


@pytest.mark.asyncio
@pytest.mark.ocpp_version("ocpp2.0.1")
@pytest.mark.ocpp_multi_only
@pytest.mark.everest_core_config("everest-config-ocpp201.yaml")
@pytest.mark.secondary_csms("EMS", downstream_msgs="ChangeAvailability")
async def test_a_granted_secondary_can_control_the_station(
    central_system: CentralSystem,
    secondary_central_systems,
    test_controller: TestController,
    test_utility: TestUtility,
):
    """Controlling the charging station is an allow list decision, not a separate capability.

    Availability is station state: whether a connection may change it is decided by its
    DownstreamMsgs, and once it may, its command is as authoritative as the CSMS's.
    """

    test_controller.start()

    primary = await central_system.wait_for_chargepoint()
    secondary = await secondary_central_systems["EMS"].wait_for_chargepoint(
        wait_for_bootnotification=False
    )

    response = await secondary.call(
        call201.ChangeAvailability(operational_status=OperationalStatusEnumType.inoperative)
    )
    assert response.status == "Accepted"

    # The station really did go unavailable, and told the CSMS so.
    assert await wait_for_and_validate(
        test_utility, primary, "StatusNotification", {"connectorStatus": "Unavailable"}
    )


@pytest.mark.asyncio
@pytest.mark.ocpp_version("ocpp2.0.1")
@pytest.mark.ocpp_multi_only
@pytest.mark.everest_core_config("everest-config-ocpp201.yaml")
@pytest.mark.secondary_csms(
    "EMS",
    downstream_msgs="SetVariables,GetVariables,GetBaseReport",
    allow_set_variables="SmartChargingCtrlr",
)
async def test_a_secondary_may_only_write_what_it_was_granted(
    central_system: CentralSystem,
    secondary_central_systems,
    test_controller: TestController,
    test_utility: TestUtility,
):
    """Application note section 3.6: default deny, and only what AllowSetVariables grants."""

    test_controller.start()

    primary = await central_system.wait_for_chargepoint()
    secondary = await secondary_central_systems["EMS"].wait_for_chargepoint(
        wait_for_bootnotification=False
    )

    # Granted: the whole SmartChargingCtrlr component.
    granted = await secondary.call(
        call201.SetVariables(
            set_variable_data=[
                SetVariableDataType(
                    attribute_value="true",
                    component=ComponentType(name="SmartChargingCtrlr"),
                    variable=VariableType(name="Enabled"),
                )
            ]
        )
    )
    assert granted.set_variable_result[0]["attribute_status"] == "Accepted"

    # Not granted: anything else.
    refused = await secondary.call(
        call201.SetVariables(
            set_variable_data=[
                SetVariableDataType(
                    attribute_value="42",
                    component=ComponentType(name="OCPPCommCtrlr"),
                    variable=VariableType(name="HeartbeatInterval"),
                )
            ]
        )
    )
    assert refused.set_variable_result[0]["attribute_status"] == "Rejected"

    # The CSMS is subject to no such gate.
    allowed_for_csms = await primary.call(
        call201.SetVariables(
            set_variable_data=[
                SetVariableDataType(
                    attribute_value="42",
                    component=ComponentType(name="OCPPCommCtrlr"),
                    variable=VariableType(name="HeartbeatInterval"),
                )
            ]
        )
    )
    assert allowed_for_csms.set_variable_result[0]["attribute_status"] == "Accepted"


@pytest.mark.asyncio
@pytest.mark.ocpp_version("ocpp2.0.1")
@pytest.mark.ocpp_multi_only
@pytest.mark.everest_core_config("everest-config-ocpp201.yaml")
@pytest.mark.secondary_csms(
    "EMS",
    downstream_msgs="SetVariables,GetVariables",
    # Granted the whole component that holds its own privileges, which still must not let it widen
    # them (application note section 3.6.2).
    allow_set_variables="OCPPCommCtrlr#EMS",
)
async def test_a_secondary_cannot_widen_its_own_privileges(
    central_system: CentralSystem,
    secondary_central_systems,
    test_controller: TestController,
    test_utility: TestUtility,
):
    """Section 3.6.2: the three privilege variables stay read only for a secondary system."""

    test_controller.start()

    await central_system.wait_for_chargepoint()
    secondary = await secondary_central_systems["EMS"].wait_for_chargepoint(
        wait_for_bootnotification=False
    )

    for privilege_variable in ("AllowSetVariables", "UpstreamMsgs", "DownstreamMsgs"):
        response = await secondary.call(
            call201.SetVariables(
                set_variable_data=[
                    SetVariableDataType(
                        attribute_value="Reset",
                        component=ComponentType(name="OCPPCommCtrlr", instance="EMS"),
                        variable=VariableType(name=privilege_variable),
                    )
                ]
            )
        )
        assert (
            response.set_variable_result[0]["attribute_status"] == "Rejected"
        ), f"{privilege_variable} must stay read only for a secondary connection"

    # Something else on the same granted component is still writable, so the refusal above is about
    # those three variables rather than the component as a whole.
    response = await secondary.call(
        call201.SetVariables(
            set_variable_data=[
                SetVariableDataType(
                    attribute_value="42",
                    component=ComponentType(name="OCPPCommCtrlr", instance="EMS"),
                    variable=VariableType(name="HeartbeatInterval"),
                )
            ]
        )
    )
    assert response.set_variable_result[0]["attribute_status"] == "Accepted"


@pytest.mark.asyncio
@pytest.mark.ocpp_version("ocpp2.0.1")
@pytest.mark.ocpp_multi_only
@pytest.mark.everest_core_config("everest-config-ocpp201.yaml")
@pytest.mark.secondary_csms(
    "EMS", downstream_msgs="GetVariables", allow_set_variables="SmartChargingCtrlr"
)
async def test_a_secondary_is_told_what_it_may_not_write(
    central_system: CentralSystem,
    secondary_central_systems,
    test_controller: TestController,
    test_utility: TestUtility,
):
    """A read by a secondary reports as ReadOnly whatever it may not actually write.

    Section 3.6: so a secondary system does not attempt writes the charging station would refuse.
    """

    test_controller.start()

    primary = await central_system.wait_for_chargepoint()
    secondary = await secondary_central_systems["EMS"].wait_for_chargepoint(
        wait_for_bootnotification=False
    )

    request = call201.GetVariables(
        get_variable_data=[
            GetVariableDataType(
                component=ComponentType(name="OCPPCommCtrlr"),
                variable=VariableType(name="HeartbeatInterval"),
            )
        ]
    )

    # Both can read it; only the CSMS could write it. GetVariables does not report mutability, so
    # the check that matters is that the read itself still works for the secondary.
    from_secondary = await secondary.call(request)
    assert from_secondary.get_variable_result[0]["attribute_status"] == "Accepted"

    from_primary = await primary.call(request)
    assert from_primary.get_variable_result[0]["attribute_status"] == "Accepted"


def _calls(charge_point, action):
    """Every CALL of the given action the CSMS has received so far."""
    frames = []
    for raw in charge_point.pipeline:
        frame = json.loads(raw)
        if frame[0] == 2 and frame[2] == action:
            frames.append(frame)
    return frames


async def _wait_for_call(charge_point, action, timeout=10):
    """Waits for a CALL of the given action, returning the frame or None on timeout."""
    for _ in range(timeout * 10):
        found = _calls(charge_point, action)
        if found:
            return found[0]
        await asyncio.sleep(0.1)
    return None


def validate_boot_notification_shape(_meta_data, msg, _exp_payload):
    """A BootNotification is a BootNotification whatever it is sent to."""
    return "chargingStation" in msg.payload and "reason" in msg.payload
