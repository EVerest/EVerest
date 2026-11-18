# SPDX-License-Identifier: Apache-2.0
# Copyright Pionix GmbH and Contributors to EVerest

"""Integration tests of the ocpp_device_model_API module from the side of an integrator: an external application
owns device model variables over MQTT, and an OCPP 1.6 key mapping file makes them configuration keys of the CSMS."""

import asyncio
import time
from pathlib import Path

import pytest
import pytest_asyncio

# fmt: off
from everest.testing.core_utils import EverestConfigAdjustmentStrategy
from everest.testing.core_utils.controller.everest_test_controller import EverestTestController
from everest.testing.core_utils.everest_core import EverestCore
from everest.testing.ocpp_utils.central_system import CentralSystem
from everest.testing.ocpp_utils.fixtures import *
from ocpp.v16.enums import ConfigurationStatus

from device_model_api_client import (API_TIMEOUTS, OWNER, DeviceModelApiConfigAdjustment, ExternalOwnerApp,
                                     cooling_unit_component, cooling_unit_registration, fan_speed_limit_variable,
                                     ocpp16_mapping, release, start_everest, write_ocpp16_mappings)
# fmt: on

DEADLINE = API_TIMEOUTS["cfg_expected_owners_timeout_s"]
KEY = "CoolingUnitFanSpeedLimit"
FAN_SPEED_LIMIT = ocpp16_mapping(KEY, "CoolingUnit", "FanSpeedLimit", instance="1", evse_id=1)
LIGHT_INTENSITY = ocpp16_mapping("LightIntensity", "StationLight", "Intensity")
INDICATOR_COLOR = ocpp16_mapping("IndicatorColor", "ChargingStatusIndicator", "Color")


class Ocpp16KeyMappings(EverestConfigAdjustmentStrategy):
    """Writes the mapping file and points Ocpp16KeyMappingsPath of OCPPmulti to it"""

    def __init__(self, directory: Path, mappings):
        self.path = write_ocpp16_mappings(directory, mappings)

    def adjust_everest_configuration(self, everest_config):
        everest_config["active_modules"]["ocpp"].setdefault("config_module", {})["Ocpp16KeyMappingsPath"] = \
            str(self.path.resolve())
        return everest_config


@pytest.fixture
def everest_config_strategies(request, everest_config_strategies, tmp_path):
    """Adds the mappings of the ocpp16_key_mappings marker, a list of mappings or the text of the file"""
    marker = request.node.get_closest_marker("ocpp16_key_mappings")
    mappings = marker.args[0] if marker else [FAN_SPEED_LIMIT]
    return [*everest_config_strategies, Ocpp16KeyMappings(tmp_path, mappings)]


def ocpp16(*marks):
    def decorate(test):
        for mark in reversed((pytest.mark.asyncio, pytest.mark.ocpp_version("ocpp1.6"),
                              pytest.mark.everest_core_config("everest-config-sil-ocpp.yaml"), *marks)):
            test = mark(test)
        return test

    return decorate


def expecting(owners=OWNER):
    return pytest.mark.everest_config_adaptions(DeviceModelApiConfigAdjustment({"cfg_expected_owners": owners}))


@pytest_asyncio.fixture
async def app(everest_core: EverestCore):
    app = ExternalOwnerApp(everest_core.mqtt_external_prefix)
    yield app
    if app.connected:
        app.clear_retained()
        await app.disconnect()


async def configuration(charge_point, *keys):
    """The configuration keys by name and the unknown keys of a GetConfiguration.req"""
    response = await charge_point.get_configuration_req(**({"key": list(keys)} if keys else {}))
    known = {entry["key"]: entry for entry in response.configuration_key or []}
    return known, response.unknown_key or []


async def change(charge_point, key, value):
    return (await charge_point.change_configuration_req(key=key, value=value)).status


def assert_accepted(result, rejected=()):
    assert result["status"] == "Accepted", result
    assert result["rejected"] == list(rejected), result


def station_light_registration(owner="station-light", data_type="integer", value="50"):
    return {"owner": owner, "components": [{
        "component": {"name": "StationLight"},
        "variables": [{
            "variable": {"name": "Intensity"},
            "characteristics": {"data_type": data_type, "supports_monitoring": False},
            "attributes": [{"type": "Actual", "value": value, "mutability": "ReadWrite"}],
        }],
    }]}


