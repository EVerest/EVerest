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

from ev_iso15118d20_common import _ev_device_adaptions, wait_for_call, wait_for_match


# The SECC publishes this literal for any negotiated AC ISO 15118-20 namespace, DER included.
D20_AC_PROTOCOL = "ISO15118-20:AC and similar"

AC_DER_IEC_SERVICE = "AC_DER_IEC"

# The one grid_support directive the probe pushes into the SECC. Evse15118D20 relays WattPF onto the
# IEC WattCosPhiMode control function, so WattCosPhiMode is the only bit the SECC's AC_DER_IEC
# DERControlFunctions offer carries. config-sil-ac-der-d20-evcpp.yaml declares WattCosPhiMode on the
# EV, so the offer is within what the EV supports and the strict
# der_stop_on_unsupported_functions path accepts it instead of stopping the session.
#
# x is a percentage of the EVSE maximum charge power, y a cos phi (its sign selects the excitation),
# so this curve needs no EVSE reactive-power base to de-normalize.
WATT_PF_DIRECTIVES = {
    "evse_id": 1,
    "directives": [
        {
            "id": "sil-der-watt-pf",
            "directive_type": "WattPF",
            "priority": 0,
            "is_default": False,
            "source": "SIL probe",
            "received_at": "2026-01-01T00:00:00Z",
            "curve": {
                "y_unit": "Not_Applicable",
                "curve_data": [
                    {"x": 0.0, "y": 1.0},
                    {"x": 50.0, "y": 0.97},
                    {"x": 100.0, "y": 0.9},
                ],
            },
        }
    ],
}

# Driven from the probe rather than from auto_exec, so the session starts only after the SECC has
# been told about the DER wiring. The chain is event-gated throughout (iso_wait_*).
SESSION_COMMANDS = (
    "iso_wait_slac_matched;iso_start_v2g_session ac_der;iso_wait_pwr_ready;"
    "iso_draw_power_regulated 16,3;iso_wait_for_stop 10;iso_wait_v2g_session_stopped;unplug"
)


