#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright Pionix GmbH and Contributors to EVerest

"""SIL gates for the ISO 15118-2 / DIN SPEC 70121 engines of the C++ EVCC (Ev15118).

The SECC is EvseV2G, which speaks -2 and DIN but not -20, so the SAP handshake selects
the pre-20 generation from the EV's offer. Every test is skipped when the config it needs
or the EvseV2G module is missing from the everest prefix (both are optional build outputs).
"""

from copy import deepcopy
from pathlib import Path
from typing import Dict, List
from unittest.mock import Mock

import pytest

from everest.testing.core_utils.common import Requirement
from everest.testing.core_utils.fixtures import *
from everest.testing.core_utils.controller.test_controller_interface import (
    TestController,
)
from everest.testing.core_utils.everest_core import EverestCore
from everest.testing.core_utils.probe_module import ProbeModule
from everest.testing.core_utils import EverestConfigAdjustmentStrategy

from ev_iso15118d20_common import _ev_config_adaptions, wait_for_call


# selected_protocol literals published by EvseV2G (v2g_server.cpp).
DIN_PROTOCOL = "DIN70121"
ISO2_PROTOCOL = "ISO15118-2-2013"

DC_CONFIG = "config-sil-dc-evsev2g.yaml"
DC_TLS_CONFIG = "config-sil-dc-tls-evsev2g.yaml"
DC_TLS_PNC_CONFIG = "config-sil-dc-d2-tls-pnc-evsev2g.yaml"
AC_CONFIG = "config-sil-evsev2g.yaml"

# The whole DC flow, event-gated after the initial settling sleep.
DC_AUTO_EXEC = (
    "sleep 1;iso_wait_slac_matched;iso_start_v2g_session DC;"
    "iso_wait_pwr_ready;iso_dc_power_on;iso_wait_for_stop 10;"
    "iso_wait_v2g_session_stopped;unplug"
)
DC_PNC_AUTO_EXEC = (
    "sleep 1;iso_wait_slac_matched;iso_start_v2g_session DC,contract;"
    "iso_wait_pwr_ready;iso_dc_power_on;iso_wait_for_stop 10;"
    "iso_wait_v2g_session_stopped;unplug"
)
# AC has no power-on step; hold the charge loop briefly, then stop.
AC_AUTO_EXEC = (
    "sleep 1;iso_wait_slac_matched;iso_start_v2g_session AC;"
    "sleep 5;iso_stop_charging;iso_wait_v2g_session_stopped;unplug"
)


def _everest_prefix(request) -> Path:
    """The --everest-prefix option; a relative value is resolved the way core_config does."""
    prefix = Path(request.config.getoption("--everest-prefix"))
    if prefix.is_absolute():
        return prefix
    candidates = [
        (Path.cwd() / prefix).resolve(),
        (request.config.rootpath / prefix).resolve(),
        (request.config.rootpath.parent / prefix).resolve(),
    ]
    return next((path for path in candidates if path.exists()), candidates[0])


@pytest.fixture(autouse=True)
def require_everest_artifacts(request):
    """Skip when EvseV2G or the config of the test is missing from the everest prefix.

    Autouse rather than a decorator: the prefix is only known once pytest has parsed its
    options, which is after collection.
    """
    prefix = _everest_prefix(request)
    if not (prefix / "libexec/everest/modules/EvseV2G").exists():
        pytest.skip("EvseV2G is not built into the everest prefix")
    marker = request.node.get_closest_marker("everest_core_config")
    if marker and marker.args and not (prefix / "etc/everest" / marker.args[0]).exists():
        pytest.skip(f"{marker.args[0]} is not installed")


class EvProtocolAdjustmentStrategy(EverestConfigAdjustmentStrategy):
    """Override Ev15118 config keys, e.g. to narrow the offered protocol generations."""

    def __init__(self, **config_keys):
        self.config_keys = config_keys

    def adjust_everest_configuration(self, everest_config: Dict) -> Dict:
        adjusted_config = deepcopy(everest_config)
        car = adjusted_config["active_modules"]["iso15118_car"]["config_module"]
        car.update(self.config_keys)
        return adjusted_config


def _dc_adaptions(auto_exec: str = DC_AUTO_EXEC, **car_keys) -> List[EverestConfigAdjustmentStrategy]:
    adaptions = list(_ev_config_adaptions(auto_exec))
    if car_keys:
        adaptions.append(EvProtocolAdjustmentStrategy(**car_keys))
    return adaptions


async def _run_dc_session(everest_core: EverestCore, expected_protocol: str):
    """Drive the auto_exec DC flow and assert the negotiated protocol and a clean end."""
    probe_module = ProbeModule(everest_core.get_runtime_session())

    selected_protocol_mock = Mock()
    dc_power_on_mock = Mock()
    finished_mock = Mock()

    probe_module.subscribe_variable("charger", "selected_protocol", selected_protocol_mock)
    probe_module.subscribe_variable("ev", "dc_power_on", dc_power_on_mock)
    probe_module.subscribe_variable("ev", "v2g_session_finished", finished_mock)

    probe_module.start()
    await probe_module.wait_to_be_ready()

    await wait_for_call(selected_protocol_mock, timeout=30)
    selected_protocol = selected_protocol_mock.call_args[0][0]
    assert selected_protocol == expected_protocol, (
        f"selected_protocol '{selected_protocol}' != expected '{expected_protocol}'"
    )

    await wait_for_call(dc_power_on_mock, timeout=30)
    await wait_for_call(finished_mock, timeout=40)


PROBE_CONNECTIONS = {
    "charger": [Requirement("iso15118_charger", "charger")],
    "ev": [Requirement("iso15118_car", "ev")],
}


