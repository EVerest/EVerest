# SPDX-License-Identifier: Apache-2.0
# Copyright Pionix GmbH and Contributors to EVerest

"""Integration tests of the ocpp_device_model_API module from the side of an integrator: an external application
owns variables of the OCPP 2.x device model over MQTT, OCPPmulti serves them to the CSMS."""

import asyncio
import itertools
import time
from pathlib import Path

import pytest
import pytest_asyncio

# fmt: off
from everest.testing.core_utils._configuration.libocpp_configuration_helper import GenericOCPP2XConfigAdjustment, OCPP2XConfigVariableIdentifier, OCPPConfigAdjustmentStrategyWrapper
from everest.testing.core_utils.controller.everest_test_controller import EverestTestController
from everest.testing.core_utils.everest_core import EverestCore
from everest.testing.ocpp_utils.central_system import CentralSystem
from everest.testing.ocpp_utils.charge_point_utils import TestUtility, wait_for_and_validate
from everest.testing.ocpp_utils.fixtures import *
from ocpp.v201 import call
from ocpp.v201.datatypes import ComponentType, ComponentVariableType, EVSEType, GetVariableDataType, SetMonitoringDataType, SetVariableDataType, VariableType
from ocpp.v201.enums import AttributeEnumType, GetVariableStatusEnumType, MonitorEnumType, ReportBaseEnumType, SetMonitoringStatusEnumType, SetVariableStatusEnumType

from device_model_api_client import (API_MODULE_ID, API_TIMEOUTS, OWNER, DeviceModelApiConfigAdjustment, ExternalOwnerApp,
                                     cooling_unit_component, cooling_unit_registration, fan_speed_limit_variable,
                                     mode_variable, release, start_everest, temperature_variable)
# fmt: on

DEADLINE = API_TIMEOUTS["cfg_expected_owners_timeout_s"]
EXPECTING_OWNER = DeviceModelApiConfigAdjustment({"cfg_expected_owners": OWNER})
MONITORING = GenericOCPP2XConfigAdjustment(
    [(OCPP2XConfigVariableIdentifier("MonitoringCtrlr", "MonitoringCtrlrEnabled", "Actual"), "true")])
# The OCPP service takes the answers of its API modules in the order of its connections, so the registrations an
# owner of the second module publishes while the first one still waits arrive before the startup answer
SECOND_OF_TWO_MODULES = DeviceModelApiConfigAdjustment(modules={
    "slow_device_model_api": {"cfg_expected_owners": "never-registers", "cfg_expected_owners_timeout_s": 6},
    API_MODULE_ID: {"cfg_expected_owners": OWNER},
})
_request_ids = itertools.count(1)


def ocpp201(*marks):
    def decorate(test):
        for mark in reversed((pytest.mark.asyncio, pytest.mark.ocpp_version("ocpp2.0.1"),
                              pytest.mark.everest_core_config("everest-config-ocpp201.yaml"), *marks)):
            test = mark(test)
        return test

    return decorate


@pytest_asyncio.fixture
async def app(everest_core: EverestCore):
    app = ExternalOwnerApp(everest_core.mqtt_external_prefix)
    yield app
    if app.connected:
        app.clear_retained()
        await app.disconnect()


def cooling_unit(evse_id=1) -> ComponentType:
    return ComponentType(name="CoolingUnit", instance="1", evse=EVSEType(id=evse_id))


async def read(charge_point, variable, component=None):
    response = await charge_point.get_variables_req(get_variable_data=[
        GetVariableDataType(component=component or cooling_unit(), variable=VariableType(name=variable),
                            attribute_type=AttributeEnumType.actual)
    ])
    return response.get_variable_result[0]


async def write(charge_point, variable, value, component=None, attribute_type=AttributeEnumType.actual):
    response = await charge_point.set_variables_req(set_variable_data=[
        SetVariableDataType(attribute_value=value, attribute_type=attribute_type,
                            component=component or cooling_unit(), variable=VariableType(name=variable))
    ])
    return response.set_variable_result[0]["attribute_status"]


