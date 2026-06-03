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

# Use smoke_tests.py's "ISO15118_SERIAL" loadgroup so --dist=loadgroup keeps
# every EvSimulator SIL test on one xdist worker. This name is intentionally
# NOT the "ISO15118" group that the network-isolation plugin strips, so the
# pause/resume SIL tests stay serialized on a single worker even under network
# isolation. Concurrent SIL stacks contend for CPU and flake pause/resume
# teardown; the name must match the "ISO15118_SERIAL" markers in smoke_tests.py
# or the suites still race.
pytestmark = pytest.mark.xdist_group(name="ISO15118_SERIAL")


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

    # Affirm energy actually flows: a Charging-at-0 A regression would
    # otherwise pass on the state name alone.
    assert evsim_test_controller.state_collector.wait_for_soc_progress(
        min_increase=0.01, timeout=_TIMEOUT_FINISH
    ), "SoC did not advance while in Charging under DC ISO-2"

    evsim_test_controller.plug_out_iso()

    assert evsim_test_controller.state_collector.wait_for_state(
        "Unplugged", timeout=_TIMEOUT_FINISH
    ), (
        "EvSimulator did not return to Unplugged after stop_session; "
        f"saw {evsim_test_controller.state_collector.states}"
    )


@pytest.mark.everest_core_config("config-sil-evsim-dc.yaml")
def test_iso15118_dc_session_stop_by_evse(everest_core, evsim_test_controller):
    """DC ISO 15118-2 session terminated by an EV-side fault injection.

    The reference test triggers stop via probe-module
    `stop_transaction`. EvSimulator does not expose a probe module and
    has no EVSE-stop hook today, so we approximate an externally driven
    stop by injecting a DiodeFail fault on the EV side. DiodeFail is the
    closest representable EV-side fault in the typed API enum
    (DiodeFail / RcdError / CpErrorE / SlacTimeout / V2GTimeout /
    Internal); the EVSE FSM reacts to the CP fault and the EV side
    leaves `Charging`.

    TODO: replace with a true EVSE-initiated stop once EvSimulator
    exposes an externally driven stop hook.
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

    evsim_test_controller.inject_fault("DiodeFail")

    assert evsim_test_controller.state_collector.wait_for_state_not(
        "Charging", timeout=_TIMEOUT_FINISH
    ), (
        "EvSimulator stayed in Charging after EVSE-side stop; "
        f"saw {evsim_test_controller.state_collector.states}"
    )

    # Assert the fault TYPE actually propagated, not merely that the
    # state left Charging: a spurious unrelated transition off Charging
    # would pass a state-only check falsely.
    fault = evsim_test_controller.state_collector.wait_for_fault(
        "DiodeFail", timeout=_TIMEOUT_FINISH
    )
    assert fault is not None, (
        "EvSimulator did not publish a DiodeFail e2m/fault after "
        "inject_fault('DiodeFail'); "
        f"faults={evsim_test_controller.state_collector.faults}"
    )


# Pause/resume is deterministic: the EV resume goes through the gated
# resume_session() path, which defers waking the SECC until the prior V2G
# session has finished tearing down. The EvSimulator FSM adds a bounded
# fallback so a lost IsoV2GFinished resumes best-effort instead of hanging.
# The pause/resume SIL tests run in the serialized ISO15118_SERIAL xdist
# group, so parallel CPU contention cannot corrupt the ISO teardown ordering.
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


@pytest.mark.everest_core_config("config-sil-evsim-dc.yaml")
def test_iso15118_dc_fault_then_clear_recovers(
    everest_core, evsim_test_controller
):
    """Inject a fault, observe Faulted, clear it, recover to Unplugged.

    Covers the fault-recovery path (inject -> Faulted -> clear_fault ->
    Unplugged) that no other integration test exercises. The Faulted
    FSM state routes ClearFault to Unplugged.
    """
    everest_core.start()

    evsim_test_controller.start()
    evsim_test_controller.plug_in_dc_iso()

    assert evsim_test_controller.state_collector.wait_for_state(
        "Charging", timeout=_TIMEOUT_CHARGING
    ), f"saw {evsim_test_controller.state_collector.states}"

    evsim_test_controller.inject_fault("DiodeFail")

    assert evsim_test_controller.state_collector.wait_for_state(
        "Faulted", timeout=_TIMEOUT_FINISH
    ), (
        "EvSimulator did not enter Faulted after inject_fault; "
        f"saw {evsim_test_controller.state_collector.states}"
    )
    assert evsim_test_controller.state_collector.wait_for_fault(
        "DiodeFail", timeout=_TIMEOUT_FINISH
    ) is not None, "no DiodeFail fault published"

    evsim_test_controller.clear_fault()

    assert evsim_test_controller.state_collector.wait_for_state(
        "Unplugged", timeout=_TIMEOUT_FINISH
    ), (
        "EvSimulator did not recover to Unplugged after clear_fault; "
        f"saw {evsim_test_controller.state_collector.states}"
    )


@pytest.mark.everest_core_config("config-sil-evsim-dc.yaml")
def test_iso15118_dc_unplug_from_fault(everest_core, evsim_test_controller):
    """Graceful teardown from Faulted: unplug while faulted -> Unplugged.

    Guards the teardown path from a fault state (no hang, no stuck FSM).
    """
    everest_core.start()

    evsim_test_controller.start()
    evsim_test_controller.plug_in_dc_iso()

    assert evsim_test_controller.state_collector.wait_for_state(
        "Charging", timeout=_TIMEOUT_CHARGING
    ), f"saw {evsim_test_controller.state_collector.states}"

    evsim_test_controller.inject_fault("DiodeFail")

    assert evsim_test_controller.state_collector.wait_for_state(
        "Faulted", timeout=_TIMEOUT_FINISH
    ), f"saw {evsim_test_controller.state_collector.states}"

    evsim_test_controller.plug_out()

    assert evsim_test_controller.state_collector.wait_for_state(
        "Unplugged", timeout=_TIMEOUT_FINISH
    ), (
        "EvSimulator did not return to Unplugged after unplug-from-fault; "
        f"saw {evsim_test_controller.state_collector.states}"
    )


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
