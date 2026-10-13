#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright Pionix GmbH and Contributors to EVerest

"""SIL probe for the ISO 15118-20 AC DER advertisement.

The AC DER energy transfer modes are only offered once DER availability has been
asserted for the EVSE and the EVSE is export capable. Every other test in this
tree asserts on internal state, which is exactly how the historical defect (the
AC DER service never reaching the offered services at all) stayed invisible.
This module boots a real SIL stack and observes what EvseManager actually
publishes as its supported energy transfer modes, before and after
``set_der_available``.

No ISO session is negotiated here, the probe stops at the advertisement
boundary. The SECC still binds the fixed SDP port at boot though, so both tests
carry the ``xdist_group`` marker that serializes ISO configs when the suite runs
in parallel without network isolation. The EvManager in both configs plugs in
after a 20 s ``sleep``, the probe is done well before that and never touches the
car simulator.
"""

import asyncio
import logging
import threading
from typing import List, Optional
from unittest.mock import Mock

import pytest

from everest.testing.core_utils.common import Requirement
from everest.testing.core_utils.fixtures import *
from everest.testing.core_utils.controller.test_controller_interface import (
    TestController,
)
from everest.testing.core_utils.everest_core import EverestCore
from everest.testing.core_utils.probe_module import ProbeModule


# Both AC DER flavors. Neither may be offered before availability is asserted,
# and the two are mutually exclusive afterwards.
AC_DER_MODES = ("AC_DER_IEC", "AC_DER_SAE")

# Generous but bounded, a loaded machine boots the SIL stack slowly.
BOOT_TIMEOUT_S = 60
PUBLISH_TIMEOUT_S = 20


class EnergyTransferModeRecorder:
    """Records every supported_energy_transfer_modes publish, in order.

    The probe module dispatches subscription callbacks from a framework thread,
    so appends are guarded and readers take a copy.
    """

    def __init__(self):
        self._lock = threading.Lock()
        self._publishes: List[List[str]] = []

    def __call__(self, value):
        with self._lock:
            self._publishes.append(list(value))

    def publishes(self) -> List[List[str]]:
        with self._lock:
            return [list(entry) for entry in self._publishes]

    def latest(self) -> Optional[List[str]]:
        with self._lock:
            return list(self._publishes[-1]) if self._publishes else None


async def wait_for_first_publish(
    recorder: EnergyTransferModeRecorder, timeout: int = PUBLISH_TIMEOUT_S
) -> List[str]:
    """Wait until at least one energy transfer mode list has been observed.

    Raises TimeoutError, not an assertion error, so that "nothing was ever
    published" cannot be mistaken for "the wrong thing was published".
    """
    deadline = asyncio.get_event_loop().time() + timeout
    while asyncio.get_event_loop().time() < deadline:
        latest = recorder.latest()
        if latest is not None:
            return latest
        await asyncio.sleep(0.1)

    raise TimeoutError(
        "Timeout waiting for the first supported_energy_transfer_modes publish "
        f"({timeout} s). EvseManager published nothing at all."
    )


async def wait_for_publish_containing(
    recorder: EnergyTransferModeRecorder,
    mode: str,
    timeout: int = PUBLISH_TIMEOUT_S,
) -> List[str]:
    """Wait for a published mode list that contains ``mode``."""
    deadline = asyncio.get_event_loop().time() + timeout
    while asyncio.get_event_loop().time() < deadline:
        for entry in recorder.publishes():
            if mode in entry:
                return entry
        await asyncio.sleep(0.1)

    raise TimeoutError(
        f"Timeout waiting for a supported_energy_transfer_modes publish containing "
        f"'{mode}' ({timeout} s). Observed publishes: {recorder.publishes()}"
    )


async def wait_for_ready(mock, timeout: int = BOOT_TIMEOUT_S):
    """Wait until EvseManager signals that it is ready to start charging."""
    deadline = asyncio.get_event_loop().time() + timeout
    while asyncio.get_event_loop().time() < deadline:
        if mock.call_count > 0:
            return
        await asyncio.sleep(0.1)

    raise TimeoutError(f"Timeout waiting for the evse_manager ready signal ({timeout} s).")


async def probe_der_advertisement(
    test_controller: TestController,
    everest_core: EverestCore,
    expected_mode: str,
):
    """Boot, observe the offered modes, assert DER availability, observe again.

    The negative half is the point of this probe: before ``set_der_available``
    the offered modes must not contain any AC DER flavor. It is not vacuous,
    the assertion only runs after a real publish has been observed, and that
    publish is additionally required to carry AC_BPT, which proves the recorder
    sees the actual content of the list rather than an empty or absent value.
    """
    test_controller.start()
    probe_module = ProbeModule(everest_core.get_runtime_session())

    ready_mock = Mock()
    recorder = EnergyTransferModeRecorder()
    probe_module.subscribe_variable("evse_manager", "ready", ready_mock)
    probe_module.subscribe_variable(
        "evse_manager", "supported_energy_transfer_modes", recorder
    )

    probe_module.start()
    await probe_module.wait_to_be_ready(timeout=BOOT_TIMEOUT_S)
    await wait_for_ready(ready_mock)

    # --- negative half -------------------------------------------------------
    boot_modes = await wait_for_first_publish(recorder)
    logging.info("modes offered at boot: %s", boot_modes)

    assert "AC_BPT" in boot_modes, (
        "Expected the export capable SIL EVSE to offer AC_BPT at boot, got "
        f"{boot_modes}. Without this the DER absence check below would be vacuous."
    )

    for published in recorder.publishes():
        for der_mode in AC_DER_MODES:
            assert der_mode not in published, (
                f"'{der_mode}' was offered before set_der_available was called. "
                f"Observed publishes: {recorder.publishes()}"
            )

    # --- positive half -------------------------------------------------------
    # Same command and arguments as the manual invocation documented in the
    # config headers, dispatched through the probe module because the test
    # harness namespaces the MQTT prefix per run.
    result = await probe_module.call_command(
        "evse_manager", "set_der_available", {"available": True}
    )
    assert result == "Accepted", f"set_der_available returned '{result}', expected 'Accepted'"

    modes = await wait_for_publish_containing(recorder, expected_mode)
    logging.info("modes offered after set_der_available: %s", modes)

    other_flavor = next(mode for mode in AC_DER_MODES if mode != expected_mode)
    assert other_flavor not in modes, (
        f"Both AC DER flavors were offered at once: {modes}"
    )


###################################################
################ Begin Tests ######################
###################################################


@pytest.mark.asyncio
@pytest.mark.xdist_group(name="ISO15118")
@pytest.mark.probe_module(
    connections={"evse_manager": [Requirement("evse_manager", "evse")]}
)
@pytest.mark.everest_core_config("config-sil-ac-d20-der-sae.yaml")
async def test_ac_der_sae_advertisement(
    test_controller: TestController, everest_core: EverestCore
):
    """AC_DER_SAE is offered only after DER availability is asserted."""
    await probe_der_advertisement(test_controller, everest_core, "AC_DER_SAE")


@pytest.mark.asyncio
@pytest.mark.xdist_group(name="ISO15118")
@pytest.mark.probe_module(
    connections={"evse_manager": [Requirement("evse_manager", "evse")]}
)
@pytest.mark.everest_core_config("config-sil-ac-d20-der-iec.yaml")
async def test_ac_der_iec_advertisement(
    test_controller: TestController, everest_core: EverestCore
):
    """AC_DER_IEC is offered only after DER availability is asserted."""
    await probe_der_advertisement(test_controller, everest_core, "AC_DER_IEC")
