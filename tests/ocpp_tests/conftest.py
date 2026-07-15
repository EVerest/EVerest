# SPDX-License-Identifier: Apache-2.0
# Copyright Pionix GmbH and Contributors to EVerest

# noinspection PyUnresolvedReferences
from everest.testing.core_utils.fixtures import *
from everest.testing.core_utils.probe_module import ProbeModule
from everest.testing.ocpp_utils.charge_point_utils import OcppTestConfiguration

# pylint: disable-next=unused-import
from everest.testing.ocpp_utils.fixtures import (
    ocpp_config,
    ocpp_version,
    charge_point,
    charge_point_v201,
    charge_point_v21,
    central_system,
    central_system_v16,
    central_system_v201,
    central_system_v21,
    central_system_v16_standalone,
    test_utility,
)

import test_sets.everest_test_utils as everest_test_utils

from everest.testing.core_utils.network_isolation import (
    NetworkIsolationStrategy,
    WORKER_INTERFACE_ENV,
    WORKER_PROXY_INTERFACE_ENV,
)
from everest.testing.core_utils._configuration.everest_configuration_strategies.everest_configuration_strategy import (
    EverestConfigAdjustmentStrategy,
)

from typing import Any, Callable

import os

import logging

import pytest



def pytest_addoption(parser):
    # Guard against duplicate registration when tests/conftest.py is also loaded
    # (e.g. when invoked via run-tests.sh with --config-file pointing to tests/pytest.ini).
    try:
        parser.addoption(
            "--everest-prefix",
            action="store",
            default="../../build/dist",
            help="everest prefix path; default = '../../build/dist'",
        )
    except ValueError:
        logging.error("Option --everest-prefix already registered, skipping duplicate registration.")

    try:
        parser.addoption(
            "--ocpp-impl",
            action="store",
            default="both",
            choices=["both", "legacy", "multi"],
            help="Which OCPP module implementation(s) to test: 'both' (default), 'legacy' (OCPP/OCPP201), or 'multi' (OCPPmulti).",
        )
    except ValueError:
        logging.error("Option --ocpp-impl already registered, skipping duplicate registration.")


def pytest_generate_tests(metafunc):
    if "ocpp_impl" in metafunc.fixturenames:
        # Tests marked ocpp_multi_only exercise functionality that only exists in
        # the combined OCPPmulti module (e.g. DER); pin them to multi regardless
        # of --ocpp-impl so the legacy OCPP/OCPP201 variant is never collected.
        if metafunc.definition.get_closest_marker("ocpp_multi_only"):
            metafunc.parametrize("ocpp_impl", ["multi"])
            return
        selected = metafunc.config.getoption("--ocpp-impl")
        impls = ["legacy", "multi"] if selected == "both" else [selected]
        metafunc.parametrize("ocpp_impl", impls)


def pytest_sessionfinish(session, exitstatus):
    pass


@pytest.fixture
def ocpp_impl():
    # Overridden by pytest_generate_tests parametrization for every EVerest-booting test.
    return "legacy"


@pytest.fixture
def everest_config_strategies(request, ocpp_impl, ocpp_version) -> list:
    strategies = []
    marker = request.node.get_closest_marker("everest_config_adaptions")
    if marker:
        for v in marker.args:
            assert isinstance(v, EverestConfigAdjustmentStrategy), \
                "Arguments to 'everest_config_adaptions' must all be instances of EverestConfigAdjustmentStrategy"
            strategies.append(v)

    interface = os.environ.get(WORKER_INTERFACE_ENV)
    if interface:
        proxy_interface = os.environ.get(WORKER_PROXY_INTERFACE_ENV)
        strategies.append(NetworkIsolationStrategy(interface, proxy_interface))

    if ocpp_impl == "multi":
        strategies.append(everest_test_utils.OCPPMultiConfigurationStrategy(ocpp_version=ocpp_version))

    return strategies


@pytest.fixture(scope="session")
def exi_generator():
    certs_path = str(Path(__file__).parent / "test_sets" / "everest-aux" / "certs")
    return everest_test_utils.EXIGenerator(certs_path)


@pytest.fixture
def test_config() -> OcppTestConfiguration:
    return everest_test_utils.load_test_config()


@pytest.fixture
def core_config(request) -> EverestEnvironmentCoreConfiguration:
    everest_prefix = Path(request.config.getoption("--everest-prefix"))

    marker = request.node.get_closest_marker("everest_core_config")

    if marker is None:
        test_function_name = request.function.__name__
        test_module_name = request.module.__name__
        everest_config_path = everest_test_utils.get_everest_config(
            test_function_name, test_module_name
        )
    else:
        everest_config_path = (
            Path(__file__).parent /
            "test_sets/everest-aux/config" / marker.args[0]
        )

    return EverestEnvironmentCoreConfiguration(
        everest_core_path=everest_prefix,
        template_everest_config_path=everest_config_path,
    )


