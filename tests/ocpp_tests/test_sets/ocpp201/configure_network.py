# SPDX-License-Identifier: Apache-2.0
# Copyright Pionix GmbH and Contributors to EVerest

"""Integration tests for OCPPmulti -> system provider `configure_network` delegation.

Covers the NotSupported / Ready direct answers and the deferred Processing variant
(completed via the configure_network_status var). The active profile is libocpp's
default (ocppInterface Wired0); request_id is a module-generated opaque id.

Only the combined OCPPmulti module implements the delegation (legacy OCPP201 keeps
the unconditional-success stub), so the tests are pinned via ocpp_multi_only; the
config rewrite swaps the OCPP201 module in the probe config for OCPPmulti.
"""

import asyncio
import logging
import threading

import pytest

from everest.testing.ocpp_utils.central_system import CentralSystem

log = logging.getLogger("OCPPmultiConfigureNetwork")

# Active network-connection-profile of this config (libocpp default single profile):
# ocppInterface = "Wired0". request_id is a module-generated unique id, not the slot.
EXPECTED_INTERFACE = "Wired0"

# Valid InterfaceClass enum values (types/network.yaml InterfaceClass).
_VALID_INTERFACE_CLASSES = {
    "Wired0", "Wired1", "Wired2", "Wired3",
    "Wireless0", "Wireless1", "Wireless2", "Wireless3",
    "Any",
}


async def _connect(probe_module):
    """Standard probe-module bring-up: start, ready, publish connector readiness."""
    probe_module.start()
    await probe_module.wait_to_be_ready()
    probe_module.publish_variable("ProbeModuleConnectorA", "ready", True)
    probe_module.publish_variable("ProbeModuleConnectorB", "ready", True)


@pytest.mark.asyncio
@pytest.mark.ocpp_version("ocpp2.0.1")
@pytest.mark.ocpp_multi_only
@pytest.mark.everest_core_config("everest-config-ocpp201-probe-module.yaml")
@pytest.mark.probe_module
class TestConfigureNetwork:

    async def test_configure_network_not_supported_fallback(
        self, probe_module, central_system: CentralSystem
    ):
        """Case 1: NotSupported (conftest default) -> connects as before."""
        await _connect(probe_module)
        chargepoint = await central_system.wait_for_chargepoint()
        assert chargepoint is not None

    @pytest.mark.parametrize(
        "skip_implementation",
        [{"ProbeModuleSystem": ["configure_network"]}],
    )
    async def test_configure_network_ready_direct(
        self, skip_implementation, probe_module, central_system: CentralSystem
    ):
        """Case 2: Ready direct (no interface_address, so no fake-iface bind)."""
        captured = []
        capture_lock = threading.Lock()

        def handler(arg):
            # Runs in the ProbeModule worker thread.
            with capture_lock:
                captured.append(arg["request"])
            return {"status": "Ready"}

        probe_module.implement_command(
            "ProbeModuleSystem", "configure_network", handler
        )

        await _connect(probe_module)
        chargepoint = await central_system.wait_for_chargepoint()
        assert chargepoint is not None

        with capture_lock:
            assert len(captured) >= 1, "configure_network was never called"
            request = captured[0]

        # request_id is an opaque module-generated id; interface derives from the profile.
        assert isinstance(request["request_id"], int)
        assert request["request_id"] > 0
        assert request["interface"] in _VALID_INTERFACE_CLASSES
        assert request["interface"] == EXPECTED_INTERFACE

    @pytest.mark.parametrize(
        "skip_implementation",
        [{"ProbeModuleSystem": ["configure_network"]}],
    )
    async def test_configure_network_processing_then_status(
        self, skip_implementation, probe_module, central_system: CentralSystem
    ):
        """Case 3: Processing -> connects only after configure_network_status is published."""
        captured_request_ids = []
        capture_lock = threading.Lock()

        def handler(arg):
            with capture_lock:
                captured_request_ids.append(arg["request"]["request_id"])
            return {"status": "Processing"}

        probe_module.implement_command(
            "ProbeModuleSystem", "configure_network", handler
        )

        await _connect(probe_module)

        # bounded poll until configure_network was invoked (avoids a flaky fixed sleep)
        request_id = None
        for _ in range(50):  # up to ~10s
            with capture_lock:
                if captured_request_ids:
                    request_id = captured_request_ids[0]
                    break
            await asyncio.sleep(0.2)
        assert request_id is not None, "configure_network was never called"
        assert isinstance(request_id, int) and request_id > 0

        # while Processing the websocket must not open yet (bootnotification=False:
        # assert on the socket, not on a slow boot, and don't consume the connect event)
        with pytest.raises(asyncio.TimeoutError):
            await central_system.wait_for_chargepoint(
                timeout=5, wait_for_bootnotification=False
            )

        # Publish the deferred outcome; libocpp should now proceed to connect.
        probe_module.publish_variable(
            "ProbeModuleSystem",
            "configure_network_status",
            {"request_id": request_id, "status": "Ready"},
        )

        chargepoint = await central_system.wait_for_chargepoint()
        assert chargepoint is not None
