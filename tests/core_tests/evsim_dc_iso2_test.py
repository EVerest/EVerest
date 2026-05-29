#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright Pionix GmbH and Contributors to EVerest
"""DC ISO 15118-2 session smokes driven by the EvSimulator typed API.

These tests parallel the reference DC ISO-2 cases in
`tests/core_tests/smoke_tests.py` (`test_iso15118_dc_session*`) but
drive the EV side through `evsim_test_controller` (the typed `m2e/*`
MQTT controller) rather than the legacy
`EverestTestController` shell-script bridge. Each test loads
`config/config-sil-evsim-dc.yaml`, which wires EvSimulator into the
DC stack in place of EvManager + JsCarSimulator.

Runtime gating: the suite needs the deployed EVerest manager binary
to be current with the EvSimulator changes on this branch. Re-run
`ninja -C build install` before invoking pytest; without that step
the EvSimulator module the manager loads may not match the
controller's typed API and tests will hang waiting for `e2m/state`
publications.
"""

import pytest

# Star-import pulls all transitive fixtures (everest_environment,
# core_config, evsim_test_controller, ...) into the test namespace.
from everest.testing.core_utils.fixtures import *  # noqa: F401,F403


# Generous timeouts: an ISO 15118-2 DC session involves SLAC, TLS
# handshake, parameter discovery, cable check, pre-charge, and the
# energy transfer loop before the FSM lands on `Charging`.
_TIMEOUT_CHARGING = 60.0
_TIMEOUT_FINISH = 30.0


@pytest.mark.everest_core_config("config-sil-evsim-dc.yaml")
def test_iso15118_dc_session(everest_core, evsim_test_controller):
    """Full DC ISO 15118-2 session end to end, terminated by the EV."""
    everest_core.start()

    evsim_test_controller.start()
    evsim_test_controller.plug_in_dc_iso()

    assert evsim_test_controller.state_collector.wait_for_state(
        "Charging", timeout=_TIMEOUT_CHARGING
    ), (
        "EvSimulator did not reach Charging; "
        f"saw {evsim_test_controller.state_collector.states}"
    )

    evsim_test_controller.plug_out_iso()

    assert evsim_test_controller.state_collector.wait_for_state(
        "Unplugged", timeout=_TIMEOUT_FINISH
    ), (
        "EvSimulator did not return to Unplugged after stop_session; "
        f"saw {evsim_test_controller.state_collector.states}"
    )


@pytest.mark.everest_core_config("config-sil-evsim-dc.yaml")
def test_iso15118_dc_session_stop_by_evse(everest_core, evsim_test_controller):
    """DC ISO 15118-2 session terminated by an EVSE-side fault injection.

    The reference test triggers stop via probe-module
    `stop_transaction`. EvSimulator does not expose a probe module, so
    we approximate an EVSE-initiated stop by injecting an EmergencyShutdown
    fault — the EVSE FSM reacts identically (StoppingCharging ->
    TransactionFinished) and the EV side leaves `Charging`.
    """
    everest_core.start()

    evsim_test_controller.start()
    evsim_test_controller.plug_in_dc_iso()

    assert evsim_test_controller.state_collector.wait_for_state(
        "Charging", timeout=_TIMEOUT_CHARGING
    ), (
        "EvSimulator did not reach Charging; "
        f"saw {evsim_test_controller.state_collector.states}"
    )

    evsim_test_controller.inject_fault("EmergencyShutdown")

    assert evsim_test_controller.state_collector.wait_for_state_not(
        "Charging", timeout=_TIMEOUT_FINISH
    ), (
        "EvSimulator stayed in Charging after EVSE-side stop; "
        f"saw {evsim_test_controller.state_collector.states}"
    )


@pytest.mark.everest_core_config("config-sil-evsim-dc.yaml")
def test_iso15118_dc_session_paused_by_ev(everest_core, evsim_test_controller):
    """DC ISO 15118-2 session paused by the EV, then resumed."""
    everest_core.start()

    evsim_test_controller.start()
    evsim_test_controller.plug_in_dc_iso()

    assert evsim_test_controller.state_collector.wait_for_state(
        "Charging", timeout=_TIMEOUT_CHARGING
    ), (
        "EvSimulator did not reach Charging before pause; "
        f"saw {evsim_test_controller.state_collector.states}"
    )

    evsim_test_controller.pause_session()

    assert evsim_test_controller.state_collector.wait_for_state_not(
        "Charging", timeout=_TIMEOUT_FINISH
    ), (
        "EvSimulator stayed in Charging after EV pause; "
        f"saw {evsim_test_controller.state_collector.states}"
    )

    evsim_test_controller.resume_session()

    assert evsim_test_controller.state_collector.wait_for_state(
        "Charging", timeout=_TIMEOUT_CHARGING
    ), (
        "EvSimulator did not resume Charging after EV resume; "
        f"saw {evsim_test_controller.state_collector.states}"
    )

    evsim_test_controller.plug_out_iso()


@pytest.mark.skip(
    reason="EvSimulator does not start a new session once it is a user pause"
)
@pytest.mark.everest_core_config("config-sil-evsim-dc.yaml")
def test_iso15118_dc_session_paused_by_evse(everest_core, evsim_test_controller):
    """DC ISO 15118-2 session paused by the EVSE, then resumed.

    Mirrors the reference test's pre-existing skip: the EvSimulator FSM
    does not start a new session after a user-initiated pause, so the
    EVSE-side resume cannot be observed end to end yet.
    """
    everest_core.start()

    evsim_test_controller.start()
    evsim_test_controller.plug_in_dc_iso()

    assert evsim_test_controller.state_collector.wait_for_state(
        "Charging", timeout=_TIMEOUT_CHARGING
    )
