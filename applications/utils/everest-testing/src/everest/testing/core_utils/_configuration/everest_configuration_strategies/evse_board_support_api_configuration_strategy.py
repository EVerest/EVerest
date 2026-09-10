from copy import deepcopy
from typing import Dict

from everest.testing.core_utils._configuration.everest_configuration_strategies.everest_configuration_strategy import \
    EverestConfigAdjustmentStrategy


class EvseBoardSupportApiConfigAdjustment(EverestConfigAdjustmentStrategy):
    """ Adjusts the Everest configuration to route connector_1's board support
    through an evse_board_support_API module (`bsp_1`), so tests can drive
    board support (e.g. raise/clear errors) via the stable external API instead
    of the YetiSimulator's built-in board support. """

    def adjust_everest_configuration(self, everest_config: Dict) -> Dict:
        adjusted_config = deepcopy(everest_config)
        active_modules = adjusted_config["active_modules"]
        active_modules["bsp_1"] = {
            "module": "evse_board_support_API",
            "mapping": {"module": {"evse": 1}},
            "config_module": {"cfg_communication_check_to_s": 0},
        }
        active_modules["connector_1"]["connections"]["bsp"] = [
            {"module_id": "bsp_1", "implementation_id": "main"}
        ]
        return adjusted_config