async def assert_ocpp_does_not_start(test_controller, everest_core, central_system, caplog, reason, within):
    try:
        await asyncio.to_thread(test_controller.start)
    except TimeoutError:
        pass
    with pytest.raises(asyncio.TimeoutError):
        await central_system.wait_for_chargepoint(timeout=within)
    stopped_by = time.monotonic() + 30
    while everest_core.process.poll() is None and time.monotonic() < stopped_by:
        await asyncio.sleep(0.5)
    assert everest_core.process.poll() is not None, "EVerest still runs without OCPP"
    assert reason in caplog.text


class TestExternalProcessFirst:

    @ocpp16(expecting())
    async def test_mapped_key_of_the_owner_is_served_from_the_first_boot(
            self, app: ExternalOwnerApp, central_system: CentralSystem, test_controller: EverestTestController):
        await app.connect()
        app.register(cooling_unit_registration())

        charge_point = await start_everest(test_controller, central_system)

        assert app.prompts == [{"expected_owners": [OWNER]}]
        assert_accepted(await app.wait_for_result())
        answered = app.received_at["set_registration_result"][0] - app.received_at["get_registrations"][0]
        assert answered < DEADLINE / 2, f"startup waited {answered:.1f} s for an owner that had registered"
        known, unknown = await configuration(charge_point, KEY)
        assert unknown == []
        assert (known[KEY]["value"], known[KEY]["readonly"]) == ("80", False)

    @ocpp16(expecting())
    async def test_retained_registration_is_used_without_republishing(
            self, everest_core: EverestCore, central_system: CentralSystem, test_controller: EverestTestController):
        app = ExternalOwnerApp(everest_core.mqtt_external_prefix, auto_republish=False)
        await app.connect()
        try:
            app.register(cooling_unit_registration(), retain=True)
            charge_point = await start_everest(test_controller, central_system)

            assert_accepted(await app.wait_for_result())
            known, _ = await configuration(charge_point, KEY)
            assert known[KEY]["value"] == "80"
        finally:
            app.clear_retained()
            await app.disconnect()


class TestEverestFirst:

    @ocpp16(expecting())
    async def test_mapped_key_appears_when_a_late_owner_registers(
            self, app: ExternalOwnerApp, central_system: CentralSystem, test_controller: EverestTestController):
        charge_point = await start_everest(test_controller, central_system, timeout=DEADLINE + 30)
        known, unknown = await configuration(charge_point, KEY)
        assert (known, unknown) == ({}, [KEY])
        assert await change(charge_point, KEY, "60") == ConfigurationStatus.not_supported

        await app.connect()
        app.register(cooling_unit_registration())
        assert_accepted(await app.wait_for_result())

        known, _ = await configuration(charge_point, KEY)
        assert (known[KEY]["value"], known[KEY]["readonly"]) == ("80", False)
        assert await change(charge_point, KEY, "60") == ConfigurationStatus.accepted
        assert (await configuration(charge_point, KEY))[0][KEY]["value"] == "60"

    @ocpp16(expecting())
    async def test_owner_that_stays_up_serves_the_key_again_after_a_restart(
            self, app: ExternalOwnerApp, central_system: CentralSystem, test_controller: EverestTestController):
        await app.connect()
        app.register(cooling_unit_registration())
        await start_everest(test_controller, central_system)
        assert_accepted(await app.wait_for_result())

        await asyncio.to_thread(test_controller.stop)
        charge_point = await start_everest(test_controller, central_system)

        assert len(app.prompts) == 2
        assert_accepted(await app.wait_for_result(count=2))
        assert (await configuration(charge_point, KEY))[0][KEY]["value"] == "80"


@pytest.mark.parametrize("reply, status, value", [
    ("Accepted", ConfigurationStatus.accepted, "60"),
    ("Rejected", ConfigurationStatus.rejected, "80"),
    ("RebootRequired", ConfigurationStatus.reboot_required, "80"),
    (None, ConfigurationStatus.rejected, "80"),
])
@ocpp16(expecting())
async def test_change_configuration_is_answered_by_the_owner(
        app: ExternalOwnerApp, central_system: CentralSystem, test_controller: EverestTestController,
        reply, status, value):
    await app.connect()
    app.register(cooling_unit_registration())
    charge_point = await start_everest(test_controller, central_system)
    assert_accepted(await app.wait_for_result())
    app.reply = reply

    assert await change(charge_point, KEY, "60") == status

    assert app.writes == [{"owner": OWNER, "component": cooling_unit_component(), "variable": {"name": "FanSpeedLimit"},
                           "attribute_type": "Actual", "value": "60", "source": "OCPP 1.6"}]
    assert (await configuration(charge_point, KEY))[0][KEY]["value"] == value


