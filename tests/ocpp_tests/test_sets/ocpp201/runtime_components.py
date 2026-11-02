# SPDX-License-Identifier: Apache-2.0
# Copyright Pionix GmbH and Contributors to EVerest

"""Integration tests for the components that EVerest registers with the device model at runtime."""

import asyncio
import logging

import pytest

# fmt: off
from everest.testing.core_utils.controller.test_controller_interface import TestController
from everest.testing.ocpp_utils.fixtures import *
from everest_test_utils import *  # Needs to be before the v201 enums below since it overrides the Action enum
from everest.testing.core_utils._configuration.libocpp_configuration_helper import GenericOCPP2XConfigAdjustment, OCPP2XConfigVariableIdentifier
from everest.testing.ocpp_utils.charge_point_utils import wait_for_and_validate, TestUtility
from everest.testing.ocpp_utils.charge_point_v201 import ChargePoint201
from everest.testing.ocpp_utils.central_system import CentralSystem
from ocpp.v201.datatypes import ComponentType, EVSEType, SetMonitoringDataType, VariableType
from ocpp.v201.enums import MonitorEnumType, SetMonitoringStatusEnumType
# fmt: on

log = logging.getLogger("runtimeComponentsTest")

POWER_THRESHOLD_W = 100


def _validate_power_monitor_event(monitor_id):
    def _validate(meta_data, msg, exp_payload):
        if msg.action != "NotifyEvent":
            return False
        for event_data in msg.payload.get("eventData", []):
            component = event_data.get("component", {})
            if (
                event_data.get("variableMonitoringId") == monitor_id
                and component.get("name") == "EVSE"
                and component.get("evse", {}).get("id") == 1
                and event_data.get("variable", {}).get("name") == "Power"
            ):
                return True
        return False

    return _validate


ID_TOKEN = "8BADF00D"


async def _charge(test_controller: TestController):
    test_controller.swipe(ID_TOKEN)
    test_controller.plug_in()


@pytest.mark.asyncio
@pytest.mark.ocpp_version("ocpp2.0.1")
@pytest.mark.everest_core_config(get_everest_config_path_str("everest-config-ocpp201.yaml"))
@pytest.mark.ocpp_config_adaptions(
    GenericOCPP2XConfigAdjustment(
        [
            (OCPP2XConfigVariableIdentifier("MonitoringCtrlr", "MonitoringCtrlrEnabled", "Actual"), "true"),
        ]
    )
)
async def test_monitor_on_evse_power_survives_a_restart(
    charge_point_v201: ChargePoint201,
    central_system_v201: CentralSystem,
    test_controller: TestController,
    test_utility: TestUtility,
):
    """EVSE(1).Power is registered by EVerest, so the CSMS can monitor it: the monitor fires on the power that
    EVerest reports while charging, and it is still there after a restart."""
    response = await charge_point_v201.set_variable_monitoring_req(
        set_monitoring_data=[
            SetMonitoringDataType(
                value=POWER_THRESHOLD_W,
                type=MonitorEnumType.upper_threshold,
                severity=5,
                component=ComponentType(name="EVSE", evse=EVSEType(id=1)),
                variable=VariableType(name="Power"),
            )
        ]
    )
    results = response.set_monitoring_result
    assert len(results) == 1
    assert results[0]["status"] == SetMonitoringStatusEnumType.accepted
    monitor_id = results[0]["id"]

    await _charge(test_controller)
    assert await wait_for_and_validate(
        test_utility, charge_point_v201, "NotifyEvent", {}, _validate_power_monitor_event(monitor_id), timeout=60
    )

    test_controller.plug_out()
    test_controller.stop()
    await asyncio.sleep(2)
    test_utility.messages.clear()
    test_controller.start()
    charge_point_v201 = await central_system_v201.wait_for_chargepoint(wait_for_bootnotification=True)

    await _charge(test_controller)
    assert await wait_for_and_validate(
        test_utility, charge_point_v201, "NotifyEvent", {}, _validate_power_monitor_event(monitor_id), timeout=60
    )
