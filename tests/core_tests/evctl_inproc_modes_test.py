#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright Pionix GmbH and Contributors to EVerest

"""SIL gates for every ISO 15118 mode EvController runs in-process.

They cover what the Ev15118 module tests (ev_iso15118d20_*_module_test.py and
ev_iso15118_d2_din_module_test.py) cover for EvManager. The configurations have no Ev15118 module, so
the EV side is observed through EvController's own e2m variables, and the charge loop through the SECC: with
EvseManager's session_logging on, Evse15118D20 publishes the id of every V2G message it exchanges
on v2g_messages, so two charge-loop requests are evidence that the EV ran the loop, and a
SessionStopReq that it ended the session cleanly.

A -20 AC session ends without SessionStop here, with Ev15118 as well: the SIL board does not report
the AC contactor open within the SECC's 1400 ms, so the SECC fails PowerDelivery(Stop) and closes
the connection. Those tests stop at V2GFinished.
"""

from copy import deepcopy
from typing import Dict
from unittest.mock import Mock

import pytest

from everest.testing.core_utils import EverestConfigAdjustmentStrategy
from everest.testing.core_utils.common import Requirement
from everest.testing.core_utils.controller.evctl_test_controller import DEFAULT_BPT
from everest.testing.core_utils.fixtures import *
from everest.testing.core_utils.everest_core import EverestCore
from everest.testing.core_utils.probe_module import ProbeModule

from ev_iso15118d20_common import (
    WATT_PF_DIRECTIVES,
    observe_controller_variable,
    start_iso_session,
    stop_iso_session,
    wait_for_call,
    wait_for_match,
    wait_for_v2g_message,
)


# selected_protocol literals published by Evse15118D20 (sap_engine.cpp).
D20_AC_PROTOCOL = "ISO15118-20:AC and similar"
D20_DC_PROTOCOL = "ISO15118-20:DC"
ISO2_PROTOCOL = "ISO15118-2-2013"
DIN_PROTOCOL = "DIN70121"

BPT_CHANNELS = ("Unified", "Separated")

PROBE_CONNECTIONS = {"charger": [Requirement("iso15118_charger", "charger")]}


class EvControllerKeys(EverestConfigAdjustmentStrategy):
    """Override EvController config keys, e.g. to narrow the offered protocols."""

    def __init__(self, **config_keys):
        self.config_keys = config_keys

    def adjust_everest_configuration(self, everest_config: Dict) -> Dict:
        adjusted_config = deepcopy(everest_config)
        adjusted_config["active_modules"]["ev_manager"]["config_module"].update(self.config_keys)
        return adjusted_config


class Session:
    """The SECC publications and EvController events of one in-process session."""

    def __init__(self, everest_core: EverestCore, controller):
        controller.start()
        self.controller = controller
        self.probe = ProbeModule(everest_core.get_runtime_session())
        self.protocol = Mock()
        self.service = Mock()
        self.messages = Mock()
        self.probe.subscribe_variable("charger", "selected_protocol", self.protocol)
        self.probe.subscribe_variable("charger", "selected_service_parameters", self.service)
        self.probe.subscribe_variable("charger", "v2g_messages", self.messages)
        self.events = None

    async def start(self, mode, **params):
        self.probe.start()
        await self.probe.wait_to_be_ready()
        self.events = await start_iso_session(self.controller, mode, **params)

    async def expect_protocol(self, expected):
        await wait_for_call(self.protocol, timeout=30)
        selected = self.protocol.call_args[0][0]
        assert selected == expected, f"selected_protocol '{selected}' != expected '{expected}'"

    async def expect_service(self, energy_transfer, bpt=False):
        await wait_for_call(self.service, timeout=30)
        selected = self.service.call_args[0][0]
        assert selected["energy_transfer"] == energy_transfer, f"selected service {selected}"
        if bpt:
            assert selected.get("bpt_channel") in BPT_CHANNELS, f"no BPT channel in {selected}"

    async def expect_event(self, kind):
        await wait_for_match(self.events, lambda event: event["kind"] == kind)

    async def expect_loop(self, request_id):
        await wait_for_v2g_message(self.messages, request_id, timeout=30, min_count=2)

    async def stop(self, session_stop=True):
        await stop_iso_session(self.controller, self.events)
        if session_stop:
            # The stop was a SessionStop exchange, not a dropped connection.
            await wait_for_v2g_message(self.messages, "SessionStopReq", timeout=10)


