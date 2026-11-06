#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright Pionix GmbH and Contributors to EVerest

"""Reservations reach the EvseManager with the reserved EVSE id.

Auth addresses its EvseManagers by their position in the evse_manager connection list,
while reservations carry the EVSE id that each EvseManager reports in get_evse. The probe
stands in for two EvseManagers connected as ids 3 and 1, in that order, so neither "id - 1"
nor any other arithmetic on the id finds the right one.
"""

import asyncio
import threading
from datetime import datetime, timedelta, timezone
from typing import Any, List, Tuple

import pytest

from everest.testing.core_utils.fixtures import *
from everest.testing.core_utils.everest_core import EverestCore
from everest.testing.core_utils.probe_module import ProbeModule

EVSE_IMPLEMENTATIONS = {
    "evse_manager_id_3": 3,
    "evse_manager_id_1": 1,
}

EVSE_MANAGER_COMMANDS = {
    "enable_disable": False,
    "authorize_response": None,
    "withdraw_authorization": None,
    "reserve": True,
    "cancel_reservation": None,
    "pause_charging": False,
    "resume_charging": False,
    "stop_transaction": False,
    "force_unlock": False,
    "external_ready_to_start_charging": False,
    "set_plug_and_charge_configuration": None,
    "update_allowed_energy_transfer_modes": "Rejected",
    "set_der_available": "Rejected",
}

BOOT_TIMEOUT_S = 30
RESERVE_TIMEOUT_S = 15


class EvseManagers:
    """Answers get_evse for each probe EvseManager and records reservation commands."""

    def __init__(self):
        self._lock = threading.Lock()
        self._calls: List[Tuple[str, str]] = []
        self._get_evse_answered = set()

    def implement(self, probe_module: ProbeModule):
        for implementation_id, evse_id in EVSE_IMPLEMENTATIONS.items():
            probe_module.implement_command(
                implementation_id, "get_evse",
                lambda _args, impl=implementation_id, evse=evse_id: self._get_evse(impl, evse))
            for command, result in EVSE_MANAGER_COMMANDS.items():
                probe_module.implement_command(
                    implementation_id, command,
                    lambda _args, impl=implementation_id, cmd=command, res=result: self._record(impl, cmd, res))
        probe_module.implement_command(
            "token_validator", "validate_token",
            lambda _args: self._record("token_validator", "validate_token", {"authorization_status": "Invalid"}))

    def _get_evse(self, implementation_id: str, evse_id: int) -> dict:
        with self._lock:
            self._get_evse_answered.add(implementation_id)
        return {
            "id": evse_id,
            "connectors": [{"id": 1, "charge_mode": "AC", "hlc_capable": False}],
        }

    def _record(self, implementation_id: str, command: str, result: Any) -> Any:
        with self._lock:
            self._calls.append((implementation_id, command))
        return result

    def all_reported(self) -> bool:
        with self._lock:
            return self._get_evse_answered == set(EVSE_IMPLEMENTATIONS)

    def calls(self) -> List[Tuple[str, str]]:
        with self._lock:
            return list(self._calls)


def reservation_request(reservation_id: int, evse_id: int) -> dict:
    expiry_time = datetime.now(timezone.utc) + timedelta(minutes=10)
    return {
        "request": {
            "reservation_id": reservation_id,
            "id_token": "RESERVED_TOKEN",
            "expiry_time": expiry_time.isoformat(timespec="milliseconds").replace("+00:00", "Z"),
            "evse_id": evse_id,
        }
    }


async def start_probe(everest_core: EverestCore, evse_managers: EvseManagers) -> ProbeModule:
    everest_core.start(standalone_module="probe")
    probe_module = ProbeModule(everest_core.get_runtime_session())
    evse_managers.implement(probe_module)
    probe_module.start()
    await probe_module.wait_to_be_ready(timeout=BOOT_TIMEOUT_S)

    deadline = asyncio.get_event_loop().time() + BOOT_TIMEOUT_S
    while not evse_managers.all_reported():
        if asyncio.get_event_loop().time() > deadline:
            raise TimeoutError("Auth did not ask every EvseManager for its EVSE")
        await asyncio.sleep(0.1)
    return probe_module


async def reserve(probe_module: ProbeModule, reservation_id: int, evse_id: int) -> str:
    """Places a reservation, retrying while Auth is still finishing its ready().

    Auth asks for the EVSEs before it registers its reservation callbacks, so the first
    request can race the end of its startup. A request that fails that way returns None.
    """
    deadline = asyncio.get_event_loop().time() + RESERVE_TIMEOUT_S
    result = None
    while asyncio.get_event_loop().time() < deadline:
        result = await probe_module.call_command(
            "reservation", "reserve_now", reservation_request(reservation_id, evse_id))
        if result == "Accepted":
            return result
        await asyncio.sleep(0.2)
    return result


@pytest.mark.asyncio
@pytest.mark.everest_core_config("config-test-auth-reservation-evse-ids.yaml")
@pytest.mark.parametrize("evse_id", [1, 3])
async def test_reservation_is_placed_on_the_reserved_evse(everest_core: EverestCore, evse_id: int):
    evse_managers = EvseManagers()
    probe_module = await start_probe(everest_core, evse_managers)

    assert await reserve(probe_module, reservation_id=1, evse_id=evse_id) == "Accepted"
    assert evse_managers.calls() == [(f"evse_manager_id_{evse_id}", "reserve")]


@pytest.mark.asyncio
@pytest.mark.everest_core_config("config-test-auth-reservation-evse-ids.yaml")
@pytest.mark.parametrize("evse_id", [1, 3])
async def test_reservation_is_cancelled_on_the_reserved_evse(everest_core: EverestCore, evse_id: int):
    evse_managers = EvseManagers()
    probe_module = await start_probe(everest_core, evse_managers)

    assert await reserve(probe_module, reservation_id=1, evse_id=evse_id) == "Accepted"
    assert await probe_module.call_command("reservation", "cancel_reservation", {"reservation_id": 1}) is True
    assert evse_managers.calls() == [
        (f"evse_manager_id_{evse_id}", "reserve"),
        (f"evse_manager_id_{evse_id}", "cancel_reservation"),
    ]