@ocpp16(expecting())
async def test_change_out_of_the_limits_does_not_reach_the_owner(
        app: ExternalOwnerApp, central_system: CentralSystem, test_controller: EverestTestController):
    await app.connect()
    app.register(cooling_unit_registration())
    charge_point = await start_everest(test_controller, central_system)
    assert_accepted(await app.wait_for_result())

    assert await change(charge_point, KEY, "150") == ConfigurationStatus.rejected
    assert app.writes == []


@ocpp16(expecting())
async def test_attribute_without_mutability_is_a_read_only_key(
        app: ExternalOwnerApp, central_system: CentralSystem, test_controller: EverestTestController):
    await app.connect()
    app.register(cooling_unit_registration(variables=[fan_speed_limit_variable(mutability=None)]))
    charge_point = await start_everest(test_controller, central_system)
    assert_accepted(await app.wait_for_result())

    known, _ = await configuration(charge_point, KEY)
    assert (known[KEY]["value"], known[KEY]["readonly"]) == ("80", True)
    assert await change(charge_point, KEY, "60") == ConfigurationStatus.rejected
    assert app.writes == []


@ocpp16(expecting())
async def test_value_change_of_the_owner_is_read_by_the_csms(
        app: ExternalOwnerApp, central_system: CentralSystem, test_controller: EverestTestController):
    await app.connect()
    app.register(cooling_unit_registration())
    charge_point = await start_everest(test_controller, central_system)
    assert_accepted(await app.wait_for_result())

    app.report(cooling_unit_component(), "FanSpeedLimit", "55")

    async def value():
        return (await configuration(charge_point, KEY))[0][KEY]["value"]

    deadline = time.monotonic() + 5
    while await value() != "55" and time.monotonic() < deadline:
        await asyncio.sleep(0.2)
    assert await value() == "55"


@ocpp16(expecting())
async def test_full_configuration_lists_only_the_mapped_keys_of_registered_variables(
        app: ExternalOwnerApp, central_system: CentralSystem, test_controller: EverestTestController):
    await app.connect()
    app.register(cooling_unit_registration())
    charge_point = await start_everest(test_controller, central_system)
    assert_accepted(await app.wait_for_result())

    known, _ = await configuration(charge_point)
    assert known[KEY]["value"] == "80"
    assert not {"Temperature", "Mode", "FanSpeedLimit"} & set(known)

    app.register(release())
    assert_accepted(await app.wait_for_result(count=2))
    known, _ = await configuration(charge_point)
    assert KEY not in known
    assert await change(charge_point, KEY, "60") == ConfigurationStatus.not_supported


@ocpp16(expecting(), pytest.mark.ocpp16_key_mappings([FAN_SPEED_LIMIT, INDICATOR_COLOR]))
async def test_variable_of_the_component_config_stays_with_the_component_config(
        app: ExternalOwnerApp, central_system: CentralSystem, test_controller: EverestTestController):
    await app.connect()
    indicator = {"component": {"name": "ChargingStatusIndicator"}, "variables": [{
        "variable": {"name": "Color"},
        "characteristics": {"data_type": "string", "supports_monitoring": False},
        "attributes": [{"type": "Actual", "value": "0000FF", "mutability": "ReadWrite"}],
    }]}
    registration = cooling_unit_registration()
    registration["components"].append(indicator)
    app.register(registration)
    charge_point = await start_everest(test_controller, central_system)

    assert_accepted(await app.wait_for_result(), [{
        "component": {"name": "ChargingStatusIndicator"}, "variable": {"name": "Color"}, "reason": "AlreadyDefined",
        "defined_by": "COMPONENT_CONFIG"}])
    assert await change(charge_point, "IndicatorColor", "00FF00") == ConfigurationStatus.accepted
    assert (await configuration(charge_point, "IndicatorColor"))[0]["IndicatorColor"]["value"] == "00FF00"
    assert app.writes == []