async def monitors(charge_point, variable, component=None):
    """The monitors of a variable from GetMonitoringReport"""
    request_id = next(_request_ids)
    target = component or cooling_unit()
    await charge_point.call(call.GetMonitoringReport(
        request_id=request_id,
        component_variable=[ComponentVariableType(component=target, variable=VariableType(name=variable))]))

    def validate(meta_data, msg, exp_payload):
        return msg.payload.get("requestId") == request_id and msg.payload

    report = await wait_for_and_validate(TestUtility(), charge_point, "NotifyMonitoringReport", {}, validate, 10)
    assert report, "no NotifyMonitoringReport"
    return [monitor for entry in report.get("monitor", []) for monitor in entry["variableMonitoring"]]


async def full_inventory(charge_point):
    request_id = next(_request_ids)
    await charge_point.get_base_report_req(request_id=request_id, report_base=ReportBaseEnumType.full_inventory)
    report = []

    def validate(meta_data, msg, exp_payload):
        if msg.payload.get("requestId") != request_id:
            return False
        report.extend(msg.payload.get("reportData", []))
        return not msg.payload.get("tbc", False)

    assert await wait_for_and_validate(TestUtility(), charge_point, "NotifyReport", {}, validate, 30)
    return report


async def restart(test_controller, central_system):
    await asyncio.to_thread(test_controller.stop)
    return await start_everest(test_controller, central_system)


def assert_accepted(result, rejected=()):
    assert result["status"] == "Accepted", result
    assert result["rejected"] == list(rejected), result


def rejected(variable, reason, defined_by=None, component=None):
    entry = {"component": component or cooling_unit_component(), "variable": {"name": variable}, "reason": reason}
    if defined_by is not None:
        entry["defined_by"] = defined_by
    return entry


def preconfigured(monitor_list):
    return [m for m in monitor_list if m["type"] == MonitorEnumType.upper_threshold and m["severity"] == 4]


def add_component_config(name, schema):
    return OCPPConfigAdjustmentStrategyWrapper(lambda config: {**config, name: schema})


COOLING_UNIT_MODE_CONFIG = add_component_config("CoolingUnit_1", {
    "$schema": "http://json-schema.org/draft-07/schema#",
    "description": "Cooling unit with the mode defined in the component config",
    "type": "object",
    "name": "CoolingUnit",
    "instance": "1",
    "evse_id": 1,
    "properties": {
        "Mode": {
            "variable_name": "Mode",
            "characteristics": {"supportsMonitoring": True, "dataType": "OptionList", "valuesList": "Eco,Normal,Boost"},
            "attributes": [{"type": "Actual", "mutability": "ReadWrite", "value": "Normal"}],
            "description": "Mode of the cooling unit",
            "type": "string",
        }
    },
    "required": [],
})


