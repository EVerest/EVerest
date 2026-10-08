from copy import deepcopy
from typing import Dict

from everest.testing.core_utils._configuration.everest_configuration_strategies.everest_configuration_strategy import EverestConfigAdjustmentStrategy


class YetiSimulatorDisableMeterTransactionStartStrategy(EverestConfigAdjustmentStrategy):
    def adjust_everest_configuration(self, config: Dict) -> Dict:

        adjusted_config = deepcopy(config)

        for module_id, module_config in adjusted_config.get("active_modules", {}).items():
            if module_config.get("module") == "YetiSimulator":
                if "config_module" not in module_config:
                    module_config["config_module"] = {}
                module_config["config_module"]["dummy_meter_value_send_on_transaction_start"] = False

        return adjusted_config
