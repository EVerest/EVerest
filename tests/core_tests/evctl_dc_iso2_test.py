#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright Pionix GmbH and Contributors to EVerest
"""DC ISO 15118-2 session smokes driven by the EvController typed API.

These tests parallel the reference DC ISO-2 cases in
`tests/core_tests/smoke_tests.py` (`test_iso15118_dc_session*`) but
drive the EV side through `evctl_test_controller` (the typed `m2e/*`
MQTT controller) rather than the legacy
`EverestTestController` shell-script bridge. Each test loads
`config/config-sil-dc.yaml` with the DC adjustment, which wires EvController into the
DC stack in place of EvManager + JsCarSimulator.

Runtime gating: the suite needs the deployed EVerest manager binary
to be current with the EvController changes on this branch. Re-run
`ninja -C build install` before invoking pytest; without that step
the EvController module the manager loads may not match the
controller's typed API and tests will hang waiting for `e2m/state`
publications.
"""

import asyncio
from unittest.mock import Mock

import pytest

# Star-import pulls all transitive fixtures (everest_environment,
# core_config, evctl_test_controller, ...) into the test namespace.
from everest.testing.core_utils.fixtures import *  # noqa: F401,F403
from evctl_config_adjustment import (  # noqa: E402
    EvControllerConfigAdjustment,
    EVCTL_BATTERY_FULL_SESSION,
    EVCTL_DC_SESSION,
)
from everest.testing.core_utils.common import Requirement
from everest.testing.core_utils.probe_module import ProbeModule


async def _wait_for_session_event(mock, expected, timeout=45):
    """Wait for `expected` on an evse_manager session_event mock.

    Deliberately reads the CHARGER's view rather than EvController's own
    e2m/state. The EV publishes FsmState::Charging on dc_power_on, which the EV module
    emits inside PreCharge BEFORE PowerDeliveryReq is queued, so an e2m/state
    assertion is satisfied one message before the SECC has had the chance to
    refuse - it cannot observe a failed resume at all. session_event
    ChargingStarted only fires once the charger has actually entered its charge
    loop.
    """
    deadline = asyncio.get_event_loop().time() + timeout
    while asyncio.get_event_loop().time() < deadline:
        seen = [c[0][0].get("event") for c in mock.call_args_list]
        if expected in seen:
            mock.reset_mock()
            return
        await asyncio.sleep(0.1)
    seen = [c[0][0].get("event") for c in mock.call_args_list]
    raise TimeoutError(f"timeout waiting for session_event {expected!r}; got {seen}")

# Share smoke_tests.py's "ISO15118" loadgroup. The isolation plugin strips this marker
# whenever network isolation is active, which is the default and what CI runs: each
# xdist worker gets its own veth pair. So the group only binds when isolation is off.
pytestmark = pytest.mark.xdist_group(name="ISO15118")


# Generous timeouts: an ISO 15118-2 DC session involves SLAC, TLS
# handshake, parameter discovery, cable check, pre-charge, and the
# energy transfer loop before the FSM lands on `Charging`.
_TIMEOUT_CHARGING = 60.0
_TIMEOUT_FINISH = 30.0


@pytest.mark.everest_core_config("config-sil-dc.yaml")
@pytest.mark.everest_config_adaptions(EvControllerConfigAdjustment(overrides=EVCTL_DC_SESSION,
                                                       charger_overrides={"supported_DIN70121": False}))
def test_iso15118_dc_session(everest_core, evctl_test_controller):
    """Full DC ISO 15118-2 session end to end, terminated by the EV."""
    everest_core.start()

    evctl_test_controller.start()
    evctl_test_controller.plug_in_dc_iso()

    assert evctl_test_controller.state_collector.wait_for_state(
        "Charging", timeout=_TIMEOUT_CHARGING
    ), (
        "EvController did not reach Charging; "
        f"saw {evctl_test_controller.state_collector.states}"
    )

    # Affirm energy actually flows: a Charging-at-0 A regression would
    # otherwise pass on the state name alone.
    assert evctl_test_controller.state_collector.wait_for_soc_progress(
        min_increase=0.01, timeout=_TIMEOUT_FINISH
    ), "SoC did not advance while in Charging under DC ISO-2"

    evctl_test_controller.plug_out_iso()

    assert evctl_test_controller.state_collector.wait_for_state(
        "Unplugged", timeout=_TIMEOUT_FINISH
    ), (
        "EvController did not return to Unplugged after stop_session; "
        f"saw {evctl_test_controller.state_collector.states}"
    )


