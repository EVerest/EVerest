# SPDX-License-Identifier: Apache-2.0
# Copyright Pionix GmbH and Contributors to EVerest

import asyncio
import logging
import time
from copy import deepcopy
from typing import Callable, Optional

from everest.testing.core_utils._configuration.everest_configuration_strategies.everest_configuration_strategy import (
    EverestConfigAdjustmentStrategy,
)
from everest.testing.core_utils.everest_core import EverestCore
from everest.testing.core_utils.probe_module import ProbeModule

from mock_vtn import CLIENT_ID, CLIENT_SECRET

BOOT_TIMEOUT_S = 30


class VtnStrategy(EverestConfigAdjustmentStrategy):
    """Points every RsOpenLEADR module to the mock VTN."""

    def __init__(self, url: str):
        self._url = url

    def adjust_everest_configuration(self, everest_config: dict) -> dict:
        adjusted = deepcopy(everest_config)
        for module_def in adjusted.get("active_modules", {}).values():
            if module_def.get("module") != "RsOpenLEADR":
                continue
            config = module_def.setdefault("config_module", {})
            config["openadr_vtn_url"] = self._url
            config["openadr_vtn_client_id"] = CLIENT_ID
            config["openadr_vtn_client_secret"] = CLIENT_SECRET
        return adjusted


class ExternalLimitsProbe:
    """Implements external_energy_limits on the probe module and records the limits it receives."""

    def __init__(self, probe: ProbeModule):
        self._queue: asyncio.Queue = asyncio.Queue()
        self._loop = asyncio.get_running_loop()
        probe.implement_command("external_limits", "set_external_limits", self._set_external_limits)

    def _set_external_limits(self, args: dict):
        limits = args.get("value", args)
        logging.info(f"Received external limits: {limits}")
        # command handlers run in a separate thread
        self._loop.call_soon_threadsafe(self._queue.put_nowait, limits)

    async def wait_for(self, predicate: Callable[[dict], bool] = lambda _: True, timeout: float = 15) -> dict:
        """Returns the first received limits that satisfy predicate."""
        deadline = time.monotonic() + timeout
        while True:
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                raise TimeoutError("No matching external limits received")
            limits = await asyncio.wait_for(self._queue.get(), timeout=remaining)
            if predicate(limits):
                return limits

    async def expect_none(self, timeout: float) -> Optional[dict]:
        """Returns limits received within timeout, None if there were none."""
        try:
            return await asyncio.wait_for(self._queue.get(), timeout=timeout)
        except asyncio.TimeoutError:
            return None


async def start_probe(everest_core: EverestCore) -> ExternalLimitsProbe:
    everest_core.start(standalone_module="probe")
    probe_module = ProbeModule(everest_core.get_runtime_session())
    limits = ExternalLimitsProbe(probe_module)
    probe_module.start()
    await probe_module.wait_to_be_ready(timeout=BOOT_TIMEOUT_S)
    return limits
