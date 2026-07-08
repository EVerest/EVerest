# SPDX-License-Identifier: Apache-2.0
# Copyright Pionix GmbH and Contributors to EVerest

import pytest

from everest.testing.core_utils._configuration.libocpp_configuration_helper import (
    GenericOCPP2XConfigAdjustment,
    OCPP2XConfigVariableIdentifier,
)
from everest.testing.core_utils.controller.test_controller_interface import TestController
from everest.testing.ocpp_utils.charge_point_utils import wait_for_and_validate, TestUtility
from everest.testing.ocpp_utils.charge_point_v201 import ChargePoint201
from everest.testing.ocpp_utils.fixtures import charge_point_v201
from everest.testing.core_utils._configuration.everest_configuration_strategies.yeti_simulator_disable_meter_transaction_start_strategy import \
    YetiSimulatorDisableMeterTransactionStartStrategy

from everest_test_utils import get_everest_config_path_str
from ocpp.v201 import call as call201


def validate_event(event, context, expected_meter_value):
    found = None
    for meter_value in event.meter_value or []:
        for sampled_value in meter_value['sampled_value']:
            if sampled_value.get('context') != context or sampled_value.get('signed_meter_value') is None:
                continue
            assert found is None, (
                f"multiple signed sampled values found for context {context}"
            )
            found = sampled_value
    assert found is not None, f"no signed sampled value found for context {context}"
    assert found['signed_meter_value']['signed_meter_data'] == expected_meter_value, (
        f"expected meter value {expected_meter_value}, got {found['signed_meter_value']['signed_meter_data']}"
    )


@pytest.mark.asyncio
@pytest.mark.ocpp_version("ocpp2.0.1")
@pytest.mark.everest_core_config(get_everest_config_path_str("everest-config-ocpp201.yaml"))
@pytest.mark.ocpp_config_adaptions(
    GenericOCPP2XConfigAdjustment(
        [
            (
                OCPP2XConfigVariableIdentifier(
                    "SampledDataCtrlr", "SampledDataSignReadings", "Actual"
                ),
                "true",
            ),
        ]
    )
)
async def test_meter_signed_meter_values(
    charge_point_v201: ChargePoint201,
    test_utility: TestUtility,
    test_controller: TestController,
):
    test_controller.plug_in(1)
    test_controller.swipe("DEADBEEF")

    started_event: call201.TransactionEvent = call201.TransactionEvent(
        **await wait_for_and_validate(  # pyright: ignore[reportCallIssue]
            test_utility,
            charge_point_v201,
            "TransactionEvent",
            {"eventType": "Started"},
        )
    )

    validate_event(started_event, "Transaction.Begin", "test start value")

    test_controller.plug_out()

    ended_event: call201.TransactionEvent = call201.TransactionEvent(
        **await wait_for_and_validate(  # pyright: ignore[reportCallIssue]
            test_utility,
            charge_point_v201,
            "TransactionEvent",
            {"eventType": "Ended"},
        )
    )

    validate_event(ended_event, "Transaction.End", "test stop value")


@pytest.mark.asyncio
@pytest.mark.ocpp_version("ocpp2.0.1")
@pytest.mark.everest_core_config(get_everest_config_path_str("everest-config-ocpp201.yaml"))
@pytest.mark.ocpp_config_adaptions(
    GenericOCPP2XConfigAdjustment(
        [
            (
                OCPP2XConfigVariableIdentifier(
                    "SampledDataCtrlr", "SampledDataSignReadings", "Actual"
                ),
                "true",
            ),
        ]
    )
)
@pytest.mark.everest_config_adaptions(YetiSimulatorDisableMeterTransactionStartStrategy())
async def test_meter_signed_meter_values_no_start(
    charge_point_v201: ChargePoint201,
    test_utility: TestUtility,
    test_controller: TestController,
):
    test_controller.plug_in(1)
    test_controller.swipe("DEADBEEF")

    started_event: call201.TransactionEvent = call201.TransactionEvent(
        **await wait_for_and_validate(  # pyright: ignore[reportCallIssue]
            test_utility,
            charge_point_v201,
            "TransactionEvent",
            {"eventType": "Started"},
        )
    )

    # The transaction start event should not contain signed meter values
    for meter_value in started_event.meter_value or []:
        for sampled_value in meter_value['sampled_value']:
            assert sampled_value.get('context') != "Transaction.Begin" or sampled_value.get('signed_meter_value') is None, \
                "Unexpected signed meter value in Transaction.Begin while the powermeter is configured to not send signed meter values on transaction start"

    test_controller.plug_out()

    ended_event: call201.TransactionEvent = call201.TransactionEvent(
        **await wait_for_and_validate(  # pyright: ignore[reportCallIssue]
            test_utility,
            charge_point_v201,
            "TransactionEvent",
            {"eventType": "Ended"},
        )
    )

    validate_event(ended_event, "Transaction.Begin", "test start value")
    validate_event(ended_event, "Transaction.End", "test stop value")
