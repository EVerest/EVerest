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


D20_DC_PROTOCOL = "ISO15118-20:DC"


@pytest.mark.asyncio
@pytest.mark.xdist_group(name="ISO15118")
@pytest.mark.probe_module(
    connections={
        "charger": [Requirement("iso15118_charger", "charger")],
        "ev": [Requirement("iso15118_car", "ev")],
    }
)
@pytest.mark.everest_core_config("config-sil-dc-d20-tls-evcpp.yaml")
@pytest.mark.everest_config_adaptions(
    *_ev_config_adaptions(
        "sleep 1;iso_wait_slac_matched;iso_start_v2g_session DC;"
        "iso_wait_pwr_ready;iso_dc_power_on;iso_wait_for_stop 10;"
        "iso_wait_v2g_session_stopped;unplug"
    )
)
async def test_ev_iso15118d20_tls_dc_charge_loop(
    test_controller: TestController, everest_core: EverestCore
):
    """SIL gate: Ev15118 runs the DC charge loop over TLS 1.3 with mutual authentication."""
    test_controller.start()
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
    assert selected_protocol_mock.call_args[0][0] == D20_DC_PROTOCOL

    await wait_for_call(dc_power_on_mock, timeout=30)
    await wait_for_call(finished_mock, timeout=40)
