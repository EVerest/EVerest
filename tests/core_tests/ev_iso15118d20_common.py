#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright Pionix GmbH and Contributors to EVerest

"""Helpers shared by the EvIso15118D20 SIL tests.

Not a conftest.py: none of these are pytest fixtures. ``_ev_config_adaptions`` and
``EvAutoExecAdjustmentStrategy`` are evaluated inside the
``@pytest.mark.everest_config_adaptions`` decorator, i.e. at module import time,
and ``wait_for_call`` is a plain coroutine.
"""

import asyncio
import os
from copy import deepcopy
from typing import Dict, List
from unittest.mock import Mock

from everest.testing.core_utils import EverestConfigAdjustmentStrategy

from smoke_tests import NetworkInterfaceConfigAdjustmentStrategy


class EvAutoExecAdjustmentStrategy(EverestConfigAdjustmentStrategy):
    """Drive the EvManager through a complete ISO 15118-20 session via auto_exec."""

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
