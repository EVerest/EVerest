#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright Pionix GmbH and Contributors to EVerest
"""Turn a stock SIL config into the one an EvSimulator suite needs.

Five config files used to carry this, each a copy of a sibling with a handful of
lines changed, so a fix to the shared 400 lines had to be made five times or not
at all. Read against the EvSimulator manifest, almost every line those copies
added was the module default written out again; what actually distinguished one
run from another is the dozen settings below.
"""

import copy
from dataclasses import dataclass, field
from typing import Any, Dict, Optional

from everest.testing.core_utils._configuration.everest_configuration_strategies.everest_configuration_strategy import (
    EverestConfigAdjustmentStrategy,
)

_PERSISTENT_STORE_ID = "persistent_store"

# The DC suites drive a real charge loop, so they ask for more current than the manifest's 5 A
# default and turn the heartbeat off: at 1000 ms it interleaves with the loop's own pacing and
# the SoC assertions read against a moving target.
EVSIM_DC_SESSION: Dict[str, Any] = {
    "dc_target_current": 50,
    "cfg_heartbeat_interval_ms": 0,
}

# The battery-full suite is the in-tree consumer of the SoC integrator's policy path. The pack and
# threshold are sized so the edge fires in seconds rather than hours: a 1000 Wh pack starting at
# 78 % needs 20 Wh to reach 80 %, and 16 A * 230 V * 3 phases delivers that in about 6.5 s.
EVSIM_BATTERY_FULL_SESSION: Dict[str, Any] = {
    "on_battery_full": "stop_session",
    "battery_full_threshold_pct": 80,
    "dc_energy_capacity": 1000,
    "soc_initial_pct": 78,
}


@dataclass
class EvSimulatorConfigAdjustment(EverestConfigAdjustmentStrategy):
    """Adjust a SIL config for an EvSimulator run.

    `overrides` are the EvSimulator settings this suite needs beyond the module defaults.
    `charger_overrides` reach the charger rather than the vehicle, so they are named separately.
    """

    overrides: Dict[str, Any] = field(default_factory=dict)
    charger_overrides: Dict[str, Any] = field(default_factory=dict)
    ev_module_id: str = "ev_manager"
    charger_module_id: str = "iso15118_charger"

    def adjust_everest_configuration(self, config: Dict) -> Dict:
        adjusted = copy.deepcopy(config)
        modules = adjusted["active_modules"]

        ev = modules[self.ev_module_id]
        if self.overrides:
            ev.setdefault("config_module", {}).update(self.overrides)

        # The module keeps plug state across a boot, so it needs somewhere to keep it. Some SIL
        # configs already stand a store up; the rest get one here.
        store_id = self._existing_store(modules) or _PERSISTENT_STORE_ID
        if store_id not in modules:
            modules[store_id] = {
                "module": "PersistentStore",
                "config_module": {"sqlite_db_file_path": "everest_persistent_store.db"},
                "connections": {},
            }
        ev.setdefault("connections", {})["kvs"] = [
            {"module_id": store_id, "implementation_id": "main"}
        ]

        charger = modules.get(self.charger_module_id)
        if charger is not None:
            charger_config = charger.setdefault("config_module", {})
            # Nothing drives the node-red canvas in a headless run, and standing it up costs a
            # port the suites then contend for.
            charger_config.pop("enable_nodered_interface", None)
            charger_config.update(self.charger_overrides)

        return adjusted

    @staticmethod
    def _existing_store(modules: Dict[str, Any]) -> Optional[str]:
        for module_id, module in modules.items():
            if module.get("module") == "PersistentStore":
                return module_id
        return None
