# SPDX-License-Identifier: Apache-2.0
# Copyright Pionix GmbH and Contributors to EVerest

"""Integration tests for the generic `ocpp` interface with OCPP2.x.

OCPP2.x analog of the OCPP1.6 ocpp_generic_interface_integration_tests. Currently only covers the `connection_status` var.
"""

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
