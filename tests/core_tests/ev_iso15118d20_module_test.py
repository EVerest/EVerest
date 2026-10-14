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
    _ev_device_adaptions,
    start_iso_session,
    stop_iso_session,
    wait_for_call,
)


D20_DC_PROTOCOL = "ISO15118-20:DC"
EXPECTED_EVCC_ID = "AA:BB:CC:DD:EE:01"


@pytest.mark.asyncio
@pytest.mark.xdist_group(name="ISO15118")
@pytest.mark.probe_module(
    connections={
        "charger": [Requirement("iso15118_charger", "charger")],
        "ev": [Requirement("iso15118_car", "ev")],
    }
)
@pytest.mark.everest_core_config("config-sil-dc-d20-plaintext.yaml")
@pytest.mark.everest_config_adaptions(*_ev_device_adaptions())
async def test_ev_iso15118d20_dc_session(
    evctl_test_controller, everest_core: EverestCore
):
    """SIL gate: the C++ Ev15118 module completes a DC ISO 15118-20 session.

    Observes both sides of the V2G link via a two-connection probe:
      - charger.evcc_id and charger.selected_protocol (SECC view of the EVCC)
      - ev.v2g_session_finished (EVCC view of session completion)
    """
    everest_core.start()
    probe_module = ProbeModule(everest_core.get_runtime_session())

    evcc_id_mock = Mock()
    selected_protocol_mock = Mock()
    finished_mock = Mock()

    probe_module.subscribe_variable("charger", "evcc_id", evcc_id_mock)
    probe_module.subscribe_variable("charger", "selected_protocol", selected_protocol_mock)
    probe_module.subscribe_variable("ev", "v2g_session_finished", finished_mock)

    probe_module.start()
    await probe_module.wait_to_be_ready()
    events = await start_iso_session(evctl_test_controller, "DcIsoD20")

    await wait_for_call(selected_protocol_mock, timeout=30)
    selected_protocol = selected_protocol_mock.call_args[0][0]
    assert selected_protocol == D20_DC_PROTOCOL, (
        f"selected_protocol '{selected_protocol}' != expected "
        f"ISO 15118-20 DC protocol literal '{D20_DC_PROTOCOL}'"
    )

    await stop_iso_session(evctl_test_controller, events)
    await wait_for_call(finished_mock, timeout=30)

    assert evcc_id_mock.call_count > 0, "evcc_id was never published"
    assert evcc_id_mock.call_args[0][0] == EXPECTED_EVCC_ID, (
        f"evcc_id '{evcc_id_mock.call_args[0][0]}' != expected '{EXPECTED_EVCC_ID}'"
    )


@pytest.mark.asyncio
@pytest.mark.xdist_group(name="ISO15118")
@pytest.mark.probe_module(
    connections={
        "charger": [Requirement("iso15118_charger", "charger")],
        "ev": [Requirement("iso15118_car", "ev")],
    }
)
@pytest.mark.everest_core_config("config-sil-dc-d20-plaintext.yaml")
@pytest.mark.everest_config_adaptions(*_ev_device_adaptions())
async def test_ev_iso15118d20_full_dc_charge_loop(
    evctl_test_controller, everest_core: EverestCore
):
    """SIL gate: Ev15118 completes a full unidirectional DC charge loop.

    EvController drives the DC flow to a clean SessionStop. The
    EV-side publications drive EvController:
      - ev_power_ready (from ScheduleExchange, before CableCheck)
      - dc_power_on    (in-tolerance DC_PreChargeResponse -> PowerDelivery(Start))
      - v2g_session_finished (clean end)
    """
    everest_core.start()
    probe_module = ProbeModule(everest_core.get_runtime_session())

    selected_protocol_mock = Mock()
    power_ready_mock = Mock()
    dc_power_on_mock = Mock()
    finished_mock = Mock()

    probe_module.subscribe_variable("charger", "selected_protocol", selected_protocol_mock)
    probe_module.subscribe_variable("ev", "ev_power_ready", power_ready_mock)
    probe_module.subscribe_variable("ev", "dc_power_on", dc_power_on_mock)
    probe_module.subscribe_variable("ev", "v2g_session_finished", finished_mock)

    probe_module.start()
    await probe_module.wait_to_be_ready()
    events = await start_iso_session(evctl_test_controller, "DcIsoD20")

    await wait_for_call(selected_protocol_mock, timeout=30)
    selected_protocol = selected_protocol_mock.call_args[0][0]
    assert selected_protocol == D20_DC_PROTOCOL, (
        f"selected_protocol '{selected_protocol}' != expected "
        f"ISO 15118-20 DC protocol literal '{D20_DC_PROTOCOL}'"
    )

    await wait_for_call(power_ready_mock, timeout=30)
    assert power_ready_mock.call_args[0][0] is True, "ev_power_ready published without a true value"

    await wait_for_call(dc_power_on_mock, timeout=30)

    await stop_iso_session(evctl_test_controller, events)
    await wait_for_call(finished_mock, timeout=40)
