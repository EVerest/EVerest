#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright Pionix GmbH and Contributors to EVerest

from unittest.mock import Mock

import pytest

from everest.testing.core_utils.common import Requirement
from everest.testing.core_utils.fixtures import *
from everest.testing.core_utils.controller.test_controller_interface import (
    TestController,
)
from everest.testing.core_utils.everest_core import EverestCore
from everest.testing.core_utils.probe_module import ProbeModule

from ev_iso15118d20_common import _ev_config_adaptions, wait_for_call


# The SECC publishes these literals for any negotiated AC / DC ISO 15118-20 namespace.
D20_AC_PROTOCOL = "ISO15118-20:AC and similar"
D20_DC_PROTOCOL = "ISO15118-20:DC"

# The SECC publishes selected_service_parameters once the EV's ServiceSelectionReq
# is accepted. energy_transfer names the negotiated service, and bpt_channel is
# only populated for a BPT service, so both distinguish a BPT session from a plain
# (unidirectional) one.
AC_BPT_SERVICE = "AC_BPT"
DC_BPT_SERVICE = "DC_BPT"
BPT_CHANNELS = ("Unified", "Separated")


@pytest.mark.asyncio
@pytest.mark.xdist_group(name="ISO15118")
@pytest.mark.probe_module(
    connections={
        "charger": [Requirement("iso15118_charger", "charger")],
        "ev": [Requirement("iso15118_car", "ev")],
    }
)
@pytest.mark.everest_core_config("config-sil-ac-bpt-d20-evcpp.yaml")
@pytest.mark.everest_config_adaptions(
    *_ev_config_adaptions(
        # sleep 1: let the modules reach steady state before SLAC matching starts.
        # The rest of the chain is event-gated (iso_wait_*), so no further sleeps.
        "sleep 1;iso_wait_slac_matched;iso_start_v2g_session AC_BPT;"
        "iso_wait_pwr_ready;iso_draw_power_regulated 16,3;iso_wait_for_stop 10;"
        "iso_wait_v2g_session_stopped;unplug"
    )
)
async def test_ev_iso15118d20_ac_bpt_session(
    test_controller: TestController, everest_core: EverestCore
):
    """SIL gate: EvIso15118D20 negotiates an AC ISO 15118-20 BPT session.

    The gate proves BPT NEGOTIATION happened, not reverse power flow: the SECC
    reports AC_BPT as the service the EV selected, and the EV then reaches the
    charge loop, which the EV-side CPD state can only do after accepting BPT AC
    limits in the CPD response. The SIL charge loop still moves power in the
    charge direction; only the negotiated service is bidirectional.
    """
    test_controller.start()
    probe_module = ProbeModule(everest_core.get_runtime_session())

    selected_protocol_mock = Mock()
    selected_service_mock = Mock()
    power_ready_mock = Mock()
    ac_target_power_mock = Mock()
    finished_mock = Mock()

    probe_module.subscribe_variable("charger", "selected_protocol", selected_protocol_mock)
    probe_module.subscribe_variable("charger", "selected_service_parameters", selected_service_mock)
    probe_module.subscribe_variable("ev", "ev_power_ready", power_ready_mock)
    probe_module.subscribe_variable("ev", "ac_evse_target_power", ac_target_power_mock)
    probe_module.subscribe_variable("ev", "v2g_session_finished", finished_mock)

    probe_module.start()
    await probe_module.wait_to_be_ready()

    await wait_for_call(selected_protocol_mock, timeout=30)
    selected_protocol = selected_protocol_mock.call_args[0][0]
    assert selected_protocol == D20_AC_PROTOCOL, (
        f"selected_protocol '{selected_protocol}' != expected "
        f"ISO 15118-20 AC protocol literal '{D20_AC_PROTOCOL}'"
    )

    await wait_for_call(selected_service_mock, timeout=30)
    selected_service_parameters = selected_service_mock.call_args[0][0]
    assert selected_service_parameters["energy_transfer"] == AC_BPT_SERVICE, (
        f"the SECC reports energy_transfer "
        f"'{selected_service_parameters['energy_transfer']}' != expected "
        f"'{AC_BPT_SERVICE}', so BPT was not negotiated"
    )
    assert selected_service_parameters.get("bpt_channel") in BPT_CHANNELS, (
        f"the SECC reports bpt_channel "
        f"'{selected_service_parameters.get('bpt_channel')}'; a BPT channel is only "
        "populated for a negotiated BPT service"
    )

    await wait_for_call(power_ready_mock, timeout=30)
    assert power_ready_mock.call_args[0][0] is True, "ev_power_ready published without a true value"

    await wait_for_call(ac_target_power_mock, timeout=30)

    await wait_for_call(finished_mock, timeout=40)


