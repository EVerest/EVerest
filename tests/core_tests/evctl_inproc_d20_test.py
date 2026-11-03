#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright Pionix GmbH and Contributors to EVerest

"""SIL gates for EvController running the ISO 15118-20 EV session in-process.

The configuration has no Ev15118 module, so the EV side is observed through
EvController's own iso_session_event and the SECC's publications.
"""

from unittest.mock import Mock

import pytest

from everest.testing.core_utils.common import Requirement
from everest.testing.core_utils.fixtures import *
from everest.testing.core_utils.everest_core import EverestCore
from everest.testing.core_utils.probe_module import ProbeModule

from ev_iso15118d20_common import (
    start_iso_session,
    stop_iso_session,
    wait_for_call,
    wait_for_match,
)


D20_DC_PROTOCOL = "ISO15118-20:DC"
EXPECTED_EVCC_ID = "AA:BB:CC:DD:EE:01"


@pytest.mark.asyncio
@pytest.mark.xdist_group(name="ISO15118")
@pytest.mark.probe_module(connections={"charger": [Requirement("iso15118_charger", "charger")]})
@pytest.mark.everest_core_config("config-sil-dc-d20-plaintext.yaml")
async def test_evctl_inproc_d20_dc_charge_loop(evctl_test_controller, everest_core: EverestCore):
    """EvController alone completes a unidirectional DC ISO 15118-20 session.

    - charger.selected_protocol and charger.evcc_id: the SECC negotiated -20 DC with this EVCC
    - iso_session_event PowerReady and DcPowerOn: the in-process session drove the FSM
    - two charge-loop requests after current_demand_started: the charge loop ran. The EV runs
      Dynamic by default (d20_dc_dynamic_charge_mode); Scheduled publishes dc_ev_target_voltage_current
    - iso_session_event V2GFinished: the session ended cleanly on stop
    """
    evctl_test_controller.start()
    probe_module = ProbeModule(everest_core.get_runtime_session())

    selected_protocol_mock = Mock()
    evcc_id_mock = Mock()
    # One recorder keeps the order of the charge-loop publications.
    charge_loop_mock = Mock()

    probe_module.subscribe_variable("charger", "selected_protocol", selected_protocol_mock)
    probe_module.subscribe_variable("charger", "evcc_id", evcc_id_mock)
    probe_module.subscribe_variable(
        "charger", "current_demand_started", lambda _: charge_loop_mock("current_demand_started")
    )
    for request in ("d20_dc_dynamic_charge_mode", "dc_ev_target_voltage_current"):
        probe_module.subscribe_variable("charger", request, lambda _: charge_loop_mock("loop_request"))

    probe_module.start()
    await probe_module.wait_to_be_ready()
    events = await start_iso_session(evctl_test_controller, "DcIsoD20")

    await wait_for_call(selected_protocol_mock, timeout=30)
    assert selected_protocol_mock.call_args[0][0] == D20_DC_PROTOCOL

    await wait_for_match(events, lambda event: event["kind"] == "DcPowerOn")

    def loop_requests():
        names = [call.args[0] for call in charge_loop_mock.call_args_list]
        if "current_demand_started" not in names:
            return 0
        return names[names.index("current_demand_started") :].count("loop_request")

    await wait_for_match(charge_loop_mock, lambda _: loop_requests() >= 2)

    await stop_iso_session(evctl_test_controller, events)

    assert evcc_id_mock.call_count > 0, "evcc_id was never published"
    assert evcc_id_mock.call_args[0][0] == EXPECTED_EVCC_ID
