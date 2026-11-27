# SPDX-License-Identifier: Apache-2.0
# Copyright Pionix GmbH and Contributors to EVerest

import asyncio
import logging

import pytest

# fmt: off
from everest.testing.core_utils.controller.test_controller_interface import TestController
from ocpp.v21.datatypes import *
from ocpp.v21.enums import *
from everest.testing.ocpp_utils.fixtures import *
from everest_test_utils import *  # Needs to be before the enums below since it overrides the v21 Action enum
from ocpp.v21.enums import ResetEnumType, ResetStatusEnumType
from everest.testing.core_utils._configuration.libocpp_configuration_helper import GenericOCPP2XConfigAdjustment
from everest.testing.ocpp_utils.charge_point_utils import wait_for_and_validate, TestUtility
# fmt: on


def transaction_events(charge_point, event_type):
    return [message.payload for message in received_calls(charge_point, "TransactionEvent")
            if message.payload["eventType"] == event_type]


def ocpp21_config(resumption_timeout: str, allow_energy_transfer_resumption: str):
    return GenericOCPP2XConfigAdjustment(
        [
            (OCPP2XConfigVariableIdentifier("InternalCtrlr", "SupportedOcppVersions", "Actual"), "ocpp2.1"),
            (OCPP2XConfigVariableIdentifier("TxCtrlr", "ResumptionTimeout", "Actual"), resumption_timeout),
            (OCPP2XConfigVariableIdentifier("TxCtrlr", "AllowEnergyTransferResumption", "Actual"),
             allow_energy_transfer_resumption),
        ]
    )


async def start_transaction(central_system: CentralSystem, test_controller: TestController,
                            test_utility: TestUtility):
    test_controller.start()
    charge_point = await central_system.wait_for_chargepoint(wait_for_bootnotification=True)

    await charge_point.request_start_transaction_req(
        id_token=IdTokenType(id_token="DEADBEEF", type="ISO14443"), remote_start_id=1, evse_id=1
    )
    test_controller.plug_in()

    assert await wait_for_and_validate(test_utility, charge_point, "TransactionEvent", {"eventType": "Started"})
    assert await wait_for_and_validate(
        test_utility, charge_point, "TransactionEvent",
        {"eventType": "Updated", "transactionInfo": {"chargingState": "Charging"}},
    )
    started = transaction_events(charge_point, "Started")
    assert len(started) == 1
    return charge_point, started[0]["transactionInfo"]["transactionId"]


@pytest.mark.asyncio
@pytest.mark.ocpp_version("ocpp2.1")
@pytest.mark.everest_core_config("everest-config-ocpp201-ps.yaml")
@pytest.mark.use_temporary_persistent_store
@pytest.mark.everest_config_adaptions(EvseManagerResumeTransactionStrategy("Never"))
@pytest.mark.ocpp_config_adaptions(ocpp21_config("60", "true"))
async def test_reset_immediate_and_resume_with_transaction(
    central_system_v21: CentralSystem,
    test_controller: TestController,
    test_utility: TestUtility,
):
    """B13.FR.03, B13.FR.04, B13.FR.23, B13.FR.24, B13.FR.30"""
    charge_point_v21, transaction_id = await start_transaction(central_system_v21, test_controller, test_utility)

    response = await charge_point_v21.reset_req(type=ResetEnumType.immediate_and_resume)
    assert response.status == ResetStatusEnumType.accepted
    assert await wait_for_and_validate(
        test_utility, charge_point_v21, "TransactionEvent",
        {"eventType": "Updated", "triggerReason": "ResetCommand", "transactionInfo": {"transactionId": transaction_id}},
    )
    await asyncio.sleep(3)
    assert not transaction_events(charge_point_v21, "Ended")

    test_controller.stop()
    await asyncio.sleep(2)
    test_utility.messages.clear()
    test_controller.start()
    charge_point_v21 = await central_system_v21.wait_for_chargepoint(wait_for_bootnotification=False)

    # the EV is still there after the reset
    test_controller.plug_in()
    assert await wait_for_and_validate(
        test_utility, charge_point_v21, "TransactionEvent",
        {"eventType": "Updated", "triggerReason": "TxResumed", "transactionInfo": {"transactionId": transaction_id}},
    )
    assert await wait_for_and_validate(
        test_utility, charge_point_v21, "TransactionEvent",
        {"eventType": "Updated", "transactionInfo": {"transactionId": transaction_id, "chargingState": "Charging"}},
    )
    assert not transaction_events(charge_point_v21, "Started")
    assert not transaction_events(charge_point_v21, "Ended")

    test_controller.plug_out()
    assert await wait_for_and_validate(
        test_utility, charge_point_v21, "TransactionEvent",
        {"eventType": "Ended", "transactionInfo": {"transactionId": transaction_id, "stoppedReason": "EVDisconnected"}},
    )
    assert len(transaction_events(charge_point_v21, "Ended")) == 1


@pytest.mark.asyncio
@pytest.mark.ocpp_version("ocpp2.1")
@pytest.mark.everest_core_config("everest-config-ocpp201-ps.yaml")
@pytest.mark.use_temporary_persistent_store
@pytest.mark.everest_config_adaptions(EvseManagerResumeTransactionStrategy("Never"))
@pytest.mark.ocpp_config_adaptions(ocpp21_config("60", "false"))
async def test_reset_immediate_and_resume_without_energy_transfer_resumption(
    central_system_v21: CentralSystem,
    test_controller: TestController,
    test_utility: TestUtility,
):
    """B13.FR.25: the resumed transaction does not transfer energy"""
    charge_point_v21, transaction_id = await start_transaction(central_system_v21, test_controller, test_utility)

    response = await charge_point_v21.reset_req(type=ResetEnumType.immediate_and_resume)
    assert response.status == ResetStatusEnumType.accepted
    await asyncio.sleep(3)

    test_controller.stop()
    await asyncio.sleep(2)
    test_utility.messages.clear()
    test_controller.start()
    charge_point_v21 = await central_system_v21.wait_for_chargepoint(wait_for_bootnotification=False)

    test_controller.plug_in()
    assert await wait_for_and_validate(
        test_utility, charge_point_v21, "TransactionEvent",
        {"eventType": "Updated", "triggerReason": "TxResumed", "transactionInfo": {"transactionId": transaction_id}},
    )
    assert await wait_for_and_validate(
        test_utility, charge_point_v21, "TransactionEvent",
        {"eventType": "Updated",
         "transactionInfo": {"transactionId": transaction_id, "chargingState": "SuspendedEVSE"}},
    )
    charging = [event for event in transaction_events(charge_point_v21, "Updated")
                if event["transactionInfo"].get("chargingState") == "Charging"]
    assert not charging


@pytest.mark.asyncio
@pytest.mark.ocpp_version("ocpp2.1")
@pytest.mark.everest_core_config("everest-config-ocpp201-ps.yaml")
@pytest.mark.use_temporary_persistent_store
@pytest.mark.ocpp_config_adaptions(ocpp21_config("0", "true"))
async def test_reset_immediate_and_resume_rejected_without_resumption_timeout(
    central_system_v21: CentralSystem,
    test_controller: TestController,
    test_utility: TestUtility,
):
    """B13.FR.01"""
    test_controller.start()
    charge_point_v21 = await central_system_v21.wait_for_chargepoint(wait_for_bootnotification=True)

    response = await charge_point_v21.reset_req(type=ResetEnumType.immediate_and_resume)
    assert response.status == ResetStatusEnumType.rejected
