#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright Pionix GmbH and Contributors to EVerest

from unittest.mock import Mock

import pytest

from everest.testing.core_utils.common import Requirement
from everest.testing.core_utils.fixtures import *
from everest.testing.core_utils.everest_core import EverestCore
from everest.testing.core_utils.probe_module import ProbeModule

from ev_iso15118d20_common import (
    AcFivePercentAdjustmentStrategy,
    _ev_device_adaptions,
    start_iso_session,
    stop_iso_session,
    wait_for_call,
)


# The SECC publishes this literal for any negotiated AC ISO 15118-20 namespace.
D20_AC_PROTOCOL = "ISO15118-20:AC and similar"


@pytest.mark.asyncio
@pytest.mark.xdist_group(name="ISO15118")
@pytest.mark.probe_module(
    connections={
        "charger": [Requirement("iso15118_charger", "charger")],
        "ev": [Requirement("iso15118_car", "ev")],
    }
)
@pytest.mark.everest_core_config("config-sil-ac-d20-plaintext.yaml")
@pytest.mark.everest_config_adaptions(*_ev_device_adaptions(), AcFivePercentAdjustmentStrategy())
async def test_ev_iso15118d20_ac_session(
    evctl_test_controller, everest_core: EverestCore
):
    """SIL gate: the C++ Ev15118 module negotiates an AC ISO 15118-20 session.

    The two-connection probe observes the SECC view of protocol selection.
    """
    everest_core.start()
    probe_module = ProbeModule(everest_core.get_runtime_session())

    selected_protocol_mock = Mock()

    probe_module.subscribe_variable("charger", "selected_protocol", selected_protocol_mock)

    probe_module.start()
    await probe_module.wait_to_be_ready()
    events = await start_iso_session(evctl_test_controller, "AcIsoD20")

    await wait_for_call(selected_protocol_mock, timeout=30)
    selected_protocol = selected_protocol_mock.call_args[0][0]
    assert selected_protocol == D20_AC_PROTOCOL, (
        f"selected_protocol '{selected_protocol}' != expected "
        f"ISO 15118-20 AC protocol literal '{D20_AC_PROTOCOL}'"
    )

    await stop_iso_session(evctl_test_controller, events)


@pytest.mark.asyncio
@pytest.mark.xdist_group(name="ISO15118")
@pytest.mark.probe_module(
    connections={
        "charger": [Requirement("iso15118_charger", "charger")],
        "ev": [Requirement("iso15118_car", "ev")],
    }
)
@pytest.mark.everest_core_config("config-sil-ac-d20-plaintext.yaml")
@pytest.mark.everest_config_adaptions(*_ev_device_adaptions(), AcFivePercentAdjustmentStrategy())
async def test_ev_iso15118d20_full_ac_charge_loop(
    evctl_test_controller, everest_core: EverestCore
):
    """SIL gate: Ev15118 completes a full AC ISO 15118-20 charge loop.

    EvController drives the AC flow to a clean SessionStop. The
    EV-side publishes, in order, drive the charge loop:
      - ev_power_ready (the EVCC is ready for power)
      - ac_evse_target_power (the EVSE target power in the charge loop)
      - v2g_session_finished (clean end)
    """
    everest_core.start()
    probe_module = ProbeModule(everest_core.get_runtime_session())

    power_ready_mock = Mock()
    ac_target_power_mock = Mock()
    finished_mock = Mock()

    probe_module.subscribe_variable("ev", "ev_power_ready", power_ready_mock)
    probe_module.subscribe_variable("ev", "ac_evse_target_power", ac_target_power_mock)
    probe_module.subscribe_variable("ev", "v2g_session_finished", finished_mock)

    probe_module.start()
    await probe_module.wait_to_be_ready()
    events = await start_iso_session(evctl_test_controller, "AcIsoD20")

    await wait_for_call(power_ready_mock, timeout=30)
    assert power_ready_mock.call_args[0][0] is True, "ev_power_ready published without a true value"

    await wait_for_call(ac_target_power_mock, timeout=30)

    await stop_iso_session(evctl_test_controller, events)
    await wait_for_call(finished_mock, timeout=40)
