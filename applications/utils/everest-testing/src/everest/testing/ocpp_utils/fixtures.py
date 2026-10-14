# SPDX-License-Identifier: Apache-2.0
# Copyright Pionix GmbH and Contributors to EVerest
import getpass
import os
import shutil
import socket
import ssl
import sys
import tempfile
from contextlib import AsyncExitStack
from dataclasses import dataclass
from pathlib import Path
from threading import Thread

import pytest
import pytest_asyncio
from pyftpdlib import servers
from pyftpdlib.authorizers import DummyAuthorizer
from pyftpdlib.handlers import FTPHandler

from everest.testing.core_utils.common import OCPPVersion
from everest.testing.core_utils._configuration.everest_environment_setup import \
    EverestEnvironmentOCPPConfiguration, EverestEnvironmentOCPPSecondaryConnection
from everest.testing.core_utils.controller.everest_test_controller import EverestTestController
from everest.testing.ocpp_utils.central_system import CentralSystem, LocalCentralSystem, inject_csms_v201_mock, inject_csms_v16_mock, \
    determine_ssl_context, inject_csms_v21_mock
from everest.testing.ocpp_utils.charge_point_utils import TestUtility, OcppTestConfiguration

sys.path.append(os.path.abspath(os.path.join(os.path.dirname(__file__), ".")))


# A TCP port nothing is expected to be listening on, for tests that want an unreachable
# secondary connection.
_UNREACHABLE_PORT = 1


@pytest.fixture
def ocpp_version(request) -> OCPPVersion:
    ocpp_version = request.node.get_closest_marker("ocpp_version")
    if ocpp_version:
        return OCPPVersion(request.node.get_closest_marker("ocpp_version").args[0])
    else:
        return OCPPVersion("ocpp1.6")


@pytest.fixture
def ocpp_config(request, central_system: CentralSystem, test_config: OcppTestConfiguration, ocpp_version: OCPPVersion,
                secondary_connections):
    ocpp_config_marker = request.node.get_closest_marker("ocpp_config")

    ocpp_configuration_strategies_marker = request.node.get_closest_marker(
        "ocpp_config_adaptions")
    ocpp_configuration_strategies = []
    if ocpp_configuration_strategies_marker:
        for v in ocpp_configuration_strategies_marker.args:
            assert hasattr(v,
                           "adjust_ocpp_configuration"), "Arguments to 'ocpp_config_adaptions' must all provide interface of OCPPConfigAdjustmentStrategy"
            ocpp_configuration_strategies.append(v)

    ocpp16_component_config_strategies_marker = request.node.get_closest_marker(
        "ocpp16_component_config_adaptions")
    ocpp16_component_config_strategies = []
    if ocpp16_component_config_strategies_marker:
        for v in ocpp16_component_config_strategies_marker.args:
            assert hasattr(v,
                           "adjust_ocpp_configuration"), "Arguments to 'ocpp16_component_config_adaptions' must all provide interface of OCPPConfigAdjustmentStrategy"
            ocpp16_component_config_strategies.append(v)

    return EverestEnvironmentOCPPConfiguration(
        central_system_port=central_system.port,
        central_system_host="127.0.0.1",
        ocpp_version=ocpp_version,
        template_ocpp_config=Path(
            ocpp_config_marker.args[0]) if ocpp_config_marker else None,
        device_model_component_config_path=Path(f"{request.config.getoption('--everest-prefix')}/share/everest/modules/OCPP201/component_config"),
        configuration_strategies=ocpp_configuration_strategies,
        ocpp16_component_config_strategies=ocpp16_component_config_strategies,
        secondary_connections=secondary_connections
    )