class TestExternalProcessFirst:

    @ocpp201(pytest.mark.everest_config_adaptions(EXPECTING_OWNER))
    async def test_app_republishes_when_asked_and_boot_does_not_wait_for_the_deadline(
            self, app: ExternalOwnerApp, central_system: CentralSystem, test_controller: EverestTestController):
        await app.connect()
        # nobody listens yet, so this registration is lost
        app.register(cooling_unit_registration())

        charge_point = await start_everest(test_controller, central_system)

        assert app.prompts == [{"expected_owners": [OWNER]}]
        assert_accepted(await app.wait_for_result())
        answered = app.received_at["set_registration_result"][0] - app.received_at["get_registrations"][0]
        assert answered < DEADLINE / 2, f"startup waited {answered:.1f} s for an owner that had registered"

        result = await read(charge_point, "FanSpeedLimit")
        assert result["attribute_status"] == GetVariableStatusEnumType.accepted
        assert result["attribute_value"] == "80"

    @ocpp201(pytest.mark.everest_config_adaptions(EXPECTING_OWNER))
    async def test_retained_registration_is_used_without_republishing(
            self, everest_core: EverestCore, central_system: CentralSystem, test_controller: EverestTestController):
        app = ExternalOwnerApp(everest_core.mqtt_external_prefix, auto_republish=False)
        await app.connect()
        try:
            app.register(cooling_unit_registration(), retain=True)

            charge_point = await start_everest(test_controller, central_system)

            assert_accepted(await app.wait_for_result())
            answered = app.received_at["set_registration_result"][0] - app.received_at["get_registrations"][0]
            assert answered < DEADLINE / 2, f"startup waited {answered:.1f} s for a retained registration"
            assert (await read(charge_point, "FanSpeedLimit"))["attribute_value"] == "80"
        finally:
            app.clear_retained()
            await app.disconnect()

    @ocpp201(pytest.mark.everest_config_adaptions(
        DeviceModelApiConfigAdjustment({"cfg_expected_owners": f"{OWNER}, station-light"})))
    async def test_every_expected_owner_and_an_unexpected_one_are_in_the_inventory(
            self, app: ExternalOwnerApp, central_system: CentralSystem, test_controller: EverestTestController):
        await app.connect()
        # republished first, so it arrives while the API module still waits for the expected owners
        app.register(cooling_unit_registration("unexpected", [mode_variable()], cooling_unit_component(2)))
        app.register(cooling_unit_registration())
        app.register({"owner": "station-light", "components": [{
            "component": {"name": "StationLight"},
            "variables": [{
                "variable": {"name": "Intensity"},
                "characteristics": {"data_type": "integer", "supports_monitoring": False, "min_limit": 0,
                                    "max_limit": 100},
                "attributes": [{"type": "Actual", "value": "50", "mutability": "ReadWrite"}],
            }],
        }]})

        charge_point = await start_everest(test_controller, central_system)

        for owner in (OWNER, "station-light", "unexpected"):
            assert_accepted(await app.wait_for_result(owner))
        inventory = {(entry["component"]["name"], entry["component"].get("evse", {}).get("id"),
                      entry["variable"]["name"]) for entry in await full_inventory(charge_point)}
        assert {("CoolingUnit", 1, "FanSpeedLimit"), ("CoolingUnit", 1, "Temperature"), ("CoolingUnit", 1, "Mode"),
                ("StationLight", None, "Intensity"), ("CoolingUnit", 2, "Mode")} <= inventory

    @ocpp201(pytest.mark.everest_config_adaptions(SECOND_OF_TWO_MODULES))
    async def test_latest_registration_before_the_startup_answer_wins(
            self, app: ExternalOwnerApp, central_system: CentralSystem, test_controller: EverestTestController):
        await app.connect()
        app.register(cooling_unit_registration())
        booting = asyncio.create_task(start_everest(test_controller, central_system))
        await app.wait_for(lambda: app.prompts, 60, "the prompt")
        await asyncio.sleep(1.5)
        assert not booting.done()

        app.register(cooling_unit_registration(
            variables=[temperature_variable(), fan_speed_limit_variable("60"), mode_variable("Eco")]))
        app.report(cooling_unit_component(), "Temperature", "50.5")
        app.report(cooling_unit_component(), "Humidity", "30")

        charge_point = await booting
        assert_accepted(await app.wait_for_result())
        assert app.results_of(OWNER)[-1]["status"] == "Accepted"
        assert (await read(charge_point, "FanSpeedLimit"))["attribute_value"] == "60"
        assert (await read(charge_point, "Mode"))["attribute_value"] == "Eco"
        assert (await read(charge_point, "Temperature"))["attribute_value"] == "50.5"
        assert (await read(charge_point, "Humidity"))["attribute_status"] == GetVariableStatusEnumType.unknown_variable

    @ocpp201(pytest.mark.everest_config_adaptions(SECOND_OF_TWO_MODULES))
    async def test_release_before_the_startup_answer_removes_the_owner(
            self, app: ExternalOwnerApp, central_system: CentralSystem, test_controller: EverestTestController):
        await app.connect()
        app.register(cooling_unit_registration())
        booting = asyncio.create_task(start_everest(test_controller, central_system))
        await app.wait_for(lambda: app.prompts, 60, "the prompt")
        await asyncio.sleep(1.5)
        assert not booting.done()

        app.register(release())

        charge_point = await booting
        await app.wait_for(lambda: app.results_of(OWNER), 10, "a registration result")
        assert app.results_of(OWNER)[-1]["status"] == "Accepted"
        result = await read(charge_point, "FanSpeedLimit")
        assert result["attribute_status"] == GetVariableStatusEnumType.unknown_component


