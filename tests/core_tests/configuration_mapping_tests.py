# SPDX-License-Identifier: Apache-2.0
# Copyright Pionix GmbH and Contributors to EVerest

import asyncio
from copy import deepcopy

import pytest

from everest.testing.core_utils.common import Requirement
from everest.testing.core_utils import fixtures as core_fixtures
from everest.testing.core_utils.fixtures import *
from everest.testing.core_utils.probe_module import ProbeModule


class MappingConfiguration(EverestConfigAdjustmentStrategy):
    def __init__(self, directory, mode):
        self.directory = directory
        self.mode = mode

    def adjust_everest_configuration(self, everest_config):
        (self.directory / "ocpp").mkdir(exist_ok=True)
        (self.directory / "ocpp16").mkdir(exist_ok=True)
        config = deepcopy(everest_config)
        modules = config["active_modules"]
        modules["ocpp"]["config_module"].update({
            "Mode": self.mode,
            "CoreDatabasePath": str(self.directory / "ocpp"),
            "DatabasePath": str(self.directory / "ocpp16"),
            "UserConfigPath": str(self.directory / "ocpp16-user.json"),
            "DeviceModelDatabasePath": str(self.directory / "device_model.db"),
            "EverestDeviceModelDatabasePath": "",
        })
        for module in modules.values():
            if module["module"] == "EvManager":
                module.setdefault("config_module", {})["auto_exec"] = False
        return config


@pytest.fixture
def everest_config_strategies(request, tmp_path):
    return [*core_fixtures.everest_config_strategies.__wrapped__(request),
            MappingConfiguration(tmp_path, request.param)]


def component_variable(component, variable, evse_id=None):
    identity = {"component": {"name": component}, "variable": {"name": variable}}
    if evse_id is not None:
        identity["component"]["evse"] = {"id": evse_id}
    return identity


async def wait_for_value(probe, identity, expected):
    async with asyncio.timeout(15):
        while True:
            result = await probe.call_command(
                "ocpp", "get_variables", {"requests": [{"component_variable": identity}]})
            if result and result[0]["status"] == "Accepted" and result[0].get("value") == expected:
                return
            await asyncio.sleep(0.1)


@pytest.mark.asyncio
@pytest.mark.xdist_group(name="ISO15118")
@pytest.mark.use_temporary_persistent_store
@pytest.mark.everest_core_config("config-sil-ocpp201.yaml")
@pytest.mark.parametrize("everest_config_strategies", ["Only1.6", "Only2"], indirect=True)
@pytest.mark.probe_module(
    connections={"ocpp": [Requirement("ocpp", "ocpp_generic")]},
    access={"config": {"modules": {
        module: {"allow_read": True, "allow_write": True}
        for module in ("auth", "evse_manager_1", "evse_manager_2")
    }}},
)
async def test_runtime_configuration_mappings(everest_core):
    everest_core.start(standalone_module="probe")
    probe_module = ProbeModule(everest_core.get_runtime_session())
    probe_module.start()
    await probe_module.wait_to_be_ready()
    entries = [
        ("auth", "connection_timeout", component_variable("TxCtrlr", "EVConnectionTimeOut"), "91", "92"),
        ("auth", "master_pass_group_id", component_variable("AuthCtrlr", "MasterPassGroupId"), "Managers", ""),
    ]
    for evse_id in (1, 2):
        for parameter, variable in (
            ("payment_enable_contract", "PnCEnabled"),
            ("central_contract_validation_allowed", "CentralContractValidationAllowed"),
            ("contract_certificate_installation_enabled", "ContractCertificateInstallationEnabled"),
        ):
            entries.append((f"evse_manager_{evse_id}", parameter,
                            component_variable("ISO15118Ctrlr", variable, evse_id), "false", "true"))

    for module, parameter, identity, config_value, ocpp_value in entries:
        initial = await probe_module.get_config_value(module_id=module, param_name=parameter)
        assert initial["status"] == "Ok", initial
        await wait_for_value(probe_module, identity, initial["value"])

        other_identity = None
        if module == "evse_manager_1":
            other_identity = component_variable("ISO15118Ctrlr", identity["variable"]["name"], 2)
            other = await probe_module.get_config_value(module_id="evse_manager_2", param_name=parameter)
            assert other["status"] == "Ok", other

        result = await probe_module.set_config_value(module_id=module, param_name=parameter, value=config_value)
        assert result["status"] == "Ok" and result["set_status"] == "Accepted", result
        await wait_for_value(probe_module, identity, config_value)

        result = await probe_module.call_command("ocpp", "set_variables", {
            "requests": [{"component_variable": identity, "value": ocpp_value}],
            "source": "runtime-configuration-test",
        })
        assert result[0]["status"] == "Accepted", result
        effective = await probe_module.get_config_value(module_id=module, param_name=parameter)
        assert effective["status"] == "Ok" and effective["value"] == ocpp_value, effective
        await wait_for_value(probe_module, identity, ocpp_value)
        if other_identity is not None:
            await wait_for_value(probe_module, other_identity, other["value"])

    for variable in ("PnCEnabled", "CentralContractValidationAllowed", "ContractCertificateInstallationEnabled"):
        identity = component_variable("ISO15118Ctrlr", variable)
        result = await probe_module.call_command(
            "ocpp", "get_variables", {"requests": [{"component_variable": identity}]})
        assert result[0]["status"] in ("UnknownComponent", "UnknownVariable"), result