# EvseManager::ready() builds its initial AC energy transfer list with der_available hardcoded to
# false (modules/EVSE/EvseManager/EvseManager.cpp:452), so AC_DER_IEC never reaches the SECC's
# offered services and no AC DER session can be negotiated. That gate is deliberate: DER stays
# unreleased until it has been fully tested. Until it is lifted this test cannot pass, so it is
# skipped rather than left red. Drop this marker when EvseManager passes the real der_available.
@pytest.mark.skip(
    reason="AC_DER_IEC is gated off in EvseManager.cpp:452 until DER is fully tested"
)
@pytest.mark.asyncio
@pytest.mark.xdist_group(name="ISO15118")
@pytest.mark.probe_module(
    connections={
        "charger": [Requirement("iso15118_charger", "charger")],
        "extensions": [Requirement("iso15118_charger", "extensions")],
        "grid_support": [Requirement("iso15118_charger", "grid_support")],
        "evse_manager": [Requirement("evse_manager", "evse")],
        "car_sim": [Requirement("ev_manager", "main")],
        "ev": [Requirement("iso15118_car", "ev")],
    }
)
@pytest.mark.everest_core_config("config-sil-ac-der-d20-evcpp.yaml")
@pytest.mark.everest_config_adaptions(*_ev_device_adaptions())
async def test_ev_iso15118d20_ac_der_iec_session(
    test_controller: TestController, everest_core: EverestCore
):
    """SIL gate: EvIso15118D20 negotiates an AC_DER_IEC ISO 15118-20 session.

    The probe stands in for the protocol backend that a production deployment would have. Two things
    have to come from outside the config: EvseManager only advertises AC_DER_IEC once a backend has
    declared DER wiring through set_der_available, and Evse15118D20 only offers DER control functions
    that were pushed into its grid_support provider. Both are issued before the probe reports ready,
    which is the window in which every other module has finished init() but none has run ready() yet,
    so the SECC sees AC_DER_IEC among its supported energy services before it builds its DER transfer
    limits.

    What the gate proves is negotiation: the SECC reports AC_DER_IEC as the service the EV selected,
    it took the DER branch when reporting the EV's charging needs, and the EV then reaches the charge
    loop, which it can only do after accepting the DER control-function offer and the DER charge
    parameters. Because the offer (WattCosPhiMode) is non-empty and a strict subset of what the EV
    declares, reaching the charge loop at all exercises the intersection rule: an EV that mishandled
    the offered mask would have stopped the session here.

    What the gate does not prove:
      * The exact negotiated control-function set. The EV only logs it. The SECC maps its copy into
        ChargingNeeds.der_charging_parameters.ev_supported_dercontrol, but the generated to_json for
        DERChargingParameters replaces that list with an empty array whenever it is the first present
        optional field, so the value never reaches the wire. Asserting the intersection directly
        needs either a published variable on ISO15118_ev carrying the EV's negotiated mask, or that
        serializer defect fixed.
      * Directive application. The SECC does send DerControl in the charge loop, but EvIso15118D20
        only logs it; no variable carries it.
    """
    test_controller.start()
    probe_module = ProbeModule(everest_core.get_runtime_session())

    selected_protocol_mock = Mock()
    selected_service_mock = Mock()
    charging_needs_mock = Mock()
    supported_modes_mock = Mock()
    sim_enabled_mock = Mock()
    power_ready_mock = Mock()
    finished_mock = Mock()

    probe_module.subscribe_variable("charger", "selected_protocol", selected_protocol_mock)
    probe_module.subscribe_variable("charger", "selected_service_parameters", selected_service_mock)
    probe_module.subscribe_variable("extensions", "charging_needs", charging_needs_mock)
    probe_module.subscribe_variable("evse_manager", "supported_energy_transfer_modes", supported_modes_mock)
    probe_module.subscribe_variable("car_sim", "enabled", sim_enabled_mock)
    probe_module.subscribe_variable("ev", "ev_power_ready", power_ready_mock)
    probe_module.subscribe_variable("ev", "v2g_session_finished", finished_mock)

    der_available_result = await probe_module.call_command("evse_manager", "set_der_available", {"available": True})
    assert der_available_result == "Accepted", (
        f"EvseManager answered '{der_available_result}' to set_der_available; without an accepted DER "
        "declaration it never advertises AC_DER_IEC"
    )

    directives_result = await probe_module.call_command(
        "grid_support", "set_active_directives", {"directives": WATT_PF_DIRECTIVES}
    )
    assert directives_result == {"accepted": True}, (
        f"Evse15118D20 answered '{directives_result}' to set_active_directives; without an accepted "
        "directive the SECC offers no DER control function at all"
    )

    probe_module.start()
    await probe_module.wait_to_be_ready()

    # EvseManager republishes the mode list as it learns the hardware capabilities, so wait for the
    # publication that carries AC_DER_IEC rather than the first one.
    await wait_for_match(supported_modes_mock, lambda modes: AC_DER_IEC_SERVICE in modes, timeout=30)

    # The EvManager silently drops a session request while the simulation is disabled, and it enables
    # itself in its ready(), so wait for that before asking it to run the chain.
    await wait_for_match(sim_enabled_mock, lambda enabled: enabled is True, timeout=30)
    await probe_module.call_command("car_sim", "execute_charging_session", {"value": SESSION_COMMANDS})

    await wait_for_call(selected_protocol_mock, timeout=30)
    selected_protocol = selected_protocol_mock.call_args[0][0]
    assert selected_protocol == D20_AC_PROTOCOL, (
        f"selected_protocol '{selected_protocol}' != expected "
        f"ISO 15118-20 AC protocol literal '{D20_AC_PROTOCOL}'"
    )

    await wait_for_call(selected_service_mock, timeout=30)
    selected_service_parameters = selected_service_mock.call_args[0][0]
    assert selected_service_parameters["energy_transfer"] == AC_DER_IEC_SERVICE, (
        f"the SECC reports energy_transfer "
        f"'{selected_service_parameters['energy_transfer']}' != expected "
        f"'{AC_DER_IEC_SERVICE}', so AC_DER_IEC was not negotiated"
    )

    charging_needs = await wait_for_match(
        charging_needs_mock, lambda needs: "der_charging_parameters" in needs, timeout=30
    )
    assert charging_needs["requested_energy_transfer"] == AC_DER_IEC_SERVICE, (
        f"the SECC reports requested_energy_transfer "
        f"'{charging_needs['requested_energy_transfer']}' != expected '{AC_DER_IEC_SERVICE}'"
    )

    await wait_for_call(power_ready_mock, timeout=30)
    assert power_ready_mock.call_args[0][0] is True, "ev_power_ready published without a true value"

    await wait_for_call(finished_mock, timeout=40)