class TestEverestFirst:

    @ocpp201(pytest.mark.everest_config_adaptions(EXPECTING_OWNER))
    async def test_app_started_while_ocpp_waits_for_its_owners(
            self, app: ExternalOwnerApp, central_system: CentralSystem, test_controller: EverestTestController,
            everest_core: EverestCore):
        booting = asyncio.create_task(start_everest(test_controller, central_system))
        await asyncio.to_thread(everest_core.all_modules_started_event.wait, 60)
        await asyncio.sleep(3)

        await app.connect()
        assert not booting.done(), "OCPP connected without waiting for the expected owner"
        registered_at = time.monotonic()
        app.register(cooling_unit_registration())

        charge_point = await booting
        booted_at = time.monotonic()
        assert app.prompts == [], "the prompt was published before the app subscribed"
        assert_accepted(await app.wait_for_result())
        assert booted_at - registered_at < DEADLINE, "boot waited for the deadline although the owner registered"
        assert (await read(charge_point, "FanSpeedLimit"))["attribute_value"] == "80"

    @ocpp201(pytest.mark.everest_config_adaptions(EXPECTING_OWNER))
    async def test_app_that_misses_the_deadline_registers_at_runtime(
            self, app: ExternalOwnerApp, central_system: CentralSystem, test_controller: EverestTestController):
        charge_point = await start_everest(test_controller, central_system)
        assert (await read(charge_point, "FanSpeedLimit"))["attribute_status"] == \
            GetVariableStatusEnumType.unknown_component

        await app.connect()
        app.register(cooling_unit_registration())

        assert_accepted(await app.wait_for_result())
        assert app.prompts == []
        assert (await read(charge_point, "FanSpeedLimit"))["attribute_value"] == "80"

    @ocpp201(pytest.mark.everest_config_adaptions(EXPECTING_OWNER), pytest.mark.ocpp_config_adaptions(MONITORING))
    async def test_late_owner_after_restart_gets_its_declared_monitors_only(
            self, everest_core: EverestCore, central_system: CentralSystem, test_controller: EverestTestController):
        app = ExternalOwnerApp(everest_core.mqtt_external_prefix)
        await app.connect()
        app.register(cooling_unit_registration())
        charge_point = await start_everest(test_controller, central_system)
        assert_accepted(await app.wait_for_result())
        response = await charge_point.set_variable_monitoring_req(set_monitoring_data=[SetMonitoringDataType(
            value=5, type=MonitorEnumType.lower_threshold, severity=6, component=cooling_unit(),
            variable=VariableType(name="Temperature"))])
        assert response.set_monitoring_result[0]["status"] == SetMonitoringStatusEnumType.accepted
        await app.disconnect()

        charge_point = await restart(test_controller, central_system)
        app = ExternalOwnerApp(everest_core.mqtt_external_prefix)
        await app.connect()
        try:
            app.register(cooling_unit_registration())
            assert_accepted(await app.wait_for_result())

            found = await monitors(charge_point, "Temperature")
            assert [(m["type"], m["value"]) for m in found] == [(MonitorEnumType.upper_threshold, 70)], found
        finally:
            await app.disconnect()

    @ocpp201(pytest.mark.everest_config_adaptions(EXPECTING_OWNER), pytest.mark.ocpp_config_adaptions(MONITORING))
    async def test_owner_on_time_after_restart_keeps_the_monitor_the_csms_changed(
            self, app: ExternalOwnerApp, central_system: CentralSystem, test_controller: EverestTestController):
        await app.connect()
        app.register(cooling_unit_registration())
        charge_point = await start_everest(test_controller, central_system)
        assert_accepted(await app.wait_for_result())
        [monitor] = preconfigured(await monitors(charge_point, "Temperature"))
        response = await charge_point.set_variable_monitoring_req(set_monitoring_data=[SetMonitoringDataType(
            id=monitor["id"], value=75, type=MonitorEnumType.upper_threshold, severity=4, component=cooling_unit(),
            variable=VariableType(name="Temperature"))])
        assert response.set_monitoring_result[0]["status"] == SetMonitoringStatusEnumType.accepted

        charge_point = await restart(test_controller, central_system)

        assert len(app.prompts) == 2
        assert_accepted(await app.wait_for_result(count=2))
        [kept] = preconfigured(await monitors(charge_point, "Temperature"))
        assert (kept["id"], kept["value"]) == (monitor["id"], 75)


