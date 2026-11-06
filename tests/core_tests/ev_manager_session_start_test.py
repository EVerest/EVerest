#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright Pionix GmbH and Contributors to EVerest

"""SIL gate for an EvManager V2G session start that the ISO15118_ev provider rejects.

The probe module stands in for the vehicle's board support and its ISO 15118 stack, so the
test sees every command the EvManager sends and decides the reply to start_charging.
"""

import asyncio
import logging
import threading
from pathlib import Path
from typing import List, Tuple

import pytest
import yaml

from everest.testing.core_utils.fixtures import *
from everest.testing.core_utils.everest_core import EverestCore
from everest.testing.core_utils.probe_module import ProbeModule

from ev_iso15118d20_common import EvAutoExecAdjustmentStrategy

# pause plugs the vehicle in at B, so an unplug after the rejected start would drive CP A.
# Everything after the rejected start is the part of the sequence that must not run.
REJECTED_START_AUTO_EXEC = "pause;sleep 1;iso_start_v2g_session DC;iso_stop_charging;unplug"
REJECTION_LOG = "ISO15118_ev rejected the start of a V2G session in energy mode 'dc'"

PROBED_INTERFACES = {"ev_board_support": "ev_board_support", "ev": "ISO15118_ev"}


class RecordingVehicle:
    """Answers every command of the probed interfaces and records them in call order."""

    def __init__(self):
        self._lock = threading.Lock()
        self._calls: List[Tuple[str, str, dict]] = []

    def handler(self, implementation_id: str, command: str):
        def handle(arguments):
            with self._lock:
                self._calls.append((implementation_id, command, arguments))
            if (implementation_id, command) == ("ev", "start_charging"):
                return False
            return None

        return handle

    def calls(self) -> List[Tuple[str, str, dict]]:
        with self._lock:
            return list(self._calls)

    def count(self, implementation_id: str, command: str) -> int:
        return sum(1 for call in self.calls() if call[:2] == (implementation_id, command))


def _interface_commands(prefix: Path, interface: str) -> List[str]:
    definition = yaml.safe_load((prefix / "share/everest/interfaces" / f"{interface}.yaml").read_text())
    return list((definition.get("cmds") or {}).keys())


async def _eventually(condition, timeout: float):
    loop = asyncio.get_running_loop()
    deadline = loop.time() + timeout
    while not condition():
        if loop.time() > deadline:
            raise TimeoutError("condition not reached")
        await asyncio.sleep(0.1)


@pytest.mark.asyncio
@pytest.mark.everest_core_config("config-test-ev-manager-probe.yaml")
@pytest.mark.everest_config_adaptions(EvAutoExecAdjustmentStrategy(REJECTED_START_AUTO_EXEC))
async def test_rejected_session_start_ends_the_command_sequence(everest_core: EverestCore, caplog):
    """start_charging answers false: the start is not retried and no later command runs."""
    # EverestCore relays every line the modules print as a debug record.
    caplog.set_level(logging.DEBUG)
    vehicle = RecordingVehicle()
    everest_core.start(standalone_module="probe")
    probe = ProbeModule(everest_core.get_runtime_session())
    for implementation_id, interface in PROBED_INTERFACES.items():
        for command in _interface_commands(everest_core.prefix_path, interface):
            probe.implement_command(implementation_id, command, vehicle.handler(implementation_id, command))
    probe.start()
    await probe.wait_to_be_ready()

    await _eventually(lambda: vehicle.count("ev", "start_charging") > 0, timeout=30)
    start = next(i for i, call in enumerate(vehicle.calls()) if call[:2] == ("ev", "start_charging"))
    await _eventually(lambda: REJECTION_LOG in caplog.text, timeout=5)

    # The simulation loop ticks every 250 ms; a sequence that went on would act within a tick.
    await asyncio.sleep(3)

    after_start = vehicle.calls()[start + 1:]
    forbidden = [
        call for call in after_start
        if call[:2] == ("ev", "stop_charging")
        or (call[:2] == ("ev_board_support", "set_cp_state") and call[2].get("cp_state") == "A")
    ]
    assert vehicle.count("ev", "start_charging") == 1
    assert not forbidden, f"forbidden commands after the rejected start: {forbidden}"
