#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright Pionix GmbH and Contributors to EVerest

"""Helpers shared by the Ev15118 SIL tests."""

import asyncio
import json
import os
from typing import Any, Callable, List
from unittest.mock import Mock

from everest.testing.core_utils import EverestConfigAdjustmentStrategy
from everest.testing.core_utils._configuration.everest_configuration_strategies.ac_five_percent import (
    AcFivePercentAdjustmentStrategy,
)

from smoke_tests import NetworkInterfaceConfigAdjustmentStrategy


def _ev_device_adaptions() -> List[EverestConfigAdjustmentStrategy]:
    """A device override when EVEREST_V2G_DEVICE is set, nothing otherwise.

    CI leaves EVEREST_V2G_DEVICE unset (device stays ``auto``, the network-isolation
    plugin picks the per-worker veth); a developer host sets it to e.g. ``v2g0``.
    """
    local_device = os.environ.get("EVEREST_V2G_DEVICE")
    if local_device:
        return [NetworkInterfaceConfigAdjustmentStrategy(local_device)]
    return []


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


def observe_controller_variable(controller, name):
    """Capture a typed e2m variable before the session starts."""
    observed = Mock()
    controller.add_message_observer(
        name, lambda _client, _userdata, message: observed(json.loads(message.payload))
    )
    return observed


async def start_iso_session(controller, mode, **params):
    """Latch the session before passing the pilot, then await the charge loop."""
    await asyncio.to_thread(controller.start)
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
