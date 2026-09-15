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
from typing import Any, Callable, Dict, List
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


def _ev_device_adaptions() -> List[EverestConfigAdjustmentStrategy]:
    """A device override when EVEREST_V2G_DEVICE is set, nothing otherwise.

    CI leaves EVEREST_V2G_DEVICE unset (device stays ``auto``, the network-isolation
    plugin picks the per-worker veth); a developer host sets it to e.g. ``v2g0``.
    """
    local_device = os.environ.get("EVEREST_V2G_DEVICE")
    if local_device:
        return [NetworkInterfaceConfigAdjustmentStrategy(local_device)]
    return []


def _ev_config_adaptions(auto_exec_commands: str) -> List[EverestConfigAdjustmentStrategy]:
    """Auto_exec strategy plus, when EVEREST_V2G_DEVICE is set, a device override."""
    return [EvAutoExecAdjustmentStrategy(auto_exec_commands), *_ev_device_adaptions()]


async def wait_for_call(mock: Mock, timeout: float = 30.0):
    """Wait until mock has been called at least once. Raises TimeoutError otherwise."""
    start_time = asyncio.get_event_loop().time()
    while asyncio.get_event_loop().time() - start_time < timeout:
        if mock.call_count > 0:
            return
        await asyncio.sleep(0.1)
    raise TimeoutError("Timeout waiting for variable publication.")


async def wait_for_match(mock: Mock, predicate: Callable[[Any], bool], timeout: float = 30.0) -> Any:
    """Wait until mock was called with a value the predicate accepts, and return that value.

    Unlike wait_for_call this scans every value seen so far, so a variable that is
    republished with a changing value (for example a list that grows as the EVSE learns
    its capabilities) can be awaited for the value under test rather than the first one.
    """
    start_time = asyncio.get_event_loop().time()
    while asyncio.get_event_loop().time() - start_time < timeout:
        for call in list(mock.call_args_list):
            value = call[0][0]
            if predicate(value):
                return value
        await asyncio.sleep(0.1)
    raise TimeoutError("Timeout waiting for a matching variable publication.")