@pytest.mark.everest_core_config("config-sil-dc.yaml")
@pytest.mark.everest_config_adaptions(EvControllerConfigAdjustment(overrides=EVCTL_DC_SESSION,
                                                       charger_overrides={"supported_DIN70121": False}))
def test_iso15118_dc_session_stop_by_evse(everest_core, evctl_test_controller):
    """A driver-injected diode fault makes the EV leave Charging."""
    everest_core.start()

    evctl_test_controller.start()
    evctl_test_controller.plug_in_dc_iso()

    assert evctl_test_controller.state_collector.wait_for_state(
        "Charging", timeout=_TIMEOUT_CHARGING
    ), (
        "EvController did not reach Charging; "
        f"saw {evctl_test_controller.state_collector.states}"
    )

    evctl_test_controller.inject_fault("DiodeFail")

    assert evctl_test_controller.state_collector.wait_for_state_not(
        "Charging", timeout=_TIMEOUT_FINISH
    ), (
        "EvController stayed in Charging after the driver injected DiodeFail; "
        f"saw {evctl_test_controller.state_collector.states}"
    )

    evctl_test_controller.ev_driver.wait_for(
        "world_state", lambda state: state.get("fault") == "DiodeFail",
        timeout=_TIMEOUT_FINISH,
    )


# Pause/resume is deterministic: the EV resume goes through the gated
# resume_session() path, which defers waking the SECC until the prior V2G
# session has finished tearing down. The EvController FSM adds a bounded
# fallback so a lost IsoV2GFinished resumes best-effort instead of hanging.
@pytest.mark.asyncio
@pytest.mark.probe_module(
    connections={"evse_manager": [Requirement("evse_manager", "evse")]}
)
@pytest.mark.everest_core_config("config-sil-dc.yaml")
@pytest.mark.everest_config_adaptions(EvControllerConfigAdjustment(overrides=EVCTL_DC_SESSION,
                                                       charger_overrides={"supported_DIN70121": False}))
async def test_iso15118_dc_session_paused_by_ev(everest_core, evctl_test_controller):
    """DC ISO 15118-2 session paused by the EV, then resumed.

    Watches the charger's session_event as well as the EV's own FSM state. The
    EV-side state alone cannot see this failing - see _wait_for_session_event.
    """
    # EvControllerTestController.start() only publishes m2e/enable; unlike
    # EverestTestController it does not start EVerest, so the manager has to be
    # started explicitly before the probe module can reach it.
    everest_core.start()

    probe_module = ProbeModule(everest_core.get_runtime_session())
    session_event_mock = Mock()
    probe_module.subscribe_variable("evse_manager", "session_event", session_event_mock)
    probe_module.start()
    await probe_module.wait_to_be_ready()

    evctl_test_controller.start()
    evctl_test_controller.plug_in_dc_iso()

    assert evctl_test_controller.state_collector.wait_for_state(
        "Charging", timeout=_TIMEOUT_CHARGING
    ), (
        "EvController did not reach Charging before pause; "
        f"saw {evctl_test_controller.state_collector.states}"
    )

    # Consume the first session's ChargingStarted before pausing. Without this
    # the post-resume wait below matches that stale event and passes even when
    # the resume never happened - the same blindness this test is meant to fix,
    # one layer up.
    await _wait_for_session_event(session_event_mock, "ChargingStarted")

    evctl_test_controller.pause_session()

    assert evctl_test_controller.state_collector.wait_for_state_not(
        "Charging", timeout=_TIMEOUT_FINISH
    ), (
        "EvController stayed in Charging after EV pause; "
        f"saw {evctl_test_controller.state_collector.states}"
    )
    await _wait_for_session_event(session_event_mock, "ChargingPausedEV")

    evctl_test_controller.resume_session()

    assert evctl_test_controller.state_collector.wait_for_state(
        "Charging", timeout=_TIMEOUT_CHARGING
    ), (
        "EvController did not resume Charging after EV resume; "
        f"saw {evctl_test_controller.state_collector.states}"
    )

    # The assertion that can actually fail when the resume is broken: the
    # charger reaching its charge loop again. Without this the test passed
    # while the resumed session was being refused
    # FAILED_PowerDeliveryNotApplied.
    await _wait_for_session_event(session_event_mock, "ChargingStarted")

    evctl_test_controller.plug_out_iso()