@pytest_asyncio.fixture
async def central_system(request, ocpp_version: OCPPVersion, test_config):
    """Fixture for CentralSystem. Can be started as TLS or
        plain websocket depending on the request parameter.
    """

    ssl_context = determine_ssl_context(request, test_config)

    central_system_marker = request.node.get_closest_marker(
        'custom_central_system')

    if central_system_marker:
        assert isinstance(central_system_marker.args[0], CentralSystem)
        cs = central_system_marker.args[0]
    else:
        cs = LocalCentralSystem(test_config.charge_point_info.charge_point_id,
                                ocpp_version=ocpp_version)

    if request.node.get_closest_marker('inject_csms_mock'):
        if ocpp_version == OCPPVersion.ocpp201:
            mock = inject_csms_v201_mock(cs)
        elif ocpp_version == OCPPVersion.ocpp16:
            mock = inject_csms_v16_mock(cs)
        else:
            mock = inject_csms_v21_mock(cs)
        cs.mock = mock

    async with cs.start(ssl_context):
        yield cs


@pytest.fixture
def secondary_csms_specs(request):
    """ The `secondary_csms` markers of a test, if any.

    Each marker declares one secondary OCPP connection. Every fixture below yields None or an empty
    list when no marker is present, so a test that knows nothing about secondary connections is
    completely unaffected.

    Usage::

        @pytest.mark.secondary_csms("EMS")
        @pytest.mark.secondary_csms("MAINT", network_configuration_slot=4)
    """
    return [
        {"connection_id": marker.args[0], **marker.kwargs}
        for marker in request.node.iter_markers("secondary_csms")
    ]


@pytest_asyncio.fixture
async def secondary_central_systems(request, secondary_csms_specs, ocpp_version: OCPPVersion, test_config):
    """ A CentralSystem per `secondary_csms` marker, keyed by connection id.

    Each listens on its own ephemeral port, so a test can run as many as it declares.
    """
    if not secondary_csms_specs:
        yield {}
        return

    ssl_context = determine_ssl_context(request, test_config)

    systems = {}
    async with AsyncExitStack() as stack:
        for spec in secondary_csms_specs:
            connection_id = spec["connection_id"]
            # A secondary connection presents its own identity, so the mock has to expect that path
            # rather than the primary charge point id.
            identity = spec.get("identity") or f"{test_config.charge_point_info.charge_point_id}_{connection_id}"
            cs = LocalCentralSystem(identity, ocpp_version=ocpp_version)
            if request.node.get_closest_marker('inject_csms_mock'):
                if ocpp_version == OCPPVersion.ocpp201:
                    cs.mock = inject_csms_v201_mock(cs)
                elif ocpp_version == OCPPVersion.ocpp16:
                    cs.mock = inject_csms_v16_mock(cs)
                else:
                    cs.mock = inject_csms_v21_mock(cs)
            await stack.enter_async_context(cs.start(ssl_context))
            systems[connection_id] = cs
        yield systems


@pytest.fixture
def secondary_connections(secondary_csms_specs, secondary_central_systems, test_config):
    """ The secondary OCPP connections to declare in the device model, one per marker. """
    connections = []
    for spec in secondary_csms_specs:
        connection_id = spec["connection_id"]
        central_system = secondary_central_systems.get(connection_id)
        connections.append(EverestEnvironmentOCPPSecondaryConnection(
            connection_id=connection_id,
            # No CentralSystem means the test deliberately points the connection at a port nothing
            # is listening on, to prove that an unreachable secondary leaves the primary alone.
            central_system_port=central_system.port if central_system is not None
            else spec.get("central_system_port", _UNREACHABLE_PORT),
            identity=central_system.chargepoint_id if central_system is not None else spec.get("identity"),
            network_configuration_slot=spec.get("network_configuration_slot", 3),
            security_profile=spec.get("security_profile", 1),
            enabled=spec.get("enabled", True),
            upstream_msgs=spec.get("upstream_msgs", ""),
            downstream_msgs=spec.get("downstream_msgs", ""),
            allow_set_variables=spec.get("allow_set_variables", "")))
    return connections


@pytest_asyncio.fixture
async def secondary_charge_points(secondary_central_systems, test_controller: EverestTestController):
    """ The ChargePoint of each secondary CentralSystem, keyed by connection id.

    Does not wait for a BootNotification by default: whether a secondary connection sends one is up
    to its configuration, so waiting would hang for a connection that is not allowed to.
    """
    charge_points = {}
    for connection_id, central_system in secondary_central_systems.items():
        charge_points[connection_id] = await central_system.wait_for_chargepoint(
            wait_for_bootnotification=False)
    yield charge_points
    for charge_point in charge_points.values():
        await charge_point.stop()