@pytest.mark.asyncio
@pytest.mark.xdist_group(name="ISO15118")
@pytest.mark.probe_module(connections=PROBE_CONNECTIONS)
@pytest.mark.everest_core_config(DC_CONFIG)
@pytest.mark.everest_config_adaptions(*_dc_adaptions(supported_ISO15118_2=False, supported_DIN70121=True))
async def test_ev_din70121_dc_charge_loop(
    test_controller: TestController, everest_core: EverestCore
):
    """DIN SPEC 70121 DC charge loop: -2 is dropped from the offer, so DIN is selected."""
    test_controller.start()
    await _run_dc_session(everest_core, DIN_PROTOCOL)


@pytest.mark.asyncio
@pytest.mark.xdist_group(name="ISO15118")
@pytest.mark.probe_module(connections=PROBE_CONNECTIONS)
@pytest.mark.everest_core_config(DC_CONFIG)
@pytest.mark.everest_config_adaptions(*_dc_adaptions())
async def test_ev_iso15118_2_dc_charge_loop(
    test_controller: TestController, everest_core: EverestCore
):
    """ISO 15118-2 DC charge loop over plain TCP with EIM.

    The EV offers -2 ahead of DIN, and EvseV2G honors the EV's priority order.
    """
    test_controller.start()
    await _run_dc_session(everest_core, ISO2_PROTOCOL)


@pytest.mark.asyncio
@pytest.mark.xdist_group(name="ISO15118")
@pytest.mark.probe_module(connections=PROBE_CONNECTIONS)
@pytest.mark.everest_core_config(DC_TLS_CONFIG)
@pytest.mark.everest_config_adaptions(*_dc_adaptions())
async def test_ev_iso15118_2_dc_tls_charge_loop(
    test_controller: TestController, everest_core: EverestCore
):
    """ISO 15118-2 DC charge loop over TLS 1.2 (no client certificate) with EIM."""
    test_controller.start()
    await _run_dc_session(everest_core, ISO2_PROTOCOL)


@pytest.mark.asyncio
@pytest.mark.xdist_group(name="ISO15118")
@pytest.mark.probe_module(connections=PROBE_CONNECTIONS)
@pytest.mark.everest_core_config(DC_TLS_PNC_CONFIG)
@pytest.mark.everest_config_adaptions(*_dc_adaptions(DC_PNC_AUTO_EXEC))
async def test_ev_iso15118_2_dc_tls_pnc_charge_loop(
    test_controller: TestController, everest_core: EverestCore
):
    """ISO 15118-2 DC charge loop over TLS 1.2 with Plug & Charge.

    The EvManager selects the Contract payment option; the EV presents the installed MO
    contract certificate in PaymentDetails and signs the AuthorizationReq. require_auth_pnc
    is the gate: EvseV2G publishes it only after a verified PaymentDetails plus signature,
    so an EIM fallback fails the test instead of passing it.
    """
    test_controller.start()
    probe_module = ProbeModule(everest_core.get_runtime_session())

    selected_protocol_mock = Mock()
    require_auth_pnc_mock = Mock()
    dc_power_on_mock = Mock()
    finished_mock = Mock()

    probe_module.subscribe_variable("charger", "selected_protocol", selected_protocol_mock)
    probe_module.subscribe_variable("charger", "require_auth_pnc", require_auth_pnc_mock)
    probe_module.subscribe_variable("ev", "dc_power_on", dc_power_on_mock)
    probe_module.subscribe_variable("ev", "v2g_session_finished", finished_mock)

    probe_module.start()
    await probe_module.wait_to_be_ready()

    await wait_for_call(selected_protocol_mock, timeout=30)
    selected_protocol = selected_protocol_mock.call_args[0][0]
    assert selected_protocol == ISO2_PROTOCOL, (
        f"selected_protocol '{selected_protocol}' != expected '{ISO2_PROTOCOL}'"
    )

    await wait_for_call(require_auth_pnc_mock, timeout=30)
    provided_id_token = require_auth_pnc_mock.call_args[0][0]
    assert provided_id_token["id_token"]["value"], (
        f"require_auth_pnc carries no eMAID: {provided_id_token}"
    )

    await wait_for_call(dc_power_on_mock, timeout=30)
    await wait_for_call(finished_mock, timeout=40)


@pytest.mark.asyncio
@pytest.mark.xdist_group(name="ISO15118")
@pytest.mark.probe_module(connections=PROBE_CONNECTIONS)
@pytest.mark.everest_core_config(AC_CONFIG)
@pytest.mark.everest_config_adaptions(*_ev_config_adaptions(AC_AUTO_EXEC))
async def test_ev_iso15118_2_ac_session(
    test_controller: TestController, everest_core: EverestCore
):
    """ISO 15118-2 AC session over plain TCP with EIM.

    AC has no power-on handshake, so the gate is protocol selection plus a clean
    SessionStop reported by the EV.
    """
    test_controller.start()
    probe_module = ProbeModule(everest_core.get_runtime_session())

    selected_protocol_mock = Mock()
    finished_mock = Mock()

    probe_module.subscribe_variable("charger", "selected_protocol", selected_protocol_mock)
    probe_module.subscribe_variable("ev", "v2g_session_finished", finished_mock)

    probe_module.start()
    await probe_module.wait_to_be_ready()

    await wait_for_call(selected_protocol_mock, timeout=30)
    selected_protocol = selected_protocol_mock.call_args[0][0]
    assert selected_protocol == ISO2_PROTOCOL, (
        f"selected_protocol '{selected_protocol}' != expected '{ISO2_PROTOCOL}'"
    )

    await wait_for_call(finished_mock, timeout=40)