@pytest.mark.parametrize("reply, status, value", [
    ("Accepted", SetVariableStatusEnumType.accepted, "60"),
    ("Rejected", SetVariableStatusEnumType.rejected, "80"),
    ("RebootRequired", SetVariableStatusEnumType.reboot_required, "80"),
    (None, SetVariableStatusEnumType.rejected, "80"),
])
@ocpp201(pytest.mark.everest_config_adaptions(EXPECTING_OWNER))
async def test_write_of_the_csms_is_answered_by_the_owner(
        app: ExternalOwnerApp, central_system: CentralSystem, test_controller: EverestTestController,
        reply, status, value):
    await app.connect()
    app.register(cooling_unit_registration())
    charge_point = await start_everest(test_controller, central_system)
    assert_accepted(await app.wait_for_result())
    app.reply = reply

    assert await write(charge_point, "FanSpeedLimit", "60") == status

    assert app.writes == [{"owner": OWNER, "component": cooling_unit_component(), "variable": {"name": "FanSpeedLimit"},
                           "attribute_type": "Actual", "value": "60", "source": "csms"}]
    assert (await read(charge_point, "FanSpeedLimit"))["attribute_value"] == value
    if reply is None:
        # a reply after the timeout is not taken for the next write
        app.send_reply(0, "Accepted")
        app.reply = lambda request: None if request["value"] == "65" else "Accepted"
        assert await write(charge_point, "FanSpeedLimit", "65") == SetVariableStatusEnumType.rejected
        assert (await read(charge_point, "FanSpeedLimit"))["attribute_value"] == "80"


@ocpp201(pytest.mark.everest_config_adaptions(EXPECTING_OWNER))
async def test_writes_the_device_model_refuses_do_not_reach_the_owner(
        app: ExternalOwnerApp, central_system: CentralSystem, test_controller: EverestTestController):
    await app.connect()
    app.register(cooling_unit_registration())
    charge_point = await start_everest(test_controller, central_system)
    assert_accepted(await app.wait_for_result())

    assert await write(charge_point, "FanSpeedLimit", "150") == SetVariableStatusEnumType.rejected
    assert await write(charge_point, "Temperature", "20") == SetVariableStatusEnumType.rejected
    assert await write(charge_point, "Mode", "Turbo") == SetVariableStatusEnumType.rejected
    assert await write(charge_point, "FanSpeedLimit", "60", attribute_type=AttributeEnumType.target) == \
        SetVariableStatusEnumType.not_supported_attribute_type
    assert app.writes == []
    assert (await read(charge_point, "FanSpeedLimit"))["attribute_value"] == "80"


@ocpp201(pytest.mark.everest_config_adaptions(EXPECTING_OWNER), pytest.mark.ocpp_config_adaptions(MONITORING))
async def test_value_change_of_the_owner_triggers_its_declared_monitor(
        app: ExternalOwnerApp, central_system: CentralSystem, test_controller: EverestTestController):
    await app.connect()
    app.register(cooling_unit_registration())
    charge_point = await start_everest(test_controller, central_system)
    assert_accepted(await app.wait_for_result())
    [monitor] = preconfigured(await monitors(charge_point, "Temperature"))

    def event_of(value, cleared):
        def validate(meta_data, msg, exp_payload):
            return any(event.get("variableMonitoringId") == monitor["id"] and event.get("actualValue") == value
                       and event.get("cleared", False) == cleared for event in msg.payload.get("eventData", []))
        return validate

    app.report(cooling_unit_component(), "Temperature", "71")
    assert await wait_for_and_validate(TestUtility(), charge_point, "NotifyEvent", {}, event_of("71", False), 30)
    app.report(cooling_unit_component(), "Temperature", "65")
    assert await wait_for_and_validate(TestUtility(), charge_point, "NotifyEvent", {}, event_of("65", True), 30)


