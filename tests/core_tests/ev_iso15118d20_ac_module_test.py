#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright Pionix GmbH and Contributors to EVerest

import asyncio
import os
from copy import deepcopy
from typing import Dict, List
from unittest.mock import Mock

import pytest

from everest.testing.core_utils.common import Requirement
from everest.testing.core_utils.fixtures import *
from everest.testing.core_utils.controller.test_controller_interface import (
    TestController,
)
from everest.testing.core_utils.everest_core import EverestCore
from everest.testing.core_utils.probe_module import ProbeModule

from smoke_tests import NetworkInterfaceConfigAdjustmentStrategy


# The SECC publishes this literal for any negotiated AC ISO 15118-20 namespace.
D20_AC_PROTOCOL = "ISO15118-20:AC and similar"


class EvAutoExecAdjustmentStrategy(EverestConfigAdjustmentStrategy):
    """Drive the EvManager through a complete AC ISO 15118-20 session via auto_exec."""

    def __init__(self, auto_exec_commands: str):
        self.auto_exec_commands = auto_exec_commands

    def adjust_everest_configuration(self, everest_config: Dict) -> Dict:
        adjusted_config = deepcopy(everest_config)
        ev_manager = adjusted_config["active_modules"]["ev_manager"]["config_module"]
        ev_manager["auto_exec"] = True
        ev_manager["auto_exec_commands"] = self.auto_exec_commands
        return adjusted_config


def _ev_config_adaptions(auto_exec_commands: str) -> List[EverestConfigAdjustmentStrategy]:
    """Auto_exec strategy plus, when EVEREST_V2G_DEVICE is set, a device override.

    CI leaves EVEREST_V2G_DEVICE unset (device stays ``auto``, the network-isolation
    plugin picks the per-worker veth); a developer host sets it to e.g. ``v2g0``.
    """
    adaptions: List[EverestConfigAdjustmentStrategy] = [EvAutoExecAdjustmentStrategy(auto_exec_commands)]
    local_device = os.environ.get("EVEREST_V2G_DEVICE")
    if local_device:
        adaptions.append(NetworkInterfaceConfigAdjustmentStrategy(local_device))
    return adaptions


async def wait_for_call(mock: Mock, timeout: float = 30.0):
    """Wait until mock has been called at least once. Raises TimeoutError otherwise."""
    start_time = asyncio.get_event_loop().time()
    while asyncio.get_event_loop().time() - start_time < timeout:
        if mock.call_count > 0:
            return
        await asyncio.sleep(0.1)
    raise TimeoutError("Timeout waiting for variable publication.")


@pytest.mark.asyncio
@pytest.mark.xdist_group(name="ISO15118")
@pytest.mark.probe_module(
    connections={
        "charger": [Requirement("iso15118_charger", "charger")],
        "ev": [Requirement("iso15118_car", "ev")],
    }
)
@pytest.mark.everest_core_config("config-sil-ac-d20-evcpp.yaml")
@pytest.mark.everest_config_adaptions(
    *_ev_config_adaptions(
        # sleep 1: let the modules reach steady state before SLAC matching starts.
        "sleep 1;iso_wait_slac_matched;iso_start_v2g_session AC;"
        "iso_wait_pwr_ready;iso_wait_for_stop 5;iso_wait_v2g_session_stopped;unplug"
    )
)
async def test_ev_iso15118d20_ac_session(
    test_controller: TestController, everest_core: EverestCore
):
    """SIL gate: the C++ EvIso15118D20 module negotiates an AC ISO 15118-20 session.

    The two-connection probe observes the SECC view of protocol selection.
    """
    test_controller.start()
    probe_module = ProbeModule(everest_core.get_runtime_session())

    selected_protocol_mock = Mock()

    probe_module.subscribe_variable("charger", "selected_protocol", selected_protocol_mock)

    probe_module.start()
    await probe_module.wait_to_be_ready()

    # The EvManager auto_exec sequence drives SLAC matching and the V2G session.

    await wait_for_call(selected_protocol_mock, timeout=30)
    selected_protocol = selected_protocol_mock.call_args[0][0]
    assert selected_protocol == D20_AC_PROTOCOL, (
        f"selected_protocol '{selected_protocol}' != expected "
        f"ISO 15118-20 AC protocol literal '{D20_AC_PROTOCOL}'"
    )


@pytest.mark.asyncio
@pytest.mark.xdist_group(name="ISO15118")
@pytest.mark.probe_module(
    connections={
        "charger": [Requirement("iso15118_charger", "charger")],
        "ev": [Requirement("iso15118_car", "ev")],
    }
)
@pytest.mark.everest_core_config("config-sil-ac-d20-evcpp.yaml")
@pytest.mark.everest_config_adaptions(
    *_ev_config_adaptions(
        # sleep 1: let the modules reach steady state before SLAC matching starts.
        # The rest of the chain is event-gated (iso_wait_*), so no further sleeps.
        "sleep 1;iso_wait_slac_matched;iso_start_v2g_session AC;"
        "iso_wait_pwr_ready;iso_draw_power_regulated 16,3;iso_wait_for_stop 10;"
        "iso_wait_v2g_session_stopped;unplug"
    )
)
async def test_ev_iso15118d20_full_ac_charge_loop(
    test_controller: TestController, everest_core: EverestCore
):
    """SIL gate: EvIso15118D20 completes a full AC ISO 15118-20 charge loop.

    The EvManager auto_exec walks the whole AC flow to a clean SessionStop. The
    EV-side publishes, in order, drive the charge loop:
      - ev_power_ready (the EVCC is ready for power)
      - ac_evse_target_power (the EVSE target power in the charge loop)
      - v2g_session_finished (clean end)
    """
    test_controller.start()
    probe_module = ProbeModule(everest_core.get_runtime_session())

    power_ready_mock = Mock()
    ac_target_power_mock = Mock()
    finished_mock = Mock()

    probe_module.subscribe_variable("ev", "ev_power_ready", power_ready_mock)
    probe_module.subscribe_variable("ev", "ac_evse_target_power", ac_target_power_mock)
    probe_module.subscribe_variable("ev", "v2g_session_finished", finished_mock)

    probe_module.start()
    await probe_module.wait_to_be_ready()

    await wait_for_call(power_ready_mock, timeout=30)
    assert power_ready_mock.call_args[0][0] is True, "ev_power_ready published without a true value"

    await wait_for_call(ac_target_power_mock, timeout=30)

    await wait_for_call(finished_mock, timeout=40)
