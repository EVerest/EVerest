# SPDX-License-Identifier: Apache-2.0
# Copyright Pionix GmbH and Contributors to EVerest

"""Integration tests for the external owners that modules register with the device model through the
ocpp_device_model interface. The probe module provides the interface in place of the ocpp_device_model_API module."""

import asyncio
import logging
from copy import deepcopy
from typing import Dict
from unittest.mock import Mock

import pytest

# fmt: off
from everest.testing.core_utils import EverestConfigAdjustmentStrategy
from everest.testing.core_utils._configuration.libocpp_configuration_helper import GenericOCPP2XConfigAdjustment, OCPP2XConfigVariableIdentifier
from everest.testing.ocpp_utils.central_system import CentralSystem
from everest.testing.ocpp_utils.charge_point_utils import wait_for_and_validate, TestUtility
from everest.testing.ocpp_utils.fixtures import *
from everest_test_utils import *
from ocpp.v201.datatypes import ComponentType, GetVariableDataType, SetMonitoringDataType, SetVariableDataType, VariableType
from ocpp.v201.enums import AttributeEnumType, GetVariableStatusEnumType, MonitorEnumType, SetMonitoringStatusEnumType, SetVariableStatusEnumType
# fmt: on

log = logging.getLogger("externalOwnersTest")

IMPLEMENTATION_ID = "ProbeModuleDeviceModel"
DEFAULT_OWNER = "probe"


class DeviceModelConnectionAdjustment(EverestConfigAdjustmentStrategy):
    def adjust_everest_configuration(self, everest_config: Dict):
        adjusted_config = deepcopy(everest_config)
        adjusted_config["active_modules"]["ocpp"]["connections"]["device_model"] = [
            {"module_id": "probe", "implementation_id": IMPLEMENTATION_ID}
        ]
        return adjusted_config


def fan_speed_registration(value="40", owner=None, component="CoolingCtrlr"):
    registration = {
        "components": [
            {
                "component": {"name": component},
                "variables": [
                    {
                        "variable": {"name": "FanSpeed"},
                        "characteristics": {
                            "data_type": "integer",
                            "supports_monitoring": True,
                            "unit": "Percent",
                            "min_limit": 0,
                            "max_limit": 100,
                        },
                        "attributes": [{"type": "Actual", "value": value, "mutability": "ReadWrite"}],
                    }
                ],
            }
        ]
    }
    if owner is not None:
        registration["owner"] = owner
    return registration


class Owner:
    """Implements the ocpp_device_model commands at the probe module"""

    def __init__(self, probe_module, registrations):
        self.registration_results = Mock()
        self.writes = Mock()
        self.reply = "Accepted"
        probe_module.implement_command(IMPLEMENTATION_ID, "get_registrations", lambda _: {"registrations": registrations})
        probe_module.implement_command(IMPLEMENTATION_ID, "set_registration_result", self._registration_result)
        probe_module.implement_command(IMPLEMENTATION_ID, "set_value", self._set_value)

    def _registration_result(self, args):
        self.registration_results(args["result"])

    def _set_value(self, args):
        self.writes(args["request"])
        return self.reply


async def start(probe_module, central_system: CentralSystem):
    probe_module.start()
    await probe_module.wait_to_be_ready()
    probe_module.publish_variable("ProbeModuleConnectorA", "ready", True)
    probe_module.publish_variable("ProbeModuleConnectorB", "ready", True)
    return await central_system.wait_for_chargepoint()


async def wait_for_calls(mock: Mock, count: int, timeout=10):
    async def _wait():
        while mock.call_count < count:
            await asyncio.sleep(0.1)

    await asyncio.wait_for(_wait(), timeout)


async def read(charge_point, component="CoolingCtrlr", variable="FanSpeed"):
    response = await charge_point.get_variables_req(
        get_variable_data=[
            GetVariableDataType(
                component=ComponentType(name=component),
                variable=VariableType(name=variable),
                attribute_type=AttributeEnumType.actual,
            )
        ]
    )
    return response.get_variable_result[0]


async def write(charge_point, value, component="CoolingCtrlr", variable="FanSpeed"):
    response = await charge_point.set_variables_req(
        set_variable_data=[
            SetVariableDataType(
                attribute_value=value,
                attribute_type=AttributeEnumType.actual,
                component=ComponentType(name=component),
                variable=VariableType(name=variable),
            )
        ]
    )
    return response.set_variable_result[0]["attribute_status"]


