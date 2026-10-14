# SPDX-License-Identifier: Apache-2.0
# Copyright Pionix GmbH and Contributors to EVerest

from copy import deepcopy

from .everest_configuration_strategy import EverestConfigAdjustmentStrategy


class AcFivePercentAdjustmentStrategy(EverestConfigAdjustmentStrategy):
    """Start AC HLC on 5% PWM so EvController initiates SLAC matching."""

    def adjust_everest_configuration(self, everest_config):
        adjusted_config = deepcopy(everest_config)
        for module in adjusted_config["active_modules"].values():
            if module["module"] == "EvseManager":
                module.setdefault("config_module", {})["ac_hlc_use_5percent"] = True
        return adjusted_config
