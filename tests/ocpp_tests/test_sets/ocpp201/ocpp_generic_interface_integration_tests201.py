# SPDX-License-Identifier: Apache-2.0
# Copyright Pionix GmbH and Contributors to EVerest

"""Integration tests for the generic `ocpp` interface with OCPP2.x."""

import asyncio
import logging
from unittest.mock import Mock

import pytest

from everest.testing.core_utils.common import Requirement
from everest.testing.ocpp_utils.central_system import CentralSystem

log = logging.getLogger("ocpp201GenericInterfaceTest")

# Network connection profile of this config (libocpp default single profile).
EXPECTED_INTERFACE = "Wired0"
EXPECTED_TRANSPORT = "JSON"
EXPECTED_OCPP_VERSION = "2.0.1"


async def _connect(probe_module):
    """Standard probe-module bring-up: start, ready, publish connector readiness."""
    probe_module.start()
    await probe_module.wait_to_be_ready()
    probe_module.publish_variable("ProbeModuleConnectorA", "ready", True)
    probe_module.publish_variable("ProbeModuleConnectorB", "ready", True)


async def _wait_for_status(subscription_mock, connected, timeout=20):
    """Waits for a connection_status publication with the given connected flag."""

    def _status():
        return next(
            (
                call.args[0]
                for call in subscription_mock.mock_calls
                if call.args and call.args[0]["connected"] is connected
            ),
            None,
        )

    async def _await_status():
        while _status() is None:
            await asyncio.sleep(0.1)

    await asyncio.wait_for(_await_status(), timeout=timeout)
    return _status()


async def _wait_for_command_state(command_mock, state, timeout=10):
    async def _await_state():
        while not any(
            entry.args and entry.args[0]["cmd_source"]["enable_state"] == state
            for entry in command_mock.mock_calls
        ):
            await asyncio.sleep(0.1)

    await asyncio.wait_for(_await_state(), timeout=timeout)


async def _wait_for_call_count(command_mock, count, timeout=10):
    async def _await_calls():
        while command_mock.call_count < count:
            await asyncio.sleep(0.1)

    await asyncio.wait_for(_await_calls(), timeout=timeout)


def _publish_firmware_status(
    probe_module, status, request_id, disable_connectors=None
):
    update = {"firmware_update_status": status, "request_id": request_id}
    if disable_connectors is not None:
        update["firmware_update_metadata"] = {
            "disable_connectors_during_install": disable_connectors
        }
    probe_module.publish_variable(
        "ProbeModuleSystem", "firmware_update_status", update
    )


def _assert_connection_details(status):
    """Asserts the details OCPP2.x provides for the connection the status refers to."""
    assert status["csms_url"]
    assert isinstance(status["security_profile"], int)
    assert isinstance(status["configuration_slot"], int)
    assert status["ocpp_interface"] == EXPECTED_INTERFACE
    assert status["ocpp_transport"] == EXPECTED_TRANSPORT
    assert status["ocpp_version"] == EXPECTED_OCPP_VERSION
    # reported because the network connection profile of this config carries an identity
    assert status["identity"]


@pytest.mark.asyncio
@pytest.mark.ocpp_version("ocpp2.0.1")
@pytest.mark.everest_core_config("everest-config-ocpp201-probe-module.yaml")
@pytest.mark.probe_module(connections={"ocpp": [Requirement("ocpp", "ocpp_generic")]})
class TestConnectionStatus201:

    async def test_connection_status_on_connect(
        self, probe_module, central_system: CentralSystem
    ):
        """The status published on connect describes the active connection."""
        subscription_mock = Mock()
        await _connect(probe_module)
        probe_module.subscribe_variable(
            "ocpp", "connection_status", subscription_mock)

        chargepoint = await central_system.wait_for_chargepoint()
        assert chargepoint is not None

        status = await _wait_for_status(subscription_mock, connected=True)
        _assert_connection_details(status)

    async def test_connection_status_on_disconnect_and_reconnect(
        self, probe_module, central_system: CentralSystem
    ):
        """Disconnecting reports the details of the connection that was just lost."""
        subscription_mock = Mock()
        await _connect(probe_module)
        probe_module.subscribe_variable(
            "ocpp", "connection_status", subscription_mock)

        await central_system.wait_for_chargepoint()
        connected = await _wait_for_status(subscription_mock, connected=True)

        assert await probe_module.call_command("ocpp", "stop", None)
        disconnected = await _wait_for_status(subscription_mock, connected=False)
        _assert_connection_details(disconnected)
        assert disconnected["csms_url"] == connected["csms_url"]
        assert disconnected["configuration_slot"] == connected["configuration_slot"]

        subscription_mock.reset_mock()
        assert await probe_module.call_command("ocpp", "restart", None)
        reconnected = await _wait_for_status(subscription_mock, connected=True)
        _assert_connection_details(reconnected)


@pytest.mark.asyncio
@pytest.mark.ocpp_version("ocpp2.0.1")
@pytest.mark.everest_core_config("everest-config-ocpp201-probe-module.yaml")
@pytest.mark.probe_module(connections={"ocpp": [Requirement("ocpp", "ocpp_generic")]})
@pytest.mark.parametrize(
    "skip_implementation",
    [
        {
            "ProbeModuleConnectorA": ["enable_disable"],
            "ProbeModuleConnectorB": ["enable_disable"],
            "ProbeModuleSystem": ["allow_firmware_installation"],
        }
    ],
)
class TestFirmwareAvailability201:

    async def test_explicit_true_and_signed_default_phases(
        self, probe_module, central_system: CentralSystem
    ):
        availability_mocks = {
            implementation: Mock(return_value=True)
            for implementation in (
                "ProbeModuleConnectorA",
                "ProbeModuleConnectorB",
            )
        }
        for implementation, command_mock in availability_mocks.items():
            probe_module.implement_command(
                implementation, "enable_disable", command_mock
            )
        installation_mock = Mock(return_value=None)
        probe_module.implement_command(
            "ProbeModuleSystem",
            "allow_firmware_installation",
            installation_mock,
        )

        await _connect(probe_module)
        assert await central_system.wait_for_chargepoint() is not None
        for command_mock in availability_mocks.values():
            command_mock.reset_mock()
        installation_mock.reset_mock()

        _publish_firmware_status(
            probe_module,
            "InstallScheduled",
            request_id=81,
            disable_connectors=True,
        )
        for command_mock in availability_mocks.values():
            await _wait_for_command_state(command_mock, "Disable")
        await _wait_for_call_count(installation_mock, 1)

        _publish_firmware_status(probe_module, "Installed", request_id=81)
        for command_mock in availability_mocks.values():
            await _wait_for_command_state(command_mock, "Enable")
            command_mock.reset_mock()
        installation_mock.reset_mock()

        _publish_firmware_status(probe_module, "Downloaded", request_id=82)
        await asyncio.sleep(0.5)
        assert installation_mock.call_count == 0
        for command_mock in availability_mocks.values():
            assert command_mock.call_count == 0

        _publish_firmware_status(probe_module, "SignatureVerified", request_id=82)
        for command_mock in availability_mocks.values():
            await _wait_for_command_state(command_mock, "Disable")
        await _wait_for_call_count(installation_mock, 1)