@pytest.mark.asyncio
@pytest.mark.ocpp_version("ocpp2.0.1")
@pytest.mark.everest_core_config("everest-config-ocpp201-probe-module.yaml")
@pytest.mark.probe_module
@pytest.mark.everest_config_adaptions(DeviceModelConnectionAdjustment())
async def test_owner_registered_at_startup_is_read_and_written(probe_module, central_system: CentralSystem):
    owner = Owner(probe_module, [fan_speed_registration()])
    charge_point = await start(probe_module, central_system)

    await wait_for_calls(owner.registration_results, 1)
    result = owner.registration_results.call_args.args[0]
    assert result == {"owner": DEFAULT_OWNER, "status": "Accepted", "rejected": []}

    read_result = await read(charge_point)
    assert read_result["attribute_status"] == GetVariableStatusEnumType.accepted
    assert read_result["attribute_value"] == "40"

    assert await write(charge_point, "60") == SetVariableStatusEnumType.accepted
    request = owner.writes.call_args.args[0]
    assert request["owner"] == DEFAULT_OWNER
    assert request["component"] == {"name": "CoolingCtrlr"}
    assert request["variable"] == {"name": "FanSpeed"}
    assert request["attribute_type"] == "Actual"
    assert request["value"] == "60"
    assert request["source"] == "csms"
    assert (await read(charge_point))["attribute_value"] == "60"

    owner.reply = "Rejected"
    assert await write(charge_point, "70") == SetVariableStatusEnumType.rejected
    owner.reply = "RebootRequired"
    assert await write(charge_point, "80") == SetVariableStatusEnumType.reboot_required
    assert (await read(charge_point))["attribute_value"] == "60"

    # Outside max_limit: the device model rejects the write before it reaches the owner
    writes = owner.writes.call_count
    assert await write(charge_point, "101") == SetVariableStatusEnumType.rejected
    assert owner.writes.call_count == writes


@pytest.mark.asyncio
@pytest.mark.ocpp_version("ocpp2.0.1")
@pytest.mark.everest_core_config("everest-config-ocpp201-probe-module.yaml")
@pytest.mark.probe_module
@pytest.mark.everest_config_adaptions(DeviceModelConnectionAdjustment())
async def test_registration_at_runtime_is_applied_and_answered(probe_module, central_system: CentralSystem):
    owner = Owner(probe_module, [])
    charge_point = await start(probe_module, central_system)
    assert (await read(charge_point, "AuxCtrlr"))["attribute_status"] == GetVariableStatusEnumType.unknown_component

    probe_module.publish_variable(
        IMPLEMENTATION_ID, "registration", fan_speed_registration("10", owner="aux", component="AuxCtrlr")
    )
    await wait_for_calls(owner.registration_results, 1)
    assert owner.registration_results.call_args.args[0] == {"owner": "aux", "status": "Accepted", "rejected": []}
    assert (await read(charge_point, "AuxCtrlr"))["attribute_value"] == "10"

    # The owners of the OCPP module are not available to external owners
    probe_module.publish_variable(IMPLEMENTATION_ID, "registration", fan_speed_registration(owner="EVEREST"))
    await wait_for_calls(owner.registration_results, 2)
    assert owner.registration_results.call_args.args[0]["status"] == "OwnerInUse"

    # A registration without components releases the owner
    probe_module.publish_variable(IMPLEMENTATION_ID, "registration", {"owner": "aux", "components": []})
    await wait_for_calls(owner.registration_results, 3)
    assert owner.registration_results.call_args.args[0]["status"] == "Accepted"
    assert (await read(charge_point, "AuxCtrlr"))["attribute_status"] == GetVariableStatusEnumType.unknown_component


@pytest.mark.asyncio
@pytest.mark.ocpp_version("ocpp2.0.1")
@pytest.mark.everest_core_config("everest-config-ocpp201-probe-module.yaml")
@pytest.mark.probe_module
@pytest.mark.everest_config_adaptions(DeviceModelConnectionAdjustment())
@pytest.mark.ocpp_config_adaptions(
    GenericOCPP2XConfigAdjustment(
        [(OCPP2XConfigVariableIdentifier("MonitoringCtrlr", "MonitoringCtrlrEnabled", "Actual"), "true")]
    )
)
async def test_value_change_of_the_owner_triggers_a_monitor(
    probe_module, central_system: CentralSystem, test_utility: TestUtility
):
    Owner(probe_module, [fan_speed_registration()])
    charge_point = await start(probe_module, central_system)

    response = await charge_point.set_variable_monitoring_req(
        set_monitoring_data=[
            SetMonitoringDataType(
                value=50,
                type=MonitorEnumType.upper_threshold,
                severity=5,
                component=ComponentType(name="CoolingCtrlr"),
                variable=VariableType(name="FanSpeed"),
            )
        ]
    )
    assert response.set_monitoring_result[0]["status"] == SetMonitoringStatusEnumType.accepted
    monitor_id = response.set_monitoring_result[0]["id"]

    probe_module.publish_variable(
        IMPLEMENTATION_ID,
        "value_changed",
        {"component": {"name": "CoolingCtrlr"}, "variable": {"name": "FanSpeed"}, "value": "75"},
    )

    def validate_event(meta_data, msg, exp_payload):
        if msg.action != "NotifyEvent":
            return False
        return any(
            event.get("variableMonitoringId") == monitor_id and event.get("actualValue") == "75"
            for event in msg.payload.get("eventData", [])
        )

    assert await wait_for_and_validate(test_utility, charge_point, "NotifyEvent", {}, validate_event, timeout=30)
    assert (await read(charge_point))["attribute_value"] == "75"
