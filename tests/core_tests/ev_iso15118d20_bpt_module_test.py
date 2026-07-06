#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright Pionix GmbH and Contributors to EVerest

import asyncio
import logging
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


# The SECC publishes these literals for any negotiated AC / DC ISO 15118-20 namespace.
D20_AC_PROTOCOL = "ISO15118-20:AC and similar"
D20_DC_PROTOCOL = "ISO15118-20:DC"

# EvIso15118D20 logs these only after the SECC returns BPT limits in the CPD
# exchange, i.e. only when a BPT service was negotiated. Grepping the manager log
# capture for them distinguishes a BPT session from a plain (unidirectional) one.
AC_BPT_LIMITS_LOG = "AC BPT EVSE limits"
DC_BPT_LIMITS_LOG = "DC BPT EVSE limits"


class EvAutoExecAdjustmentStrategy(EverestConfigAdjustmentStrategy):
    """Drive the EvManager through a complete ISO 15118-20 BPT session via auto_exec."""

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
@pytest.mark.everest_core_config("config-sil-ac-bpt-d20-evcpp.yaml")
@pytest.mark.everest_config_adaptions(
    *_ev_config_adaptions(
        # sleep 1: let the modules reach steady state before SLAC matching starts.
        # The rest of the chain is event-gated (iso_wait_*), so no further sleeps.
        "sleep 1;iso_wait_slac_matched;iso_start_v2g_session AC_BPT;"
        "iso_wait_pwr_ready;iso_draw_power_regulated 16,3;iso_wait_for_stop 10;"
        "iso_wait_v2g_session_stopped;unplug"
    )
)
async def test_ev_iso15118d20_ac_bpt_session(
    test_controller: TestController, everest_core: EverestCore, caplog
):
    """SIL gate: EvIso15118D20 negotiates an AC ISO 15118-20 BPT session.

    The gate proves BPT NEGOTIATION happened, not reverse power flow: the EV
    selects the AC_BPT service and the SECC returns BPT AC limits in the CPD
    exchange (logged by the EV module). The SIL charge loop still moves power
    in the charge direction; only the negotiated service is bidirectional.
    """
    caplog.set_level(logging.DEBUG)
    test_controller.start()
    probe_module = ProbeModule(everest_core.get_runtime_session())

    selected_protocol_mock = Mock()
    power_ready_mock = Mock()
    ac_target_power_mock = Mock()
    finished_mock = Mock()

    probe_module.subscribe_variable("charger", "selected_protocol", selected_protocol_mock)
    probe_module.subscribe_variable("ev", "ev_power_ready", power_ready_mock)
    probe_module.subscribe_variable("ev", "ac_evse_target_power", ac_target_power_mock)
    probe_module.subscribe_variable("ev", "v2g_session_finished", finished_mock)

    probe_module.start()
    await probe_module.wait_to_be_ready()

    await wait_for_call(selected_protocol_mock, timeout=30)
    selected_protocol = selected_protocol_mock.call_args[0][0]
    assert selected_protocol == D20_AC_PROTOCOL, (
        f"selected_protocol '{selected_protocol}' != expected "
        f"ISO 15118-20 AC protocol literal '{D20_AC_PROTOCOL}'"
    )

    await wait_for_call(power_ready_mock, timeout=30)
    assert power_ready_mock.call_args[0][0] is True, "ev_power_ready published without a true value"

    await wait_for_call(ac_target_power_mock, timeout=30)

    await wait_for_call(finished_mock, timeout=40)

    assert AC_BPT_LIMITS_LOG in caplog.text, (
        f"'{AC_BPT_LIMITS_LOG}' not found in the manager log capture; the AC BPT "
        "CPD exchange did not happen, so BPT was not negotiated"
    )


@pytest.mark.asyncio
@pytest.mark.xdist_group(name="ISO15118")
@pytest.mark.probe_module(
    connections={
        "charger": [Requirement("iso15118_charger", "charger")],
        "ev": [Requirement("iso15118_car", "ev")],
    }
)
@pytest.mark.everest_core_config("config-sil-dc-bpt-d20-evcpp.yaml")
@pytest.mark.everest_config_adaptions(
    *_ev_config_adaptions(
        # sleep 1: let the modules reach steady state before SLAC matching starts.
        # The rest of the chain is event-gated (iso_wait_*), so no further sleeps.
        "sleep 1;iso_wait_slac_matched;iso_start_v2g_session DC_BPT;"
        "iso_wait_pwr_ready;iso_dc_power_on;iso_wait_for_stop 10;"
        "iso_wait_v2g_session_stopped;unplug"
    )
)
async def test_ev_iso15118d20_dc_bpt_session(
    test_controller: TestController, everest_core: EverestCore, caplog
):
    """SIL gate: EvIso15118D20 negotiates a DC ISO 15118-20 BPT session.

    The gate proves BPT NEGOTIATION happened, not reverse power flow: the EV
    selects the DC_BPT service and the SECC returns BPT DC limits in the CPD
    exchange (logged by the EV module). iso_dc_power_on blocks the auto_exec
    chain until dc_power_on is published, so the chain only advances once the
    DC charge loop has run on both sides. Power still flows in the charge
    direction; only the negotiated service is bidirectional.
    """
    caplog.set_level(logging.DEBUG)
    test_controller.start()
    probe_module = ProbeModule(everest_core.get_runtime_session())

    selected_protocol_mock = Mock()
    power_ready_mock = Mock()
    dc_power_on_mock = Mock()
    finished_mock = Mock()

    probe_module.subscribe_variable("charger", "selected_protocol", selected_protocol_mock)
    probe_module.subscribe_variable("ev", "ev_power_ready", power_ready_mock)
    probe_module.subscribe_variable("ev", "dc_power_on", dc_power_on_mock)
    probe_module.subscribe_variable("ev", "v2g_session_finished", finished_mock)

    probe_module.start()
    await probe_module.wait_to_be_ready()

    await wait_for_call(selected_protocol_mock, timeout=30)
    selected_protocol = selected_protocol_mock.call_args[0][0]
    assert selected_protocol == D20_DC_PROTOCOL, (
        f"selected_protocol '{selected_protocol}' != expected "
        f"ISO 15118-20 DC protocol literal '{D20_DC_PROTOCOL}'"
    )

    await wait_for_call(power_ready_mock, timeout=30)
    assert power_ready_mock.call_args[0][0] is True, "ev_power_ready published without a true value"

    await wait_for_call(dc_power_on_mock, timeout=30)

    await wait_for_call(finished_mock, timeout=40)

    assert DC_BPT_LIMITS_LOG in caplog.text, (
        f"'{DC_BPT_LIMITS_LOG}' not found in the manager log capture; the DC BPT "
        "CPD exchange did not happen, so BPT was not negotiated"
    )
