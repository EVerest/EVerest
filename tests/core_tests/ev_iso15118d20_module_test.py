#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright Pionix GmbH and Contributors to EVerest

import asyncio
from copy import deepcopy
from typing import Dict
from unittest.mock import Mock

import pytest

from everest.testing.core_utils.common import Requirement
from everest.testing.core_utils.fixtures import *
from everest.testing.core_utils.controller.test_controller_interface import (
    TestController,
)
from everest.testing.core_utils.everest_core import EverestCore
from everest.testing.core_utils.probe_module import ProbeModule


D20_DC_PROTOCOL = "ISO15118-20:DC"
EXPECTED_EVCC_ID = "AA:BB:CC:DD:EE:01"


class EvAutoExecAdjustmentStrategy(EverestConfigAdjustmentStrategy):
    """Drive the EvManager through a complete DC ISO 15118-20 session via auto_exec."""

    def __init__(self, auto_exec_commands: str):
        self.auto_exec_commands = auto_exec_commands

    def adjust_everest_configuration(self, everest_config: Dict) -> Dict:
        adjusted_config = deepcopy(everest_config)
        ev_manager = adjusted_config["active_modules"]["ev_manager"]["config_module"]
        ev_manager["auto_exec"] = True
        ev_manager["auto_exec_commands"] = self.auto_exec_commands
        return adjusted_config


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
@pytest.mark.everest_core_config("config-sil-dc-d20-evcpp.yaml")
@pytest.mark.everest_config_adaptions(
    EvAutoExecAdjustmentStrategy(
        "sleep 1;iso_wait_slac_matched;iso_start_v2g_session DC;"
        "sleep 3;iso_stop_charging;iso_wait_v2g_session_stopped;unplug"
    )
)
async def test_ev_iso15118d20_dc_session(
    test_controller: TestController, everest_core: EverestCore
):
    """M0 SIL gate: the C++ EvIso15118D20 module completes a DC ISO 15118-20 session.

    Observes both sides of the V2G link via a two-connection probe:
      - charger.evcc_id and charger.selected_protocol (SECC view of the EVCC)
      - ev.v2g_session_finished (EVCC view of session completion)

    With empty Impl stubs this fails because start_charging never connects the EV,
    so the SECC never sees an EVCC and the session never finishes. T4 turns it GREEN.
    """
    test_controller.start()
    probe_module = ProbeModule(everest_core.get_runtime_session())

    evcc_id_mock = Mock()
    selected_protocol_mock = Mock()
    finished_mock = Mock()

    probe_module.subscribe_variable("charger", "evcc_id", evcc_id_mock)
    probe_module.subscribe_variable("charger", "selected_protocol", selected_protocol_mock)
    probe_module.subscribe_variable("ev", "v2g_session_finished", finished_mock)

    probe_module.start()
    await probe_module.wait_to_be_ready()

    # The EvManager auto_exec sequence drives SLAC matching and the V2G session.

    await wait_for_call(selected_protocol_mock, timeout=30)
    selected_protocol = selected_protocol_mock.call_args[0][0]
    assert selected_protocol == D20_DC_PROTOCOL, (
        f"selected_protocol '{selected_protocol}' != expected "
        f"ISO 15118-20 DC protocol literal '{D20_DC_PROTOCOL}'"
    )

    await wait_for_call(finished_mock, timeout=30)

    assert evcc_id_mock.call_count > 0, "evcc_id was never published"
    assert evcc_id_mock.call_args[0][0] == EXPECTED_EVCC_ID, (
        f"evcc_id '{evcc_id_mock.call_args[0][0]}' != expected '{EXPECTED_EVCC_ID}'"
    )
