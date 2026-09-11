# SPDX-License-Identifier: Apache-2.0
# Copyright Pionix GmbH and Contributors to EVerest

import importlib.util
from pathlib import Path
import sys
import threading
from types import SimpleNamespace
from unittest.mock import Mock

import pytest


@pytest.fixture
def josev_module(monkeypatch):
    # Exercise the module's session loop without starting MQTT, the EV stack, or
    # its Java EXI codec. The session itself is controlled by the test below.
    dependencies = {
        "everest.framework": ("Module", "RuntimeSession", "log"),
        "iso15118.evcc": ("EVCCHandler",),
        "iso15118.evcc.controller.simulator": ("SimEVController",),
        "iso15118.evcc.evcc_config": ("EVCCConfig",),
        "iso15118.evcc.everest": ("context",),
        "iso15118.shared.exificient_exi_codec": ("ExificientEXICodec",),
        "iso15118.shared.settings": ("set_PKI_PATH", "enable_tls_1_3"),
        "utilities": (
            "setup_everest_logging", "determine_network_interface", "patch_josev_config"
        ),
    }
    for name, attributes in dependencies.items():
        monkeypatch.setitem(sys.modules, name, SimpleNamespace(**{
            attribute: Mock() for attribute in attributes
        }))
    monkeypatch.setattr(sys, "path", sys.path.copy())
    module_path = Path(__file__).resolve().parents[2] / "modules/EV/PyEvJosev/module.py"
    spec = importlib.util.spec_from_file_location("py_ev_josev_under_test", module_path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


@pytest.mark.parametrize("next_start", ["during_session", "on_session_finished"])
def test_next_session_start_survives_previous_session_completion(josev_module, next_start):
    module = josev_module
    ev = module.PyEVJosevModule.__new__(module.PyEVJosevModule)
    ev._ready_event = threading.Event()
    ev._es = SimpleNamespace()
    ev._setup = SimpleNamespace(configs=SimpleNamespace(module={}))
    ev._mod = Mock()
    starts = []

    def request_start():
        assert ev._handler_start_charging({
            "DepartureTime": 0,
            "EAmount": 0,
            "EnergyTransferMode": "DC_extended",
            "SelectedPaymentOption": {},
        })

    # Fail immediately instead of hanging if the runner loses the next start.
    def wait_for_pending_start():
        assert ev._ready_event.is_set(), "The next session start was lost"

    ev._ready_event.wait = wait_for_pending_start

    async def run_session(config, codec):
        starts.append(True)
        if len(starts) == 2:
            raise KeyboardInterrupt  # End the otherwise infinite module loop.
        if next_start == "during_session":
            request_start()

    def session_finished(*args):
        if next_start == "on_session_finished":
            request_start()

    module.evcc_handler_main_loop = run_session
    ev._mod.publish_variable.side_effect = session_finished
    request_start()
    ev.start_evcc_handler()

    assert len(starts) == 2
    ev._mod.publish_variable.assert_called_once_with("ev", "v2g_session_finished", None)
    module.ExificientEXICodec.return_value.shutdown.assert_called_once_with()