@pytest_asyncio.fixture
async def charge_point(central_system: CentralSystem, test_controller: EverestTestController):
    """Fixture for ChargePoint16. Requires central_system_v201 and test_controller. Starts test_controller immediately
    """
    test_controller.start()
    cp = await central_system.wait_for_chargepoint()
    yield cp
    await cp.stop()


@pytest.fixture
def test_utility():
    """Fixture for test case meta data
    """
    return TestUtility()


@pytest.fixture
def test_config():
    return OcppTestConfiguration()


class FtpThread(Thread):
    def __init__(self, directory, port, test_config: OcppTestConfiguration, ftp_socket,
                 group=None, target=None, name=None, args=..., kwargs=None, *, daemon=None):
        super().__init__(group, target, name, args, kwargs, daemon=daemon)
        self.directory = directory
        self.port = port
        self.test_config = test_config
        self.ftp_socket = ftp_socket

    def set_directory(self, directory):
        self.directory = directory

    def set_port(self, port):
        self.port = port

    def set_test_config(self, test_config: OcppTestConfiguration):
        self.test_config = test_config

    def set_socket(self, ftp_socket):
        self.ftp_socket = ftp_socket

    def stop(self):
        self.server.close_all()

    def run(self):
        shutil.copyfile(self.test_config.firmware_info.update_file, os.path.join(
            self.directory, "firmware_update.pnx"))
        shutil.copyfile(self.test_config.firmware_info.update_file_signature,
                        os.path.join(self.directory, "firmware_update.pnx.base64"))

        authorizer = DummyAuthorizer()
        authorizer.add_user(getpass.getuser(), "12345",
                            self.directory, perm="elradfmwMT")

        handler = FTPHandler
        handler.authorizer = authorizer

        self.server = servers.FTPServer(self.ftp_socket, handler)

        self.server.serve_forever()


@pytest.fixture
def ftp_server(test_config: OcppTestConfiguration):
    """This fixture creates a temporary directory and starts
    a local ftp server connected to that directory. The temporary
    directory is deleted afterwards
    """

    d = tempfile.mkdtemp(prefix='tmp_ftp')
    address = ("127.0.0.1", 0)
    ftp_socket = socket.socket()
    ftp_socket.bind(address)
    port = ftp_socket.getsockname()[1]

    ftp_thread = FtpThread(directory=d, port=port,
                           test_config=test_config, ftp_socket=ftp_socket)
    ftp_thread.daemon = True
    ftp_thread.start()

    yield ftp_thread

    ftp_thread.stop()

    shutil.rmtree(d)


@pytest_asyncio.fixture
async def central_system_v16(central_system):
    """ Note: This is only for backwards compatibility; use central_system directly! """
    yield central_system


@pytest_asyncio.fixture
async def central_system_v201(central_system):
    """ Note: This is only for backwards compatibility; use central_system directly! """
    yield central_system


@pytest_asyncio.fixture
async def central_system_v21(central_system):
    """ Note: This is only for backwards compatibility; use central_system directly! """
    yield central_system


@pytest_asyncio.fixture
async def charge_point_v16(charge_point):
    """ Note: This is only for backwards compatibility; use charge_point directly! """
    yield charge_point


@pytest_asyncio.fixture
async def charge_point_v201(charge_point):
    """ Note: This is only for backwards compatibility; use charge_point directly! """
    yield charge_point


@pytest_asyncio.fixture
async def charge_point_v21(charge_point):
    """ Note: This is only for backwards compatibility; use charge_point directly! """
    yield charge_point


@pytest_asyncio.fixture
async def central_system_v16_standalone(request, central_system: CentralSystem, test_controller: EverestTestController):
    """ Note: This is only for backwards compatibility; use central_system + test_controller directly!

    Fixture for standalone central system. Requires central_system_v16 and test_controller. Starts test_controller immediately
    """
    test_controller.start()
    yield central_system
    test_controller.stop()
