# SPDX-License-Identifier: Apache-2.0
# Copyright Pionix GmbH and Contributors to EVerest

"""
EverestConfigAdjustmentStrategy that rewrites the `device` field in ISO 15118
modules to use a specific network interface, enabling parallel test execution.

Usage:
    @pytest.mark.everest_config_adaptions(NetworkIsolationStrategy("ev_test0", "ev_test0_peer"))
"""

from copy import deepcopy
from typing import Dict, Optional

from everest.testing.core_utils import EverestConfigAdjustmentStrategy

# EV-facing modules: listen on the EV-side of the veth pair
_EV_SIDE_MODULES = frozenset({"PyEvJosev", "IsoMux"})

# EVSE proxy-side modules: listen on the EVSE/proxy-side of the veth pair
_PROXY_SIDE_MODULES = frozenset({"EvseV2G", "Evse15118D20"})

# All ISO 15118 module types handled by this strategy
ISO15118_MODULE_TYPES = _EV_SIDE_MODULES | _PROXY_SIDE_MODULES


class NetworkIsolationStrategy(EverestConfigAdjustmentStrategy):
    """Rewrites `device` in ISO 15118 modules to use dedicated network interfaces.

    This enables multiple ISO 15118 test sessions to run in parallel by assigning
    each test to a separate virtual ethernet (veth) interface pair, avoiding port
    and multicast conflicts.

    When IsoMux is present:
        - IsoMux and PyEvJosev use `interface_name` as `device` (EV-facing side).
        - EvseV2G and Evse15118D20 use `proxy_interface_name` as `device`.
        - IsoMux `proxy_device` is set to `proxy_interface_name` so it connects
          to the ISO-2/ISO-20 instances via link-local instead of loopback.

    When IsoMux is absent:
        - All ISO 15118 modules use `interface_name` as `device`.

    Args:
        interface_name: The EV-facing network interface (e.g., "ev_test0").
        proxy_interface_name: The EVSE proxy-facing interface (e.g., "ev_test0_peer").
            Used only when IsoMux is present in the config.
    """

    def __init__(self, interface_name: str, proxy_interface_name: Optional[str] = None):
        self._interface_name = interface_name
        self._proxy_interface_name = proxy_interface_name

    def adjust_everest_configuration(self, everest_config: Dict) -> Dict:
        adjusted_config = deepcopy(everest_config)

        active_modules = adjusted_config.get("active_modules", {})

        has_isomux = any(
            module_def.get("module") == "IsoMux"
            for module_def in active_modules.values()
        )

        for module_def in active_modules.values():
            module_type = module_def.get("module", "")
            if module_type not in ISO15118_MODULE_TYPES:
                continue

            config_module = module_def.get("config_module", {})

            if module_type in _EV_SIDE_MODULES:
                if "device" in config_module:
                    config_module["device"] = self._interface_name
                if module_type == "IsoMux" and self._proxy_interface_name:
                    config_module["proxy_device"] = self._proxy_interface_name
            else:  # _PROXY_SIDE_MODULES
                if "device" in config_module:
                    config_module["device"] = (
                        self._proxy_interface_name
                        if has_isomux and self._proxy_interface_name
                        else self._interface_name
                    )

        return adjusted_config
