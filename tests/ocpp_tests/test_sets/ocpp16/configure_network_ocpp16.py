# SPDX-License-Identifier: Apache-2.0
# Copyright Pionix GmbH and Contributors to EVerest

"""Integration tests for the OCPP1.6 -> system provider `configure_network` delegation.

Covers the NotSupported / Ready direct answers and the deferred Processing variant
(completed via the configure_network_status var). Only the combined OCPPmulti module
implements the delegation, so the tests are pinned via ocpp_multi_only; the config
rewrite swaps the legacy OCPP module in the probe config for OCPPmulti (Mode Only1.6).

In OCPP1.6 libocpp synthesizes a single network connection profile from
CentralSystemURI/SecurityProfile with configuration slot 1 and ocppInterface "Any"
(vs the explicit "Wired0" profile the 2.x tests observe). request_id is a
module-generated opaque id.
"""

import asyncio
import logging
import threading

import pytest

from everest.testing.core_utils.probe_module import ProbeModule
from everest.testing.core_utils._configuration.libocpp_configuration_helper import (
    GenericOCPP2XConfigAdjustment,
    OCPP2XConfigVariableIdentifier,
)
from everest.testing.ocpp_utils.central_system import CentralSystem

from everest_test_utils_probe_modules import implement_ocpp16_probe_commands

log = logging.getLogger("OCPPmultiConfigureNetwork16")

# The v16 profile is synthesized by libocpp with ocppInterface "Any" (not used for OCPP1.6).
EXPECTED_INTERFACE = "Any"


@pytest.fixture
def probe_module(started_test_controller, everest_core, skip_implementation) -> ProbeModule:
    """Local probe fixture for the ocpp16 probe config (implementation ids `system`,
    `evse_manager`/`evse_manager_b`, ... - different from the 2.x `ProbeModule*` ids the
    shared conftest fixture implements)."""
    module = ProbeModule(everest_core.get_runtime_session())
    implement_ocpp16_probe_commands(module, skip_implementation)
    return module


async def _connect(probe_module):
    """Standard probe-module bring-up: start, ready, publish EVSE manager readiness."""
    probe_module.start()
    await probe_module.wait_to_be_ready()
    probe_module.publish_variable("evse_manager", "ready", True)
    probe_module.publish_variable("evse_manager_b", "ready", True)


@pytest.mark.asyncio
@pytest.mark.ocpp_version("ocpp1.6")
@pytest.mark.ocpp_multi_only
@pytest.mark.everest_core_config("everest-config-ocpp16-probe-module.yaml")
@pytest.mark.probe_module
class TestConfigureNetwork16:

    async def test_configure_network_not_supported_fallback(
        self, probe_module, central_system: CentralSystem
    ):
        """Case 1: NotSupported (helper default) -> connects as before."""
        await _connect(probe_module)
        chargepoint = await central_system.wait_for_chargepoint()
        assert chargepoint is not None

    @pytest.mark.parametrize(
        "skip_implementation",
        [{"system": ["configure_network"]}],
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

        probe_module.implement_command("system", "configure_network", handler)

        await _connect(probe_module)
        chargepoint = await central_system.wait_for_chargepoint()
        assert chargepoint is not None

        with capture_lock:
            assert len(captured) >= 1, "configure_network was never called"
            request = captured[0]

        # request_id is an opaque module-generated id; the v16 profile carries interface "Any".
        assert isinstance(request["request_id"], int)
        assert request["request_id"] > 0
        assert request["interface"] == EXPECTED_INTERFACE

    @pytest.mark.parametrize(
        "skip_implementation",
        [{"system": ["configure_network"]}],
    )
    @pytest.mark.ocpp16_component_config_adaptions(
        GenericOCPP2XConfigAdjustment(
            [
                # Slot 1: only the interface is set explicitly (survives the migration `Any` pin);
                # URL/SecurityProfile/Identity are supplied by the legacy JSON migration.
                (OCPP2XConfigVariableIdentifier("NetworkConfiguration_1", "OcppInterface"), "Wireless0"),
                # Slot 2: fully device-model configured. The placeholder OcppCsmsUrl is rewritten to
                # the mock CSMS by the everest-testing default component-config strategy.
                (OCPP2XConfigVariableIdentifier("NetworkConfiguration_2", "OcppCsmsUrl"), "ws://replaced.invalid/cp"),
                (OCPP2XConfigVariableIdentifier("NetworkConfiguration_2", "SecurityProfile"), 0),
                (OCPP2XConfigVariableIdentifier("NetworkConfiguration_2", "OcppTransport"), "JSON"),
                (OCPP2XConfigVariableIdentifier("NetworkConfiguration_2", "MessageTimeout"), 30),
                (OCPP2XConfigVariableIdentifier("NetworkConfiguration_2", "OcppInterface"), "Wired0"),
                # Try slot 1 (wireless) first, then fail over to slot 2 (wired).
                (OCPP2XConfigVariableIdentifier("OCPPCommCtrlr", "NetworkConfigurationPriority"), "1,2"),
            ]
        )
    )
    async def test_configure_network_failover_two_slots(
        self, skip_implementation, probe_module, central_system: CentralSystem
    ):
        """Multi-slot failover: slot 1 (Wireless0) configure_network fails, the ConnectivityManager
        hops to slot 2 (Wired0) after ~2s and connects there."""
        interfaces = []
        capture_lock = threading.Lock()

        def handler(arg):
            # Runs in the ProbeModule worker thread.
            with capture_lock:
                interfaces.append(arg["request"]["interface"])
                attempt = len(interfaces)
            # First attempt (slot 1 / Wireless0) fails -> CM hops to the next priority slot;
            # every subsequent attempt (slot 2 / Wired0) is Ready.
            return {"status": "Failed"} if attempt == 1 else {"status": "Ready"}

        probe_module.implement_command("system", "configure_network", handler)

        await _connect(probe_module)
        chargepoint = await central_system.wait_for_chargepoint()
        assert chargepoint is not None

        with capture_lock:
            assert len(interfaces) >= 2, f"expected failover across slots, got {interfaces}"
            assert interfaces[0] == "Wireless0"
            assert "Wired0" in interfaces[1:]

    @pytest.mark.parametrize(
        "skip_implementation",
        [{"system": ["configure_network"]}],
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

        probe_module.implement_command("system", "configure_network", handler)

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
            "system",
            "configure_network_status",
            {"request_id": request_id, "status": "Ready"},
        )

        chargepoint = await central_system.wait_for_chargepoint()
        assert chargepoint is not None