@pytest.fixture
def started_test_controller(test_controller):
    test_controller.start()
    yield test_controller
    test_controller.stop()


@pytest.fixture
def skip_implementation():
    return None


@pytest.fixture
def overwrite_implementation():
    return None


def implement_command(
    module: ProbeModule,
    skip_implementation: dict,
    implementation_id: str,
    command_name: str,
    handler: Callable[[dict], Any],
):
    skip = False
    if skip_implementation:
        if implementation_id in skip_implementation:
            to_skip = skip_implementation[implementation_id]
            if command_name in to_skip:
                logging.info(f"Skipping implementation of {command_name}")
                skip = True
    if not skip:
        module.implement_command(implementation_id, command_name, handler)


@pytest.fixture
def probe_module(
    started_test_controller, everest_core, skip_implementation
) -> ProbeModule:
    # initiate the probe module, connecting to the same runtime session the test controller started
    module = ProbeModule(everest_core.get_runtime_session())

    logging.info(f"hello: {skip_implementation}")

    # implement necessary commands for initialization in the module
    implement_command(
        module,
        skip_implementation,
        "ProbeModuleConnectorA",
        "get_evse",
        lambda arg: {"id": 1, "connectors": [{"id": 1}]},
    )
    implement_command(
        module,
        skip_implementation,
        "ProbeModuleConnectorA",
        "enable_disable",
        lambda arg: True,
    )
    implement_command(
        module,
        skip_implementation,
        "ProbeModuleConnectorA",
        "authorize_response",
        lambda arg: None,
    )
    implement_command(
        module,
        skip_implementation,
        "ProbeModuleConnectorA",
        "update_allowed_energy_transfer_modes",
        lambda arg: None,
    )
    implement_command(
        module,
        skip_implementation,
        "ProbeModuleConnectorA",
        "withdraw_authorization",
        lambda arg: None,
    )
    implement_command(
        module,
        skip_implementation,
        "ProbeModuleConnectorA",
        "reserve",
        lambda arg: False,
    )
    implement_command(
        module,
        skip_implementation,
        "ProbeModuleConnectorA",
        "cancel_reservation",
        lambda arg: None,
    )
    implement_command(
        module,
        skip_implementation,
        "ProbeModuleConnectorA",
        "pause_charging",
        lambda arg: True,
    )
    implement_command(
        module,
        skip_implementation,
        "ProbeModuleConnectorA",
        "resume_charging",
        lambda arg: True,
    )
    implement_command(
        module,
        skip_implementation,
        "ProbeModuleConnectorA",
        "stop_transaction",
        lambda arg: True,
    )
    implement_command(
        module,
        skip_implementation,
        "ProbeModuleConnectorA",
        "force_unlock",
        lambda arg: True,
    )
    implement_command(
        module,
        skip_implementation,
        "ProbeModuleIso15118Extensions",
        "set_get_certificate_response",
        lambda arg: None,
    )
    implement_command(
        module,
        skip_implementation,
        "ProbeModuleConnectorA",
        "external_ready_to_start_charging",
        lambda arg: True,
    )
    implement_command(
        module,
        skip_implementation,
        "ProbeModuleConnectorA",
        "set_plug_and_charge_configuration",
        lambda arg: True,
    )
    implement_command(
        module,
        skip_implementation,
        "ProbeModuleConnectorB",
        "get_evse",
        lambda arg: {"id": 2, "connectors": [{"id": 1}]},
    )
    implement_command(
        module,
        skip_implementation,
        "ProbeModuleConnectorB",
        "enable_disable",
        lambda arg: True,
    )
    implement_command(
        module,
        skip_implementation,
        "ProbeModuleConnectorB",
        "authorize_response",
        lambda arg: None,
    )
    implement_command(
        module,
        skip_implementation,
        "ProbeModuleConnectorB",
        "update_allowed_energy_transfer_modes",
        lambda arg: None,
    )
    implement_command(
        module,
        skip_implementation,
        "ProbeModuleConnectorB",
        "withdraw_authorization",
        lambda arg: None,
    )
    implement_command(
        module,
        skip_implementation,
        "ProbeModuleConnectorB",
        "reserve",
        lambda arg: False,
    )
    implement_command(
        module,
        skip_implementation,
        "ProbeModuleConnectorB",
        "cancel_reservation",
        lambda arg: None,
    )
    implement_command(
        module,
        skip_implementation,
        "ProbeModuleConnectorB",
        "pause_charging",
        lambda arg: True,
    )
    implement_command(
        module,
        skip_implementation,
        "ProbeModuleConnectorB",
        "resume_charging",
        lambda arg: True,
    )
    implement_command(
        module,
        skip_implementation,
        "ProbeModuleConnectorB",
        "stop_transaction",
        lambda arg: True,
    )
    implement_command(
        module,
        skip_implementation,
        "ProbeModuleConnectorB",
        "force_unlock",
        lambda arg: True,
    )
    implement_command(
        module,
        skip_implementation,
        "ProbeModuleConnectorB",
        "external_ready_to_start_charging",
        lambda arg: True,
    )
    implement_command(
        module,
        skip_implementation,
        "ProbeModuleConnectorB",
        "set_plug_and_charge_configuration",
        lambda arg: True,
    )
    implement_command(
        module,
        skip_implementation,
        "ProbeModuleSystem",
        "get_boot_reason",
        lambda arg: "PowerUp",
    )
    implement_command(
        module,
        skip_implementation,
        "ProbeModuleSystem",
        "update_firmware",
        lambda arg: "Accepted",
    )
    implement_command(
        module,
        skip_implementation,
        "ProbeModuleSystem",
        "allow_firmware_installation",
        lambda arg: None,
    )
    implement_command(
        module,
        skip_implementation,
        "ProbeModuleSystem",
        "upload_logs",
        lambda arg: "Accepted",
    )
    implement_command(
        module,
        skip_implementation,
        "ProbeModuleSystem",
        "is_reset_allowed",
        lambda arg: True,
    )
    implement_command(
        module, skip_implementation, "ProbeModuleSystem", "reset", lambda arg: None
    )
    implement_command(
        module,
        skip_implementation,
        "ProbeModuleSystem",
        "set_system_time",
        lambda arg: True,
    )
    implement_command(
        module,
        skip_implementation,
        "ProbeModuleSecurity",
        "get_leaf_expiry_days_count",
        lambda arg: 42,
    )
    implement_command(
        module,
        skip_implementation,
        "ProbeModuleSecurity",
        "get_v2g_ocsp_request_data",
        lambda arg: {"ocsp_request_data_list": []},
    )
    implement_command(
        module,
        skip_implementation,
        "ProbeModuleSecurity",
        "get_mo_ocsp_request_data",
        lambda arg: {"ocsp_request_data_list": []},
    )
    implement_command(
        module,
        skip_implementation,
        "ProbeModuleSecurity",
        "install_ca_certificate",
        lambda arg: "Accepted",
    )
    implement_command(
        module,
        skip_implementation,
        "ProbeModuleSecurity",
        "delete_certificate",
        lambda arg: "Accepted",
    )
    implement_command(
        module,
        skip_implementation,
        "ProbeModuleSecurity",
        "update_leaf_certificate",
        lambda arg: "Accepted",
    )
    implement_command(
        module,
        skip_implementation,
        "ProbeModuleSecurity",
        "verify_certificate",
        lambda arg: "Valid",
    )
    implement_command(
        module,
        skip_implementation,
        "ProbeModuleSecurity",
        "get_installed_certificates",
        lambda arg: {"status": "Accepted", "certificate_hash_data_chain": []},
    )
    implement_command(
        module,
        skip_implementation,
        "ProbeModuleSecurity",
        "update_ocsp_cache",
        lambda arg: None,
    )
    implement_command(
        module,
        skip_implementation,
        "ProbeModuleSecurity",
        "is_ca_certificate_installed",
        lambda arg: False,
    )
    implement_command(
        module,
        skip_implementation,
        "ProbeModuleSecurity",
        "generate_certificate_signing_request",
        lambda arg: {"status": "Accepted"},
    )
    implement_command(
        module,
        skip_implementation,
        "ProbeModuleSecurity",
        "get_leaf_certificate_info",
        lambda arg: {"status": "Accepted"},
    )
    implement_command(
        module,
        skip_implementation,
        "ProbeModuleSecurity",
        "get_verify_file",
        lambda arg: "",
    )
    implement_command(
        module,
        skip_implementation,
        "ProbeModuleSecurity",
        "get_verify_location",
        lambda arg: "",
    )
    implement_command(
        module,
        skip_implementation,
        "ProbeModuleSecurity",
        "verify_file_signature",
        lambda arg: True,
    )
    implement_command(
        module,
        skip_implementation,
        "ProbeModuleSecurity",
        "get_all_valid_certificates_info",
        lambda arg: {"status": "NotFound", "info": []},
    )

    return module


@pytest.fixture()
def ocpp_config_reader(ocpp_config, ocpp_configuration):
    """
    Returns a reader over the final OCPP config (after all adaptations during test setup) for convenience.
    """
    return everest_test_utils.OCPPConfigReader(ocpp_configuration)