@ocpp201(pytest.mark.everest_config_adaptions(EXPECTING_OWNER))
async def test_released_owner_disappears_and_its_name_is_free_again(
        app: ExternalOwnerApp, central_system: CentralSystem, test_controller: EverestTestController):
    await app.connect()
    app.register(cooling_unit_registration())
    charge_point = await start_everest(test_controller, central_system)
    assert_accepted(await app.wait_for_result())

    app.register(release())
    assert_accepted(await app.wait_for_result(count=2))
    assert (await read(charge_point, "FanSpeedLimit"))["attribute_status"] == \
        GetVariableStatusEnumType.unknown_component

    app.register(cooling_unit_registration(variables=[fan_speed_limit_variable("30")]))
    assert_accepted(await app.wait_for_result(count=3))
    assert (await read(charge_point, "FanSpeedLimit"))["attribute_value"] == "30"


@ocpp201(pytest.mark.everest_config_adaptions(EXPECTING_OWNER))
async def test_value_changes_that_do_not_fit_a_registration_are_ignored(
        app: ExternalOwnerApp, central_system: CentralSystem, test_controller: EverestTestController):
    await app.connect()
    app.register(cooling_unit_registration())
    charge_point = await start_everest(test_controller, central_system)
    assert_accepted(await app.wait_for_result())

    app.publish("m2e/value_changed", "not json")
    app.report({"name": "C" * 51, "instance": "1", "evse": {"id": 1}}, "FanSpeedLimit", "10")
    app.report(cooling_unit_component(), "Humidity", "30")
    app.report(cooling_unit_component(), "FanSpeedLimit", "20", owner="someone-else")
    app.report(cooling_unit_component(), "FanSpeedLimit", "55")

    await asyncio.sleep(1)
    assert (await read(charge_point, "FanSpeedLimit"))["attribute_value"] == "55"
    assert (await read(charge_point, "Humidity"))["attribute_status"] == GetVariableStatusEnumType.unknown_variable


@ocpp201(pytest.mark.everest_config_adaptions(EXPECTING_OWNER),
         pytest.mark.ocpp_config_adaptions(COOLING_UNIT_MODE_CONFIG))
async def test_variable_of_the_component_config_stays_with_the_component_config(
        app: ExternalOwnerApp, central_system: CentralSystem, test_controller: EverestTestController):
    await app.connect()
    app.register(cooling_unit_registration())
    charge_point = await start_everest(test_controller, central_system)

    assert_accepted(await app.wait_for_result(), [rejected("Mode", "AlreadyDefined", "COMPONENT_CONFIG")])
    assert await write(charge_point, "Mode", "Eco") == SetVariableStatusEnumType.accepted
    assert (await read(charge_point, "Mode"))["attribute_value"] == "Eco"
    assert app.writes == []
    assert await write(charge_point, "FanSpeedLimit", "60") == SetVariableStatusEnumType.accepted
    assert [w["variable"]["name"] for w in app.writes] == ["FanSpeedLimit"]


