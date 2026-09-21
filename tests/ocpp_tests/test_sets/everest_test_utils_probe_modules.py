# SPDX-License-Identifier: Apache-2.0
# Copyright Pionix GmbH and Contributors to EVerest

import logging
from unittest.mock import Mock

import pytest
import pytest_asyncio

from copy import deepcopy
from typing import Dict, List

from everest.testing.ocpp_utils.central_system import CentralSystem
from everest.testing.core_utils.probe_module import ProbeModule
from everest.testing.core_utils import EverestConfigAdjustmentStrategy


@pytest.fixture
def probe_module(started_test_controller, everest_core) -> ProbeModule:
    # initiate the probe module, connecting to the same runtime session the test controller started
    module = ProbeModule(everest_core.get_runtime_session())

    return module


@pytest_asyncio.fixture
async def chargepoint_with_pm(central_system: CentralSystem, probe_module: ProbeModule):
    """Fixture for ChargePoint201. Requires central_system_v201
    """
    # wait for libocpp to go online
    cp = await central_system.wait_for_chargepoint()
    yield cp
    await cp.stop()


class ProbeModuleCostAndPriceMetervaluesConfigurationAdjustment(EverestConfigAdjustmentStrategy):
    """
    Probe module to be able to 'inject' metervalues
    """
    def __init__(self, evse_manager_ids: List[str]):
        self.evse_manager_ids = evse_manager_ids

    def adjust_everest_configuration(self, everest_config: Dict):
        adjusted_config = deepcopy(everest_config)

        adjusted_config["active_modules"]["grid_connection_point"]["connections"]["powermeter"] = [
            {"module_id": "probe", "implementation_id": "ProbeModulePowerMeter"}]

        for evse_manager_id in self.evse_manager_ids:
            adjusted_config["active_modules"][evse_manager_id]["connections"]["powermeter_grid_side"] = [
                {"module_id": "probe", "implementation_id": "ProbeModulePowerMeter"}]

        return adjusted_config


class ProbeModuleCostAndPriceDisplayMessageConfigurationAdjustment(EverestConfigAdjustmentStrategy):
    """
    Probe module to be able to mock display messages
    """

    def adjust_everest_configuration(self, everest_config: Dict):
        adjusted_config = deepcopy(everest_config)

        adjusted_config["active_modules"]["ocpp"]["connections"]["display_message"] = [
            {"module_id": "probe", "implementation_id": "ProbeModuleDisplayMessage"}]

        return adjusted_config


class ProbeModuleCostAndPriceSessionCostConfigurationAdjustment(EverestConfigAdjustmentStrategy):
    """
    Probe module to be able to mock the session cost interface calls
    """

    def adjust_everest_configuration(self, everest_config: Dict):
        adjusted_config = deepcopy(everest_config)

        adjusted_config["active_modules"]["probe"]["connections"]["session_cost"] = [
            {"module_id": "ocpp", "implementation_id": "session_cost"}]

        return adjusted_config


def implement_ocpp16_probe_commands(
    probe_module: ProbeModule, skip_implementation=None, overwrite_implementation=None
) -> Dict:
    """Implement the probe-module commands the everest-config-ocpp16-probe-module.yaml wiring expects.

    That config satisfies the OCPP module's requirements (evse_manager/security/auth/reservation/
    system) with probe implementations; every command the module may call during boot/connect needs
    a handler, otherwise the call blocks.

    Every command gets a Mock handler with a canned return value so tests can assert on calls.
    Commands listed in ``skip_implementation`` ({impl_id: [command, ...]}) are left unimplemented
    (the test provides its own handler); return values in ``overwrite_implementation``
    ({impl_id: {command: value}}) replace the default.

    Returns {implementation_id: {command: Mock}}.
    """
    command_mocks: Dict = {}

    def _add(implementation_id, command, value):
        if skip_implementation and command in skip_implementation.get(implementation_id, []):
            logging.info(f"Skipping implementation of {command}")
            return
        if overwrite_implementation and command in overwrite_implementation.get(
            implementation_id, {}
        ):
            logging.info(f"Overwriting implementation of {command}")
            value = overwrite_implementation[implementation_id][command]
        mock = Mock()
        mock.return_value = value
        command_mocks.setdefault(implementation_id, {})[command] = mock
        probe_module.implement_command(
            implementation_id=implementation_id,
            command_name=command,
            handler=mock,
        )

    for idx, evse_manager in enumerate(["evse_manager", "evse_manager_b"]):
        # charge_mode and hlc_capable are required on a Connector; no probe based test wires
        # grid_support, so the values are inert and only their presence matters.
        _add(
            evse_manager,
            "get_evse",
            {"id": idx + 1, "connectors": [{"id": 1, "charge_mode": "AC", "hlc_capable": False}]},
        )
        _add(evse_manager, "enable_disable", True)
        _add(evse_manager, "authorize_response", None)
        _add(evse_manager, "withdraw_authorization", None)
        _add(evse_manager, "reserve", False)
        _add(evse_manager, "cancel_reservation", None)
        _add(evse_manager, "pause_charging", True)
        _add(evse_manager, "resume_charging", True)
        _add(evse_manager, "stop_transaction", True)
        _add(evse_manager, "force_unlock", True)
        _add(evse_manager, "update_allowed_energy_transfer_modes", None)
        _add(evse_manager, "external_ready_to_start_charging", True)
        _add(evse_manager, "set_plug_and_charge_configuration", True)
        _add(evse_manager, "set_der_available", "Accepted")

    _add("security", "get_leaf_expiry_days_count", 42)
    _add("security", "get_v2g_ocsp_request_data", {"ocsp_request_data_list": []})
    _add("security", "get_mo_ocsp_request_data", {"ocsp_request_data_list": []})
    _add("security", "install_ca_certificate", "Accepted")
    _add("security", "delete_certificate", "Accepted")
    _add("security", "update_leaf_certificate", "Accepted")
    _add("security", "verify_certificate", "Valid")
    _add(
        "security",
        "get_installed_certificates",
        {"status": "Accepted", "certificate_hash_data_chain": []},
    )
    _add("security", "update_ocsp_cache", None)
    _add("security", "is_ca_certificate_installed", False)
    _add("security", "generate_certificate_signing_request", {"status": "Accepted"})
    _add("security", "get_leaf_certificate_info", {"status": "Accepted"})
    _add("security", "get_verify_file", "")
    _add("security", "verify_file_signature", True)
    _add("security", "get_all_valid_certificates_info", {"status": "NotFound", "info": []})
    _add("security", "get_verify_location", "")

    _add("auth", "set_connection_timeout", None)
    _add("auth", "withdraw_authorization", "Accepted")
    _add("auth", "set_master_pass_group_id", None)

    _add("reservation", "cancel_reservation", "Accepted")
    _add("reservation", "reserve_now", False)
    _add("reservation", "exists_reservation", False)

    _add("system", "get_boot_reason", "PowerUp")
    _add("system", "update_firmware", "Accepted")
    _add("system", "allow_firmware_installation", None)
    _add("system", "upload_logs", "Accepted")
    _add("system", "is_reset_allowed", True)
    _add("system", "reset", None)
    _add("system", "set_system_time", True)
    # Called on every connect attempt once the OCPP module delegates network configuration
    # (OCPPmulti); NotSupported preserves the legacy connect-as-before behavior.
    _add("system", "configure_network", {"status": "NotSupported"})

    return command_mocks