def inproc(config, *adaptions, probe=PROBE_CONNECTIONS):
    """The marks every test here shares."""

    def mark(test):
        for decorator in (
            pytest.mark.everest_config_adaptions(*adaptions),
            pytest.mark.everest_core_config(config),
            pytest.mark.probe_module(connections=probe),
            pytest.mark.xdist_group(name="ISO15118"),
            pytest.mark.asyncio,
        ):
            test = decorator(test)
        return test

    return mark


@inproc("config-sil-ac-d20-plaintext.yaml")
async def test_evctl_inproc_d20_ac_charge_loop(evctl_test_controller, everest_core: EverestCore):
    """AcIsoD20: the -20 AC service, PowerReady, two AC_ChargeLoopReq and the session's end."""
    session = Session(everest_core, evctl_test_controller)
    await session.start("AcIsoD20")
    await session.expect_protocol(D20_AC_PROTOCOL)
    await session.expect_service("AC")
    await session.expect_event("PowerReady")
    await session.expect_loop("AcChargeLoopReq")
    await session.stop(session_stop=False)


@inproc("config-sil-ac-bpt-d20.yaml")
async def test_evctl_inproc_d20_ac_bpt_charge_loop(evctl_test_controller, everest_core: EverestCore):
    """AcIsoD20 with bpt: the SECC selects AC_BPT with a BPT channel and the AC loop runs."""
    session = Session(everest_core, evctl_test_controller)
    await session.start("AcIsoD20", bpt=DEFAULT_BPT)
    await session.expect_protocol(D20_AC_PROTOCOL)
    await session.expect_service("AC_BPT", bpt=True)
    await session.expect_event("PowerReady")
    await session.expect_loop("AcChargeLoopReq")
    await session.stop(session_stop=False)


@inproc("config-sil-dc-bpt-d20.yaml")
async def test_evctl_inproc_d20_dc_bpt_charge_loop(evctl_test_controller, everest_core: EverestCore):
    """DcIsoD20 with bpt: the SECC selects DC_BPT with a BPT channel and the DC loop runs."""
    session = Session(everest_core, evctl_test_controller)
    await session.start("DcIsoD20", bpt=DEFAULT_BPT)
    await session.expect_protocol(D20_DC_PROTOCOL)
    await session.expect_service("DC_BPT", bpt=True)
    await session.expect_event("DcPowerOn")
    await session.expect_loop("DcChargeLoopReq")
    await session.stop()


@inproc("config-sil-dc-d20-tls.yaml")
async def test_evctl_inproc_d20_dc_tls_charge_loop(evctl_test_controller, everest_core: EverestCore):
    """DcIsoD20 over TLS 1.3 with mutual authentication: the DC loop runs over the TLS channel."""
    session = Session(everest_core, evctl_test_controller)
    await session.start("DcIsoD20")
    await session.expect_protocol(D20_DC_PROTOCOL)
    await session.expect_event("DcPowerOn")
    await session.expect_loop("DcChargeLoopReq")
    await session.stop()


@inproc(
    "config-sil-ac-der-d20.yaml",
    probe={
        "charger": [Requirement("iso15118_charger", "charger")],
        "grid_support": [Requirement("iso15118_charger", "grid_support")],
        "evse_manager": [Requirement("evse_manager", "evse")],
    },
)
async def test_evctl_inproc_d20_ac_der_iec_session(evctl_test_controller, everest_core: EverestCore):
    """AcIsoD20DerIec: AC_DER_IEC is selected and the negotiated set is the offered WattCosPhiMode."""
    session = Session(everest_core, evctl_test_controller)
    negotiated = observe_controller_variable(evctl_test_controller, "der_negotiated_functions")
    assert await session.probe.call_command("evse_manager", "set_der_available", {"available": True}) == "Accepted"
    assert await session.probe.call_command(
        "grid_support", "set_active_directives", {"directives": WATT_PF_DIRECTIVES}
    ) == {"accepted": True}
    await session.start("AcIsoD20DerIec")
    await session.expect_protocol(D20_AC_PROTOCOL)
    await session.expect_service("AC_DER_IEC")
    functions = await wait_for_match(negotiated, lambda value: value["flavor"] == "IEC")
    assert functions["functions"] == ["WattCosPhiMode"]
    await session.stop(session_stop=False)


