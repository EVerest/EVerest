from copy import deepcopy
from typing import Dict

from everest.testing.core_utils._configuration.everest_configuration_strategies.everest_configuration_strategy import \
    EverestConfigAdjustmentStrategy


class DisableResetAfterUpdateStrategy(EverestConfigAdjustmentStrategy):
    def adjust_everest_configuration(self, config: Dict) -> Dict:

        adjusted_config = deepcopy(config)

        for module_id, module_config in adjusted_config.get("active_modules", {}).items():
            if module_config.get("module") == "System":
                if "config_module" not in module_config:
                    module_config["config_module"] = {}
                module_config["config_module"]["ResetAfterUpdate"] = False

        return adjusted_config
