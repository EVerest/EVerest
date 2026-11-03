#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright Pionix GmbH and Contributors to EVerest

"""Helpers shared by the ISO 15118 EV SIL tests.

The Ev15118 module tests drive EvManager through auto_exec (``_ev_config_adaptions``); the
EvController tests run the session in-process (``start_iso_session``, ``stop_iso_session``).

Not a conftest.py: none of these are pytest fixtures. ``_ev_config_adaptions`` and
the strategies are evaluated inside the
``@pytest.mark.everest_config_adaptions`` decorator, i.e. at module import time,
and the ``wait_for_*`` helpers are plain coroutines.
"""

import asyncio
import json
import os
from copy import deepcopy
from typing import Any, Callable, Dict, List
from unittest.mock import Mock

from everest.testing.core_utils import EverestConfigAdjustmentStrategy

from smoke_tests import NetworkInterfaceConfigAdjustmentStrategy


# The one grid_support directive the probe pushes into the SECC. Evse15118D20 relays WattPF onto the
# IEC WattCosPhiMode control function, so WattCosPhiMode is the only bit the SECC's AC_DER_IEC
# DERControlFunctions offer carries. The AC DER configs declare WattCosPhiMode on the EV, so the
# offer is within what the EV supports and the strict der_stop_on_unsupported_functions path
# accepts it instead of stopping the session.
#
# x is a percentage of the EVSE maximum charge power, y a cos phi (its sign selects the excitation),
# so this curve needs no EVSE reactive-power base to de-normalize.
WATT_PF_DIRECTIVES = {
    "evse_id": 1,
    "directives": [
        {
            "id": "sil-der-watt-pf",
            "directive_type": "WattPF",
            "priority": 0,
            "is_default": False,
            "source": "SIL probe",
            "received_at": "2026-01-01T00:00:00Z",
            "curve": {
                "y_unit": "Not_Applicable",
                "curve_data": [
                    {"x": 0.0, "y": 1.0},
                    {"x": 50.0, "y": 0.97},
                    {"x": 100.0, "y": 0.9},
                ],
            },
        }
    ],
}


class EvManagerAdjustmentStrategy(EverestConfigAdjustmentStrategy):
    """Run the car as EvManager on Ev15118, the module pair these tests drive.

    The SIL configs load EvController behind an EvDriverSim and run ISO 15118
    in-process. EvManager takes the driver's place on the board support, and an
    Ev15118 built from EvController's keys takes the session. ``ev15118_keys`` set
    the Ev15118 keys that EvController has no counterpart for.
    """

    _EV_MANAGER_KEYS = ("connector_id", "dc_target_current", "dc_target_voltage")
    _EV_CONTROLLER_KEYS = ("der_iec_functions", "min_current_a")

    def __init__(self, **ev15118_keys):
        self.ev15118_keys = ev15118_keys

    def adjust_everest_configuration(self, everest_config: Dict) -> Dict:
        adjusted_config = deepcopy(everest_config)
        modules = adjusted_config["active_modules"]
        driver = modules.pop("ev_manager_driver")
        ev_manager = modules["ev_manager"]
        car_keys = ev_manager["config_module"]
        ev15118_keys = {
            key: value
            for key, value in car_keys.items()
            if key not in self._EV_MANAGER_KEYS + self._EV_CONTROLLER_KEYS
        }
        ev15118_keys["device"] = ev15118_keys.pop("hlc_device")
        ev15118_keys.update(self.ev15118_keys)
        modules["iso15118_car"] = {"module": "Ev15118", "config_module": ev15118_keys}
        ev_manager["module"] = "EvManager"
        ev_manager["config_module"] = {key: value for key, value in car_keys.items() if key in self._EV_MANAGER_KEYS}
        ev_manager["config_module"]["auto_enable"] = True
        ev_manager["connections"]["ev_board_support"] = driver["connections"]["ev_board_support"]
        ev_manager["connections"]["ev"] = [{"module_id": "iso15118_car", "implementation_id": "ev"}]
        return adjusted_config


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


def _ev_config_adaptions(auto_exec_commands: str, **ev15118_keys) -> List[EverestConfigAdjustmentStrategy]:
    """EvManager, its auto_exec and, when EVEREST_V2G_DEVICE is set, a device override."""
    return [
        EvManagerAdjustmentStrategy(**ev15118_keys),
        EvAutoExecAdjustmentStrategy(auto_exec_commands),
        *_ev_device_adaptions(),
    ]


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


async def wait_for_v2g_message(mock: Mock, message_id: str, timeout: float = 30.0, min_count: int = 1):
    """Wait until the EV reported min_count received V2G messages with this id, from its v2g_messages variable.

    The EV publishes the id of every response it receives, so min_count charge-loop response ids
    are evidence that min_count charge-loop requests were answered. Raises TimeoutError otherwise.
    """
    start_time = asyncio.get_event_loop().time()
    while asyncio.get_event_loop().time() - start_time < timeout:
        if sum(1 for call in list(mock.call_args_list) if call[0][0]["id"] == message_id) >= min_count:
            return
        await asyncio.sleep(0.1)
    raise TimeoutError(f"Timeout waiting for {min_count} {message_id} in v2g_messages.")


def observe_controller_variable(controller, name):
    """Capture a typed e2m variable before the session starts."""
    observed = Mock()
    controller.add_message_observer(
        name, lambda _client, _userdata, message: observed(json.loads(message.payload))
    )
    return observed


async def start_iso_session(controller, mode, **params):
    """Latch the session before passing the pilot, then await the charge loop."""
    events = observe_controller_variable(controller, "iso_session_event")
    await asyncio.to_thread(controller.configure_session, {"mode": mode, "params": params})
    await asyncio.to_thread(controller.ev_driver.plug().wait)
    assert await asyncio.to_thread(controller.state_collector.wait_for_state, "Charging", 30), (
        f"EV did not reach Charging: {controller.state_collector.states}"
    )
    if mode in ("DcIso2", "DcIsoD20"):
        await wait_for_match(events, lambda event: event["kind"] == "PowerReady")
    return events


async def stop_iso_session(controller, events):
    """Wait for the ISO session to finish before removing the cable."""
    controller.stop_session()
    await wait_for_match(events, lambda event: event["kind"] == "V2GFinished", timeout=40)
    await asyncio.to_thread(controller.ev_driver.unplug().wait)
    assert await asyncio.to_thread(controller.state_collector.wait_for_state, "Unplugged", 10), (
        f"EV did not return to Unplugged: {controller.state_collector.states}"
    )