@inproc(
    "config-sil-ac-d20-der-sae.yaml",
    probe={
        "charger": [Requirement("iso15118_charger", "charger")],
        "evse_manager": [Requirement("evse_manager", "evse")],
    },
)
async def test_evctl_inproc_d20_ac_der_sae_session(evctl_test_controller, everest_core: EverestCore):
    """AcIsoD20DerSae: AC_DER_SAE is selected and the EV reports the SAE modes it enabled.

    SAE runs the SECC's inert default grid code (grid_support directives reach IEC only), so the
    gate is the service and the SAE flavor of the negotiated set, not its content.
    """
    session = Session(everest_core, evctl_test_controller)
    negotiated = observe_controller_variable(evctl_test_controller, "der_negotiated_functions")
    assert await session.probe.call_command("evse_manager", "set_der_available", {"available": True}) == "Accepted"
    await session.start("AcIsoD20DerSae")
    await session.expect_protocol(D20_AC_PROTOCOL)
    await session.expect_service("AC_DER_SAE")
    await wait_for_match(negotiated, lambda value: value["flavor"] == "SAE")
    await session.stop(session_stop=False)


@inproc("config-sil-dc-d20-iso2-din.yaml", EvControllerKeys(supported_protocols="ISO15118_20,DIN70121"))
async def test_evctl_inproc_din70121_dc_charge_loop(evctl_test_controller, everest_core: EverestCore):
    """DcIso2 offering DIN70121 alone (a DcIso2 start drops -20): DIN is selected and the loop runs."""
    session = Session(everest_core, evctl_test_controller)
    await session.start("DcIso2")
    await session.expect_protocol(DIN_PROTOCOL)
    await session.expect_event("DcPowerOn")
    await session.expect_loop("CurrentDemandReq")
    await session.stop()


@inproc("config-sil-dc-d20-iso2-din.yaml")
async def test_evctl_inproc_iso15118_2_dc_charge_loop(evctl_test_controller, everest_core: EverestCore):
    """DcIso2 over plain TCP with EIM: the EV and the SECC both rank -2 ahead of DIN."""
    session = Session(everest_core, evctl_test_controller)
    await session.start("DcIso2")
    await session.expect_protocol(ISO2_PROTOCOL)
    await session.expect_event("DcPowerOn")
    await session.expect_loop("CurrentDemandReq")
    await session.stop()


@inproc("config-sil-dc-d20-iso2-tls.yaml")
async def test_evctl_inproc_iso15118_2_dc_tls_charge_loop(evctl_test_controller, everest_core: EverestCore):
    """DcIso2 over TLS 1.2 (no client certificate) with EIM."""
    session = Session(everest_core, evctl_test_controller)
    await session.start("DcIso2")
    await session.expect_protocol(ISO2_PROTOCOL)
    await session.expect_event("DcPowerOn")
    await session.expect_loop("CurrentDemandReq")
    await session.stop()


@inproc("config-sil-dc-d20-iso2-tls-pnc.yaml")
async def test_evctl_inproc_iso15118_2_dc_tls_pnc_charge_loop(evctl_test_controller, everest_core: EverestCore):
    """DcIso2 over TLS 1.2 with Plug & Charge.

    The start selects Contract; the EV presents the installed MO contract certificate and signs
    the AuthorizationReq. Evse15118D20 publishes require_auth_pnc only after it verified that
    signature, so an EIM fallback fails the test.
    """
    session = Session(everest_core, evctl_test_controller)
    require_auth_pnc = Mock()
    session.probe.subscribe_variable("charger", "require_auth_pnc", require_auth_pnc)
    await session.start("DcIso2", payment="Contract")
    await session.expect_protocol(ISO2_PROTOCOL)
    await wait_for_call(require_auth_pnc, timeout=30)
    token = require_auth_pnc.call_args[0][0]
    assert token["id_token"]["value"], f"require_auth_pnc carries no eMAID: {token}"
    await session.expect_event("DcPowerOn")
    await session.expect_loop("CurrentDemandReq")
    await session.stop()


@inproc("config-sil-ac-d20-iso2.yaml")
async def test_evctl_inproc_iso15118_2_ac_session(evctl_test_controller, everest_core: EverestCore):
    """AcIso2 over plain TCP with EIM: the SECC closes the contactor and two ChargingStatusReq run."""
    session = Session(everest_core, evctl_test_controller)
    close_contactor = Mock()
    session.probe.subscribe_variable("charger", "ac_close_contactor", close_contactor)
    await session.start("AcIso2")
    await session.expect_protocol(ISO2_PROTOCOL)
    await wait_for_call(close_contactor, timeout=30)
    await session.expect_loop("ChargingStatusReq")
    await session.stop()
