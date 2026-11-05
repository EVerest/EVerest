#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright Pionix GmbH and Contributors to EVerest

"""SIL gates for the ISO 15118-2 / DIN SPEC 70121 engines of the C++ EVCC (Ev15118).

The SECC is Evse15118D20 with ISO 15118-20 left out of its offer, so the SAP handshake
selects the pre-20 generation from the EV's offer.
"""

from copy import deepcopy
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

from ev_iso15118d20_common import _ev_config_adaptions, wait_for_call, wait_for_v2g_message


# selected_protocol literals published by Evse15118D20 (sap_engine.cpp).
DIN_PROTOCOL = "DIN70121"
ISO2_PROTOCOL = "ISO15118-2-2013"

# Charge-loop response ids the EV reports in v2g_messages, the same for DIN and -2.
DC_CHARGE_LOOP_RES = "CurrentDemandRes"
AC_CHARGE_LOOP_RES = "ChargingStatusRes"

DC_CONFIG = "config-sil-dc-d20-iso2-din.yaml"
DC_TLS_CONFIG = "config-sil-dc-d20-iso2-tls.yaml"
DC_TLS_PNC_CONFIG = "config-sil-dc-d20-iso2-tls-pnc.yaml"
AC_CONFIG = "config-sil-ac-d20-iso2.yaml"

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
# iso_wait_pwr_ready moves the EV to CP state C, so the EvseManager closes the contactor that
# PowerDelivery(Start) waits for; the EV then holds the charge loop briefly and stops.
AC_AUTO_EXEC = (
    "sleep 1;iso_wait_slac_matched;iso_start_v2g_session AC;"
    "iso_wait_pwr_ready;iso_draw_power_regulated 16,3;"
    "sleep 5;iso_stop_charging;iso_wait_v2g_session_stopped;unplug"
)


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
    """Drive the auto_exec DC flow and assert the negotiated protocol, two charge-loop exchanges and a clean end."""
    probe_module = ProbeModule(everest_core.get_runtime_session())

    selected_protocol_mock = Mock()
    dc_power_on_mock = Mock()
    v2g_messages_mock = Mock()
    finished_mock = Mock()

    probe_module.subscribe_variable("charger", "selected_protocol", selected_protocol_mock)
    probe_module.subscribe_variable("ev", "dc_power_on", dc_power_on_mock)
    probe_module.subscribe_variable("ev", "v2g_messages", v2g_messages_mock)
    probe_module.subscribe_variable("ev", "v2g_session_finished", finished_mock)

    probe_module.start()
    await probe_module.wait_to_be_ready()

    await wait_for_call(selected_protocol_mock, timeout=30)
    selected_protocol = selected_protocol_mock.call_args[0][0]
    assert selected_protocol == expected_protocol, (
        f"selected_protocol '{selected_protocol}' != expected '{expected_protocol}'"
    )

    await wait_for_call(dc_power_on_mock, timeout=30)
    await wait_for_v2g_message(v2g_messages_mock, DC_CHARGE_LOOP_RES, timeout=30, min_count=2)
    await wait_for_call(finished_mock, timeout=40)


PROBE_CONNECTIONS = {
    "charger": [Requirement("iso15118_charger", "charger")],
    "ev": [Requirement("iso15118_car", "ev")],
}


@pytest.mark.asyncio
@pytest.mark.xdist_group(name="ISO15118")
@pytest.mark.probe_module(connections=PROBE_CONNECTIONS)
@pytest.mark.everest_core_config(DC_CONFIG)
@pytest.mark.everest_config_adaptions(*_dc_adaptions(supported_protocols="ISO15118_20,DIN70121"))
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

    The EV and the SECC both rank -2 ahead of DIN.
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
    is the gate: Evse15118D20 publishes it only after it verified the signed
    AuthorizationReq, so an EIM fallback fails the test instead of passing it.
    """
    test_controller.start()
    probe_module = ProbeModule(everest_core.get_runtime_session())

    selected_protocol_mock = Mock()
    require_auth_pnc_mock = Mock()
    dc_power_on_mock = Mock()
    v2g_messages_mock = Mock()
    finished_mock = Mock()

    probe_module.subscribe_variable("charger", "selected_protocol", selected_protocol_mock)
    probe_module.subscribe_variable("charger", "require_auth_pnc", require_auth_pnc_mock)
    probe_module.subscribe_variable("ev", "dc_power_on", dc_power_on_mock)
    probe_module.subscribe_variable("ev", "v2g_messages", v2g_messages_mock)
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
    await wait_for_v2g_message(v2g_messages_mock, DC_CHARGE_LOOP_RES, timeout=30, min_count=2)
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

    The gate is protocol selection, a ChargingStatus exchange and a clean SessionStop
    reported by the EV.
    """
    test_controller.start()
    probe_module = ProbeModule(everest_core.get_runtime_session())

    selected_protocol_mock = Mock()
    v2g_messages_mock = Mock()
    finished_mock = Mock()

    probe_module.subscribe_variable("charger", "selected_protocol", selected_protocol_mock)
    probe_module.subscribe_variable("ev", "v2g_messages", v2g_messages_mock)
    probe_module.subscribe_variable("ev", "v2g_session_finished", finished_mock)

    probe_module.start()
    await probe_module.wait_to_be_ready()

    await wait_for_call(selected_protocol_mock, timeout=30)
    selected_protocol = selected_protocol_mock.call_args[0][0]
    assert selected_protocol == ISO2_PROTOCOL, (
        f"selected_protocol '{selected_protocol}' != expected '{ISO2_PROTOCOL}'"
    )

    await wait_for_v2g_message(v2g_messages_mock, AC_CHARGE_LOOP_RES, timeout=30, min_count=2)
    await wait_for_call(finished_mock, timeout=40)