@ocpp16(expecting())
async def test_supported_feature_profiles_stay_with_the_ocpp_service(
        app: ExternalOwnerApp, central_system: CentralSystem, test_controller: EverestTestController):
    await app.connect()
    app.register(cooling_unit_registration())
    charge_point = await start_everest(test_controller, central_system)
    assert_accepted(await app.wait_for_result())
    profiles = (await configuration(charge_point, "SupportedFeatureProfiles"))[0]["SupportedFeatureProfiles"]

    app.register({"owner": "profiles", "components": [{
        "component": {"name": "OCPP16LegacyCtrlr"},
        "variables": [{"variable": {"name": "SupportedFeatureProfiles"},
                       "characteristics": {"data_type": "string", "supports_monitoring": False},
                       "attributes": [{"type": "Actual", "value": "Core", "mutability": "ReadOnly"}]}],
    }]})

    assert_accepted(await app.wait_for_result("profiles"), [{
        "component": {"name": "OCPP16LegacyCtrlr"}, "variable": {"name": "SupportedFeatureProfiles"},
        "reason": "AlreadyDefined", "defined_by": "COMPONENT_CONFIG"}])
    assert (await configuration(charge_point, "SupportedFeatureProfiles"))[0]["SupportedFeatureProfiles"] == profiles


@ocpp16(expecting("station-light"), pytest.mark.ocpp16_key_mappings([LIGHT_INTENSITY]))
async def test_built_in_key_mapped_to_an_owner_is_served_by_the_owner(
        app: ExternalOwnerApp, central_system: CentralSystem, test_controller: EverestTestController):
    await app.connect()
    app.register(station_light_registration())
    charge_point = await start_everest(test_controller, central_system)
    assert_accepted(await app.wait_for_result("station-light"))

    known, _ = await configuration(charge_point, "LightIntensity")
    assert (known["LightIntensity"]["value"], known["LightIntensity"]["readonly"]) == ("50", False)
    assert await change(charge_point, "LightIntensity", "70") == ConfigurationStatus.accepted
    assert app.writes == [{"owner": "station-light", "component": {"name": "StationLight"},
                           "variable": {"name": "Intensity"}, "attribute_type": "Actual", "value": "70",
                           "source": "OCPP 1.6"}]
    assert (await configuration(charge_point, "LightIntensity"))[0]["LightIntensity"]["value"] == "70"


async def assert_built_in_key_stays_with_the_ocpp_service(charge_point, app: ExternalOwnerApp):
    """The test configuration gives the built-in variable of LightIntensity no value"""
    assert await configuration(charge_point, "LightIntensity") == ({}, ["LightIntensity"])
    assert await change(charge_point, "LightIntensity", "70") == ConfigurationStatus.not_supported
    assert app.writes == []


@ocpp16(expecting("station-light"), pytest.mark.ocpp16_key_mappings([LIGHT_INTENSITY]))
async def test_built_in_key_mapped_to_an_owner_absent_at_startup_keeps_its_built_in_variable(
        app: ExternalOwnerApp, central_system: CentralSystem, test_controller: EverestTestController, caplog):
    charge_point = await start_everest(test_controller, central_system, timeout=DEADLINE + 30)
    assert "OCPP 1.6 key LightIntensity is custom-mapped to StationLight/Intensity" in caplog.text
    assert "using its built-in mapping instead" in caplog.text

    await app.connect()
    app.register(station_light_registration())
    assert_accepted(await app.wait_for_result("station-light"))
    await assert_built_in_key_stays_with_the_ocpp_service(charge_point, app)


@ocpp16(expecting("station-light"), pytest.mark.ocpp16_key_mappings([LIGHT_INTENSITY]))
async def test_built_in_key_mapped_to_a_variable_of_another_type_keeps_its_built_in_variable(
        app: ExternalOwnerApp, central_system: CentralSystem, test_controller: EverestTestController, caplog):
    await app.connect()
    app.register(station_light_registration(data_type="string", value="bright"))
    charge_point = await start_everest(test_controller, central_system)
    assert_accepted(await app.wait_for_result("station-light"))
    assert "with dataType string, but its built-in variable OCPP16LegacyCtrlr/LightIntensity" in caplog.text

    await assert_built_in_key_stays_with_the_ocpp_service(charge_point, app)