@pytest.mark.everest_core_config("config-sil-dc.yaml")
@pytest.mark.everest_config_adaptions(EvControllerConfigAdjustment(overrides=EVCTL_DC_SESSION,
                                                       charger_overrides={"supported_DIN70121": False}))
def test_iso15118_dc_fault_then_clear_recovers(
    everest_core, evctl_test_controller
):
    """Clear the driver's world fault and disconnect the car through the pilot."""
    everest_core.start()

    evctl_test_controller.start()
    evctl_test_controller.plug_in_dc_iso()

    assert evctl_test_controller.state_collector.wait_for_state(
        "Charging", timeout=_TIMEOUT_CHARGING
    ), f"saw {evctl_test_controller.state_collector.states}"

    evctl_test_controller.inject_fault("DiodeFail")

    evctl_test_controller.ev_driver.wait_for(
        "world_state", lambda state: state.get("fault") == "DiodeFail",
        timeout=_TIMEOUT_FINISH,
    )
    assert evctl_test_controller.state_collector.wait_for_state_not(
        "Charging", timeout=_TIMEOUT_FINISH
    ), f"saw {evctl_test_controller.state_collector.states}"

    after = evctl_test_controller.ev_driver.payload_count("world_state")
    evctl_test_controller.clear_fault()
    evctl_test_controller.ev_driver.wait_for(
        "world_state", lambda state: state.get("fault") is None,
        timeout=_TIMEOUT_FINISH, after=after,
    )
    evctl_test_controller.plug_out()

    assert evctl_test_controller.state_collector.wait_for_state(
        "Unplugged", timeout=_TIMEOUT_FINISH
    ), (
        "EvController did not recover to Unplugged after clearing the fault and unplugging; "
        f"saw {evctl_test_controller.state_collector.states}"
    )


@pytest.mark.everest_core_config("config-sil-dc.yaml")
@pytest.mark.everest_config_adaptions(EvControllerConfigAdjustment(overrides=EVCTL_DC_SESSION,
                                                       charger_overrides={"supported_DIN70121": False}))
def test_iso15118_dc_unplug_from_fault(everest_core, evctl_test_controller):
    """Unplug while a world fault remains active; the car must reach Unplugged."""
    everest_core.start()

    evctl_test_controller.start()
    evctl_test_controller.plug_in_dc_iso()

    assert evctl_test_controller.state_collector.wait_for_state(
        "Charging", timeout=_TIMEOUT_CHARGING
    ), f"saw {evctl_test_controller.state_collector.states}"

    evctl_test_controller.inject_fault("DiodeFail")

    evctl_test_controller.ev_driver.wait_for(
        "world_state", lambda state: state.get("fault") == "DiodeFail",
        timeout=_TIMEOUT_FINISH,
    )
    assert evctl_test_controller.state_collector.wait_for_state_not(
        "Charging", timeout=_TIMEOUT_FINISH
    ), f"saw {evctl_test_controller.state_collector.states}"

    evctl_test_controller.plug_out()

    assert evctl_test_controller.state_collector.wait_for_state(
        "Unplugged", timeout=_TIMEOUT_FINISH
    ), (
        "EvController did not return to Unplugged after unplug-from-fault; "
        f"saw {evctl_test_controller.state_collector.states}"
    )