@pytest.mark.asyncio
@pytest.mark.xdist_group(name="ISO15118")
@pytest.mark.probe_module(
    connections={
        "charger": [Requirement("iso15118_charger", "charger")],
        "ev": [Requirement("iso15118_car", "ev")],
    }
)
@pytest.mark.everest_core_config("config-sil-dc-bpt-d20-evcpp.yaml")
@pytest.mark.everest_config_adaptions(
    *_ev_config_adaptions(
        # sleep 1: let the modules reach steady state before SLAC matching starts.
        # The rest of the chain is event-gated (iso_wait_*), so no further sleeps.
        "sleep 1;iso_wait_slac_matched;iso_start_v2g_session DC_BPT;"
        "iso_wait_pwr_ready;iso_dc_power_on;iso_wait_for_stop 10;"
        "iso_wait_v2g_session_stopped;unplug"
    )
)
async def test_ev_iso15118d20_dc_bpt_session(
    test_controller: TestController, everest_core: EverestCore
):
    """SIL gate: EvIso15118D20 negotiates a DC ISO 15118-20 BPT session.

    The gate proves BPT NEGOTIATION happened, not reverse power flow: the SECC
    reports DC_BPT as the service the EV selected, and the EV then reaches the
    charge loop, which the EV-side CPD state can only do after accepting BPT DC
    limits in the CPD response. iso_dc_power_on blocks the auto_exec chain until
    dc_power_on is published, so the chain only advances once the DC charge loop
    has run on both sides. Power still flows in the charge direction; only the
    negotiated service is bidirectional.
    """
    test_controller.start()
    probe_module = ProbeModule(everest_core.get_runtime_session())

    selected_protocol_mock = Mock()
    selected_service_mock = Mock()
    power_ready_mock = Mock()
    dc_power_on_mock = Mock()
    finished_mock = Mock()

    probe_module.subscribe_variable("charger", "selected_protocol", selected_protocol_mock)
    probe_module.subscribe_variable("charger", "selected_service_parameters", selected_service_mock)
    probe_module.subscribe_variable("ev", "ev_power_ready", power_ready_mock)
    probe_module.subscribe_variable("ev", "dc_power_on", dc_power_on_mock)
    probe_module.subscribe_variable("ev", "v2g_session_finished", finished_mock)

    probe_module.start()
    await probe_module.wait_to_be_ready()

    await wait_for_call(selected_protocol_mock, timeout=30)
    selected_protocol = selected_protocol_mock.call_args[0][0]
    assert selected_protocol == D20_DC_PROTOCOL, (
        f"selected_protocol '{selected_protocol}' != expected "
        f"ISO 15118-20 DC protocol literal '{D20_DC_PROTOCOL}'"
    )

    await wait_for_call(selected_service_mock, timeout=30)
    selected_service_parameters = selected_service_mock.call_args[0][0]
    assert selected_service_parameters["energy_transfer"] == DC_BPT_SERVICE, (
        f"the SECC reports energy_transfer "
        f"'{selected_service_parameters['energy_transfer']}' != expected "
        f"'{DC_BPT_SERVICE}', so BPT was not negotiated"
    )
    assert selected_service_parameters.get("bpt_channel") in BPT_CHANNELS, (
        f"the SECC reports bpt_channel "
        f"'{selected_service_parameters.get('bpt_channel')}'; a BPT channel is only "
        "populated for a negotiated BPT service"
    )

    await wait_for_call(power_ready_mock, timeout=30)
    assert power_ready_mock.call_args[0][0] is True, "ev_power_ready published without a true value"

    await wait_for_call(dc_power_on_mock, timeout=30)

    await wait_for_call(finished_mock, timeout=40)
