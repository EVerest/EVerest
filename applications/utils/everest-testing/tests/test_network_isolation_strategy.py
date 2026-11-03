# SPDX-License-Identifier: Apache-2.0
# Copyright Pionix GmbH and Contributors to EVerest
"""Unit tests for NetworkIsolationStrategy."""

import os
import sys

_PKG_SRC = os.path.normpath(os.path.join(os.path.dirname(__file__), "..", "src"))
if _PKG_SRC not in sys.path:
    sys.path.insert(0, _PKG_SRC)

from everest.testing.core_utils.network_isolation.strategy import NetworkIsolationStrategy  # noqa: E402


def test_in_process_ev_controller_gets_the_ev_side_interface():
    config = {
        "active_modules": {
            "charger": {"module": "Evse15118D20", "config_module": {"device": "auto"}},
            "car": {"module": "EvController", "config_module": {"hlc_device": "auto"}},
        }
    }

    adjusted = NetworkIsolationStrategy("ev_test0").adjust_everest_configuration(config)

    assert adjusted["active_modules"]["car"]["config_module"]["hlc_device"] == "ev_test0"
    assert adjusted["active_modules"]["charger"]["config_module"]["device"] == "ev_test0"


def test_ev_controller_without_hlc_device_is_left_alone():
    config = {"active_modules": {"car": {"module": "EvController", "config_module": {"connector_id": 1}}}}

    adjusted = NetworkIsolationStrategy("ev_test0").adjust_everest_configuration(config)

    assert adjusted["active_modules"]["car"]["config_module"] == {"connector_id": 1}