@ocpp201(pytest.mark.everest_config_adaptions(EXPECTING_OWNER))
async def test_variables_an_owner_may_not_define_are_rejected(
        app: ExternalOwnerApp, central_system: CentralSystem, test_controller: EverestTestController):
    await app.connect()
    app.register(cooling_unit_registration())
    charge_point = await start_everest(test_controller, central_system)
    assert_accepted(await app.wait_for_result())
    storage = await read(charge_point, "Storage", ComponentType(name="AuthCacheCtrlr"))

    app.register({"owner": "auth-cache", "components": [{
        "component": {"name": "AuthCacheCtrlr"},
        "variables": [{"variable": {"name": "Storage"},
                       "characteristics": {"data_type": "integer", "supports_monitoring": False},
                       "attributes": [{"type": "Actual", "value": "1", "mutability": "ReadOnly"}]}],
    }]})
    assert_accepted(await app.wait_for_result("auth-cache"),
                    [rejected("Storage", "ReservedVariable", component={"name": "AuthCacheCtrlr"})])
    assert await read(charge_point, "Storage", ComponentType(name="AuthCacheCtrlr")) == storage

    app.register(cooling_unit_registration("COMPONENT_CONFIG", [mode_variable()], cooling_unit_component(2)))
    assert_accepted(await app.wait_for_result("COMPONENT_CONFIG"),
                    [rejected("Mode", "ReservedOwner", component=cooling_unit_component(2))])

    no_attributes = mode_variable()
    no_attributes["attributes"] = []
    app.register(cooling_unit_registration("no-attributes", [no_attributes], cooling_unit_component(2)))
    assert_accepted(await app.wait_for_result("no-attributes"),
                    [rejected("Mode", "NoAttributes", component=cooling_unit_component(2))])

    empty_instance = {"name": "CoolingUnit", "instance": "", "evse": {"id": 2}}
    app.register(cooling_unit_registration("empty-instance", [mode_variable()], empty_instance))
    assert_accepted(await app.wait_for_result("empty-instance"),
                    [rejected("Mode", "InvalidNameKey", component=empty_instance)])
    assert (await read(charge_point, "Mode", cooling_unit(2)))["attribute_status"] == \
        GetVariableStatusEnumType.unknown_component


@ocpp201(pytest.mark.everest_config_adaptions(DeviceModelApiConfigAdjustment({"cfg_expected_owners": "owner-a"})))
async def test_second_owner_of_a_variable_is_rejected_and_writes_reach_the_first(
        app: ExternalOwnerApp, central_system: CentralSystem, test_controller: EverestTestController):
    await app.connect()
    app.register(cooling_unit_registration("owner-a", [mode_variable()]))
    charge_point = await start_everest(test_controller, central_system)
    assert_accepted(await app.wait_for_result("owner-a"))

    app.register(cooling_unit_registration("owner-b", [mode_variable("Boost"), fan_speed_limit_variable()]))

    assert_accepted(await app.wait_for_result("owner-b"), [rejected("Mode", "AlreadyDefined", "owner-a")])
    assert (await read(charge_point, "Mode"))["attribute_value"] == "Normal"
    assert await write(charge_point, "Mode", "Eco") == SetVariableStatusEnumType.accepted
    assert [(w["owner"], w["variable"]["name"]) for w in app.writes] == [("owner-a", "Mode")]


@ocpp201(pytest.mark.everest_config_adaptions(EXPECTING_OWNER),
         pytest.mark.ocpp_config_adaptions(MONITORING, COOLING_UNIT_MODE_CONFIG))
async def test_variable_moved_from_the_component_config_to_an_owner_keeps_its_monitor(
        app: ExternalOwnerApp, central_system: CentralSystem, test_controller: EverestTestController, tmp_path: Path):
    charge_point = await start_everest(test_controller, central_system, timeout=DEADLINE + 30)
    response = await charge_point.set_variable_monitoring_req(set_monitoring_data=[SetMonitoringDataType(
        value=1, type=MonitorEnumType.delta, severity=5, component=cooling_unit(), variable=VariableType(name="Mode"))])
    assert response.set_monitoring_result[0]["status"] == SetMonitoringStatusEnumType.accepted
    monitor_id = response.set_monitoring_result[0]["id"]

    await asyncio.to_thread(test_controller.stop)
    component_config = tmp_path / "ocpp_config" / "component_config" / "standardized" / "CoolingUnit_1.json"
    assert component_config.exists()
    component_config.unlink()
    await app.connect()
    # registered with the characteristics it had in the component config
    mode = mode_variable("Boost")
    mode["characteristics"]["supports_monitoring"] = True
    app.register(cooling_unit_registration(variables=[mode]))
    charge_point = await start_everest(test_controller, central_system)

    assert_accepted(await app.wait_for_result())
    assert (await read(charge_point, "Mode"))["attribute_value"] == "Boost"
    assert [(m["id"], m["type"]) for m in await monitors(charge_point, "Mode")] == [(monitor_id, MonitorEnumType.delta)]