@ocpp16(expecting("station-light"), pytest.mark.ocpp16_key_mappings([LIGHT_INTENSITY]))
async def test_optional_built_in_key_is_unknown_after_its_owner_released_it(
        app: ExternalOwnerApp, central_system: CentralSystem, test_controller: EverestTestController):
    await app.connect()
    app.register(station_light_registration())
    charge_point = await start_everest(test_controller, central_system)
    assert_accepted(await app.wait_for_result("station-light"))

    app.register(release("station-light"))
    assert_accepted(await app.wait_for_result("station-light", count=2))

    assert await configuration(charge_point, "LightIntensity") == ({}, ["LightIntensity"])
    assert await change(charge_point, "LightIntensity", "70") == ConfigurationStatus.not_supported
    known, _ = await configuration(charge_point)
    assert "LightIntensity" not in known and "HeartbeatInterval" in known


@ocpp16(expecting("station"),
        pytest.mark.ocpp16_key_mappings([ocpp16_mapping("GetConfigurationMaxKeys", "Station", "MaxKeys")]))
async def test_get_configuration_answers_after_the_owner_of_a_required_built_in_key_released_it(
        app: ExternalOwnerApp, central_system: CentralSystem, test_controller: EverestTestController):
    await app.connect()
    app.register({"owner": "station", "components": [{
        "component": {"name": "Station"},
        "variables": [{"variable": {"name": "MaxKeys"},
                       "characteristics": {"data_type": "integer", "supports_monitoring": False},
                       "attributes": [{"type": "Actual", "value": "100", "mutability": "ReadOnly"}]}],
    }]})
    charge_point = await start_everest(test_controller, central_system)
    assert_accepted(await app.wait_for_result("station"))
    assert (await configuration(charge_point, "GetConfigurationMaxKeys"))[0]["GetConfigurationMaxKeys"]["value"] == "100"

    app.register(release("station"))
    assert_accepted(await app.wait_for_result("station", count=2))

    assert await configuration(charge_point, "GetConfigurationMaxKeys") == ({}, ["GetConfigurationMaxKeys"])
    known, _ = await configuration(charge_point)
    assert "GetConfigurationMaxKeys" not in known and "HeartbeatInterval" in known


@ocpp16(expecting("owner-a"))
async def test_second_owner_of_a_mapped_variable_is_rejected_and_changes_reach_the_first(
        app: ExternalOwnerApp, central_system: CentralSystem, test_controller: EverestTestController):
    await app.connect()
    app.register(cooling_unit_registration("owner-a", [fan_speed_limit_variable()]))
    charge_point = await start_everest(test_controller, central_system)
    assert_accepted(await app.wait_for_result("owner-a"))

    app.register(cooling_unit_registration("owner-b", [fan_speed_limit_variable("20")]))

    assert_accepted(await app.wait_for_result("owner-b"), [{
        "component": cooling_unit_component(), "variable": {"name": "FanSpeedLimit"}, "reason": "AlreadyDefined",
        "defined_by": "owner-a"}])
    assert (await configuration(charge_point, KEY))[0][KEY]["value"] == "80"
    assert await change(charge_point, KEY, "60") == ConfigurationStatus.accepted
    assert [w["owner"] for w in app.writes] == ["owner-a"]


@ocpp16(expecting(), pytest.mark.ocpp16_key_mappings([FAN_SPEED_LIMIT, {**FAN_SPEED_LIMIT, "ocpp16_key": "AnotherKey"}]))
async def test_second_key_for_a_variable_is_ignored(
        app: ExternalOwnerApp, central_system: CentralSystem, test_controller: EverestTestController, caplog):
    await app.connect()
    app.register(cooling_unit_registration())
    charge_point = await start_everest(test_controller, central_system)
    assert_accepted(await app.wait_for_result())

    assert "Ignoring the entry for key 'AnotherKey'" in caplog.text
    known, unknown = await configuration(charge_point, KEY, "AnotherKey")
    assert known[KEY]["value"] == "80" and unknown == ["AnotherKey"]


@ocpp16(expecting(), pytest.mark.ocpp16_key_mappings("mappings:\n  - ocpp16_key: [unclosed\n"))
async def test_malformed_mapping_file_stops_ocpp(
        everest_core: EverestCore, central_system: CentralSystem, test_controller: EverestTestController, caplog):
    await assert_ocpp_does_not_start(test_controller, everest_core, central_system, caplog, "YAML parsing error",
                                     within=15)
