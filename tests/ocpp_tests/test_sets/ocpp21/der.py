# SPDX-License-Identifier: Apache-2.0
# Copyright Pionix GmbH and Contributors to EVerest

"""
Acceptance suite for the OCPP 2.1 functional block R (DER control), exercised
end-to-end over a real CSMS websocket against the d20 DC SIL.

The grid_support interface is inverted: the DER device is the *provider* and the
OCPP module (the combined OCPPmulti module) *optionally requires* it. A probe
module stands in for the DER device. It:

  * implements the ``set_active_directives`` command - OCPP201 calls it to push
    the active directive set per EVSE (on a CSMS SetDERControl and on the
    heartbeat). The handler records every call so tests can assert on it.
  * publishes the ``capability`` variable - the device declaring its DER
    capability for an EVSE (replaces the old set_capability command).
  * publishes the ``alarm`` variable - the device raising a grid event fault that
    OCPP201 forwards to the CSMS as NotifyDERAlarm (replaces the old notify_alarm
    command).

How DER gets enabled
--------------------
EverestDeviceModelStorage provisions a DER controller component (DCDERCtrlr for
the DER-capable DC EVSE) disabled (Available=false, empty ModesSupported). When
the device publishes a ``capability`` with a non-empty ``supported_types``,
OCPP201 writes Available=true and ModesSupported (the CSV of supported control
types) onto that component, which makes libocpp build the DERControl functional
block and gate SetDERControl on the declared types.

This write is asynchronous (it travels device -> probe MQTT -> OCPP201 ->
device-model write), so tests must wait for it to commit before driving the CSMS.
``enable_der`` does that by polling the device model (via GetVariables) until
DCDERCtrlr ModesSupported reflects the published capability. Directive pushes are
read by draining the probe's queue and taking a fresh push, since the 1s
heartbeat also enqueues the current active set.
"""

import asyncio
import pytest
import pytest_asyncio
import logging
import queue
from copy import deepcopy
from datetime import datetime, timezone
from typing import Dict, Iterable

# fmt: off
from everest.testing.core_utils.controller.test_controller_interface import TestController
from everest.testing.core_utils.probe_module import ProbeModule
from everest.testing.core_utils import EverestConfigAdjustmentStrategy

from everest.testing.ocpp_utils.fixtures import *
from everest_test_utils import *  # must come before v21 datatype re-imports
from everest.testing.core_utils._configuration.libocpp_configuration_helper import GenericOCPP2XConfigAdjustment
from everest.testing.ocpp_utils.charge_point_utils import wait_for_and_validate, TestUtility

from ocpp.v21.datatypes import (
    FreqDroopType,
    DERCurveType,
    DERCurvePointsType,
    GetVariableDataType,
    SetVariableDataType,
    ComponentType,
    VariableType,
    EVSEType,
)
from ocpp.v21.enums import (
    DERControlEnumType,
    DERControlStatusEnumType,
    DERUnitEnumType,
    AttributeEnumType,
)
# fmt: on

log = logging.getLogger("gridSupportTest")

# The id of the OCPP module in everest-config-ocppmulti-sil-dc-d20-eim.yaml.
# The config names it OCPP201 on disk; OCPPMultiConfigurationStrategy swaps it to
# the combined OCPPmulti module at runtime (the module id "ocpp" is unchanged).
OCPP_MODULE_ID = "ocpp"
# The probe module is always injected under this fixed module id.
PROBE_MODULE_ID = "probe"
# The implementation id the probe provides and the OCPP201 grid_support
# requirement is wired to.
GRID_SUPPORT_IMPL_ID = "grid_support"

# The DER-capable EVSE in the d20 config: a DC EvseManager wired to a bidirectional
# DCSupplySimulator publishes DC_BPT, which classifies it DER-capable so
# EverestDeviceModelStorage provisions a DCDERCtrlr component (disabled).
DER_EVSE_ID = 1

# Default control types a declared capability advertises as supported.
DEFAULT_SUPPORTED_TYPES = ("FreqDroop", "VoltWatt", "VoltVar", "FixedVar")


def rfc3339_now() -> str:
    """RFC3339 / ISO8601 timestamp with timezone, as the grid_support GridAlarm expects."""
    return datetime.now(timezone.utc).isoformat()


def make_capability(supported_types: Iterable[str] = DEFAULT_SUPPORTED_TYPES) -> dict:
    """A DERCapability with the dc inverter class populated and the given supported types.

    At least one of ac/dc must be present; we populate dc to exercise the DC DER
    component variable writes. ``supported_types`` becomes the DCDERCtrlr
    ModesSupported CSV that gates which control types SetDERControl accepts.
    """
    return {
        "supported_types": list(supported_types),
        "nameplate": {
            "max_w_W": 11000.0,
            "max_va_VA": 11000.0,
            "v_nom_V": 400.0,
        },
        "dc": {
            "manufacturer": "Pionix",
            "model": "SIL",
        },
    }


def make_evse_capability(
    evse_id: int = DER_EVSE_ID, supported_types: Iterable[str] = DEFAULT_SUPPORTED_TYPES
) -> dict:
    """The EVSECapability variable value: a DERCapability tagged with its EVSE."""
    return {"evse_id": evse_id, "capability": make_capability(supported_types)}


class GridSupportProbeWiringAdjustment(EverestConfigAdjustmentStrategy):
    """Wires the OCPP module's grid_support requirement to the probe's provided
    implementation and speeds up the active-directive heartbeat.

    The probe module itself is injected by the ``probe_module`` marker; this
    strategy points the OCPP module's (optional) grid_support requirement at the
    probe, so the OCPP module becomes the consumer and the probe the DER-device
    provider. It also sets GridSupportHeartbeatS so the heartbeat fires every
    second for fast test feedback. Because the OCPP module now requires every
    grid_support connection to carry a framework mapping, it also maps the probe
    module to DER_EVSE_ID so the connection is not excluded from DER routing.

    This adjustment keys on the module id (OCPP_MODULE_ID), so it runs before
    OCPPMultiConfigurationStrategy renames the on-disk OCPP201 module to OCPPmulti
    and is unaffected by that rename.
    """

    def __init__(self, heartbeat_s: int = 1):
        self._heartbeat_s = heartbeat_s

    def adjust_everest_configuration(self, everest_config: Dict) -> Dict:
        adjusted_config = deepcopy(everest_config)
        ocpp_module = adjusted_config["active_modules"][OCPP_MODULE_ID]
        ocpp_module.setdefault("config_module", {})["GridSupportHeartbeatS"] = self._heartbeat_s
        ocpp_module.setdefault("connections", {})["grid_support"] = [
            {"module_id": PROBE_MODULE_ID, "implementation_id": GRID_SUPPORT_IMPL_ID}
        ]
        # The probe_module marker already injected the probe's active_modules entry (probe strategies run
        # before user everest_config_adaptions), so map it to the DER EVSE here or OCPP201 excludes it.
        probe_module = adjusted_config["active_modules"][PROBE_MODULE_ID]
        probe_module.setdefault("mapping", {})["module"] = {"evse": DER_EVSE_ID}
        return adjusted_config


@pytest_asyncio.fixture
async def probe_grid_support(started_test_controller, everest_core) -> ProbeModule:
    """A ProbeModule connected to the running EVerest session.

    Started lazily by each test (after registering the set_active_directives
    handler) via ``probe.start()`` + ``probe.wait_to_be_ready()``.
    """
    module = ProbeModule(everest_core.get_runtime_session())
    return module


def provide_grid_support(probe: ProbeModule) -> "queue.Queue":
    """Register the probe as the grid_support provider.

    Implements the ``set_active_directives`` command and returns a queue that
    receives every ActiveDirectiveSet OCPP201 pushes to the device. Must be
    called before ``probe.start()``.
    """
    pushed_directives: "queue.Queue" = queue.Queue()

    def on_set_active_directives(arg: dict) -> dict:
        pushed_directives.put(arg["directives"])
        return {"accepted": True}

    probe.implement_command(GRID_SUPPORT_IMPL_ID, "set_active_directives", on_set_active_directives)
    return pushed_directives


async def wait_for_der_enabled(
    charge_point, evse_id: int = DER_EVSE_ID, timeout_s: float = 20.0
) -> str:
    """Poll DCDERCtrlr ModesSupported until the async capability enable has committed.

    Returns the ModesSupported CSV once non-empty. Raises if the DER controller
    is not enabled within ``timeout_s`` (the capability write was rejected or
    never arrived).
    """
    get_var = GetVariableDataType(
        component=ComponentType(name="DCDERCtrlr", evse=EVSEType(id=evse_id)),
        variable=VariableType(name="ModesSupported"),
        attribute_type=AttributeEnumType.actual,
    )
    last = None
    for _ in range(int(timeout_s / 0.5)):
        response = await charge_point.get_variables_req(get_variable_data=[get_var])
        results = response.get_variable_result if response else None
        if results:
            last = results[0]
            if last.get("attribute_status") == "Accepted" and last.get("attribute_value"):
                return last["attribute_value"]
        await asyncio.sleep(0.5)
    raise AssertionError(
        f"DCDERCtrlr for EVSE {evse_id} not enabled within {timeout_s}s; last GetVariables result: {last}"
    )


async def enable_der(
    probe: ProbeModule,
    charge_point,
    evse_id: int = DER_EVSE_ID,
    supported_types: Iterable[str] = DEFAULT_SUPPORTED_TYPES,
) -> None:
    """Declare DER capability for ``evse_id`` and block until the enable commits.

    The device publishing ``capability`` makes OCPP201 enable the DCDERCtrlr and
    advertise ModesSupported; this waits until that write is observable so the
    caller can drive SetDERControl deterministically.
    """
    probe.publish_variable(
        GRID_SUPPORT_IMPL_ID, "capability", make_evse_capability(evse_id, supported_types)
    )
    await wait_for_der_enabled(charge_point, evse_id)


def drain(pushed: "queue.Queue") -> None:
    """Discard every currently-queued active-directive push."""
    while not pushed.empty():
        pushed.get_nowait()


def latest_directive_types(pushed: "queue.Queue", timeout_s: float = 10.0) -> set:
    """Return the directive types in the next active-directive push for the DER EVSE."""
    received = pushed.get(timeout=timeout_s)
    return {d["directive_type"] for d in received["directives"]}


FREQ_DROOP = FreqDroopType(
    priority=0,
    over_freq=61.0,
    under_freq=59.0,
    over_droop=5.0,
    under_droop=5.0,
    response_time=3.0,
)


# Common marker stack: a real CSMS over the d20 DC SIL config, OCPP 2.1, with the
# probe wired as the grid_support provider (the OCPP module the consumer) and the
# heartbeat sped up to 1s. DER lives only in the combined OCPPmulti module, so
# ocpp_multi_only pins every test to the multi variant (the config's on-disk
# OCPP201 module is swapped to OCPPmulti at runtime).
def grid_support_markers(func):
    func = pytest.mark.asyncio(func)
    func = pytest.mark.xdist_group(name="ISO15118")(func)
    func = pytest.mark.ocpp_version("ocpp2.1")(func)
    func = pytest.mark.ocpp_multi_only(func)
    func = pytest.mark.everest_core_config(
        "everest-config-ocppmulti-sil-dc-d20-eim.yaml"
    )(func)
    func = pytest.mark.ocpp_config_adaptions(
        GenericOCPP2XConfigAdjustment(
            [
                (
                    OCPP2XConfigVariableIdentifier(
                        "InternalCtrlr", "SupportedOcppVersions", "Actual"
                    ),
                    "ocpp2.1",
                )
            ]
        )
    )(func)
    func = pytest.mark.everest_config_adaptions(
        GridSupportProbeWiringAdjustment(heartbeat_s=1)
    )(func)
    func = pytest.mark.probe_module(func)
    return func


# -----------------------------------------------------------------------------
# Inverted-wiring smoke test - boots without enabling DER.
#
# Proves the inversion: the probe provides the grid_support interface, OCPP201
# binds it as its optional requirement, and the charge point boots. Publishing a
# capability and an alarm exercises the OCPP201 consumer handlers.
# -----------------------------------------------------------------------------

@grid_support_markers
async def test_inverted_wiring_boots_and_handles_publishes(
    central_system_v21: CentralSystem,
    test_controller: TestController,
    test_utility: TestUtility,
    probe_grid_support: ProbeModule,
):
    """The probe provides grid_support, OCPP201 requires it, the charge point
    boots, and publishing capability + alarm does not bring the session down.
    """
    provide_grid_support(probe_grid_support)
    probe_grid_support.start()
    await probe_grid_support.wait_to_be_ready()

    # Boot proves OCPP201 started with the probe bound to its grid_support
    # requirement (a misconfigured inversion would fail the manager start).
    charge_point_v21 = await central_system_v21.wait_for_chargepoint(
        wait_for_bootnotification=True
    )
    assert charge_point_v21 is not None

    probe_grid_support.publish_variable(
        GRID_SUPPORT_IMPL_ID, "capability", make_evse_capability(evse_id=1)
    )
    probe_grid_support.publish_variable(
        GRID_SUPPORT_IMPL_ID,
        "alarm",
        {
            "directive_type": "VoltVar",
            "fault": "OverVoltage",
            "alarm_ended": True,
            "timestamp": rfc3339_now(),
        },
    )


# -----------------------------------------------------------------------------
# A published capability enables the EVSE and OCPP201 pushes its active set.
# -----------------------------------------------------------------------------

@grid_support_markers
async def test_capability_enables_evse_and_pushes_active_set(
    central_system_v21: CentralSystem,
    test_controller: TestController,
    test_utility: TestUtility,
    probe_grid_support: ProbeModule,
):
    """A published capability enables the EVSE and OCPP201 pushes its active
    directive set back to the device via set_active_directives.
    """
    pushed = provide_grid_support(probe_grid_support)
    probe_grid_support.start()
    await probe_grid_support.wait_to_be_ready()

    charge_point_v21 = await central_system_v21.wait_for_chargepoint(
        wait_for_bootnotification=True
    )

    await enable_der(probe_grid_support, charge_point_v21)

    pushed_set = pushed.get(timeout=10)
    assert pushed_set["evse_id"] == 1


# -----------------------------------------------------------------------------
# CSMS SetDERControl is accepted and the directive is pushed to the device.
# -----------------------------------------------------------------------------

@grid_support_markers
async def test_set_der_control_applies_directive(
    central_system_v21: CentralSystem,
    test_controller: TestController,
    test_utility: TestUtility,
    probe_grid_support: ProbeModule,
):
    """CSMS SetDERControl(default FreqDroop) -> Accepted, and the directive is
    pushed to the device via set_active_directives.
    """
    pushed = provide_grid_support(probe_grid_support)
    probe_grid_support.start()
    await probe_grid_support.wait_to_be_ready()

    charge_point_v21 = await central_system_v21.wait_for_chargepoint(
        wait_for_bootnotification=True
    )

    await enable_der(probe_grid_support, charge_point_v21)

    r = await charge_point_v21.set_der_control_req(
        is_default=True,
        control_id="ctrl-fd",
        control_type=DERControlEnumType.freq_droop,
        freq_droop=FREQ_DROOP,
    )
    assert r is not None and r.status == DERControlStatusEnumType.accepted

    # Drain stale pushes (enable republish + earlier heartbeats), then the next
    # heartbeat push reflects the new active set containing the directive.
    drain(pushed)
    assert "FreqDroop" in latest_directive_types(pushed)


# -----------------------------------------------------------------------------
# ClearDERControl removes the directive from the pushed set.
# -----------------------------------------------------------------------------

@grid_support_markers
async def test_clear_der_control_removes_directive(
    central_system_v21: CentralSystem,
    test_controller: TestController,
    test_utility: TestUtility,
    probe_grid_support: ProbeModule,
):
    """After SetDERControl, ClearDERControl removes the directive and the
    device sees the slot drop out of the pushed set.
    """
    pushed = provide_grid_support(probe_grid_support)
    probe_grid_support.start()
    await probe_grid_support.wait_to_be_ready()

    charge_point_v21 = await central_system_v21.wait_for_chargepoint(
        wait_for_bootnotification=True
    )

    await enable_der(probe_grid_support, charge_point_v21)

    r_set = await charge_point_v21.set_der_control_req(
        is_default=True,
        control_id="ctrl-fd",
        control_type=DERControlEnumType.freq_droop,
        freq_droop=FREQ_DROOP,
    )
    assert r_set is not None and r_set.status == DERControlStatusEnumType.accepted

    r_clear = await charge_point_v21.clear_der_control_req(
        is_default=True,
        control_type=DERControlEnumType.freq_droop,
    )
    assert r_clear is not None and r_clear.status == DERControlStatusEnumType.accepted

    # After the clear, a fresh push must no longer carry the FreqDroop slot.
    drain(pushed)
    assert "FreqDroop" not in latest_directive_types(pushed)


# -----------------------------------------------------------------------------
# Two distinct default directive types coexist in the pushed set.
# -----------------------------------------------------------------------------

@grid_support_markers
async def test_multiple_directives_coexist(
    central_system_v21: CentralSystem,
    test_controller: TestController,
    test_utility: TestUtility,
    probe_grid_support: ProbeModule,
):
    """Two distinct default directive types coexist in the pushed set."""
    pushed = provide_grid_support(probe_grid_support)
    probe_grid_support.start()
    await probe_grid_support.wait_to_be_ready()

    charge_point_v21 = await central_system_v21.wait_for_chargepoint(
        wait_for_bootnotification=True
    )

    await enable_der(probe_grid_support, charge_point_v21)

    r1 = await charge_point_v21.set_der_control_req(
        is_default=True,
        control_id="ctrl-fd",
        control_type=DERControlEnumType.freq_droop,
        freq_droop=FREQ_DROOP,
    )
    assert r1 is not None and r1.status == DERControlStatusEnumType.accepted

    volt_watt_curve = DERCurveType(
        curve_data=[DERCurvePointsType(x=240.0, y=100.0)],
        priority=1,
        y_unit=DERUnitEnumType.pct_maxw,
    )
    r2 = await charge_point_v21.set_der_control_req(
        is_default=True,
        control_id="ctrl-vw",
        control_type=DERControlEnumType.volt_watt,
        curve=volt_watt_curve,
    )
    assert r2 is not None and r2.status == DERControlStatusEnumType.accepted

    drain(pushed)
    assert {"FreqDroop", "VoltWatt"}.issubset(latest_directive_types(pushed))


# -----------------------------------------------------------------------------
# The heartbeat re-pushes the active directive set on its interval.
# -----------------------------------------------------------------------------

@grid_support_markers
async def test_heartbeat_re_pushes_active_directives(
    central_system_v21: CentralSystem,
    test_controller: TestController,
    test_utility: TestUtility,
    probe_grid_support: ProbeModule,
):
    """The OCPP201 heartbeat re-pushes the active directive set per EVSE on its
    interval (GridSupportHeartbeatS=1), even with no state change.
    """
    pushed = provide_grid_support(probe_grid_support)
    probe_grid_support.start()
    await probe_grid_support.wait_to_be_ready()

    charge_point_v21 = await central_system_v21.wait_for_chargepoint(
        wait_for_bootnotification=True
    )

    await enable_der(probe_grid_support, charge_point_v21)

    r = await charge_point_v21.set_der_control_req(
        is_default=True,
        control_id="ctrl-fd",
        control_type=DERControlEnumType.freq_droop,
        freq_droop=FREQ_DROOP,
    )
    assert r is not None and r.status == DERControlStatusEnumType.accepted

    # Drain, then expect a subsequent heartbeat tick to re-push the set unchanged.
    drain(pushed)
    assert "FreqDroop" in latest_directive_types(pushed)
    # A further drained push proves the heartbeat keeps re-pushing without a state change.
    drain(pushed)
    assert "FreqDroop" in latest_directive_types(pushed)


# -----------------------------------------------------------------------------
# A published alarm is forwarded to the CSMS as NotifyDERAlarm.
# -----------------------------------------------------------------------------

@grid_support_markers
async def test_alarm_forwarded_to_csms(
    central_system_v21: CentralSystem,
    test_controller: TestController,
    test_utility: TestUtility,
    probe_grid_support: ProbeModule,
):
    """Probe publishes alarm GridAlarm{OverVoltage, ended, VoltVar} -> CSMS
    receives NotifyDERAlarm with controlType=VoltVar, gridEventFault=OverVoltage,
    alarmEnded=true.
    """
    provide_grid_support(probe_grid_support)
    probe_grid_support.start()
    await probe_grid_support.wait_to_be_ready()

    charge_point_v21 = await central_system_v21.wait_for_chargepoint(
        wait_for_bootnotification=True
    )

    # on_der_alarm only dispatches NotifyDERAlarm once the DER block is enabled.
    await enable_der(probe_grid_support, charge_point_v21)

    probe_grid_support.publish_variable(
        GRID_SUPPORT_IMPL_ID,
        "alarm",
        {
            "directive_type": "VoltVar",
            "fault": "OverVoltage",
            "alarm_ended": True,
            "timestamp": rfc3339_now(),
        },
    )

    assert await wait_for_and_validate(
        test_utility,
        charge_point_v21,
        "NotifyDERAlarm",
        {
            "controlType": "VoltVar",
            "gridEventFault": "OverVoltage",
            "alarmEnded": True,
        },
    )


# -----------------------------------------------------------------------------
# Alarm forwarding and directive push in one bidirectional round trip.
# -----------------------------------------------------------------------------

@grid_support_markers
async def test_alarm_then_directive_round_trip(
    central_system_v21: CentralSystem,
    test_controller: TestController,
    test_utility: TestUtility,
    probe_grid_support: ProbeModule,
):
    """An alarm published by the device is forwarded to the CSMS, and a
    subsequent CSMS directive is applied and pushed back to the device.
    """
    pushed = provide_grid_support(probe_grid_support)
    probe_grid_support.start()
    await probe_grid_support.wait_to_be_ready()

    charge_point_v21 = await central_system_v21.wait_for_chargepoint(
        wait_for_bootnotification=True
    )

    await enable_der(probe_grid_support, charge_point_v21)

    probe_grid_support.publish_variable(
        GRID_SUPPORT_IMPL_ID,
        "alarm",
        {
            "directive_type": "VoltVar",
            "fault": "OverVoltage",
            "alarm_ended": False,
            "timestamp": rfc3339_now(),
        },
    )

    assert await wait_for_and_validate(
        test_utility,
        charge_point_v21,
        "NotifyDERAlarm",
        {"controlType": "VoltVar", "gridEventFault": "OverVoltage", "alarmEnded": False},
    )

    r = await charge_point_v21.set_der_control_req(
        is_default=True,
        control_id="ctrl-fd",
        control_type=DERControlEnumType.freq_droop,
        freq_droop=FREQ_DROOP,
    )
    assert r is not None and r.status == DERControlStatusEnumType.accepted

    drain(pushed)
    assert "FreqDroop" in latest_directive_types(pushed)


# -----------------------------------------------------------------------------
# CSMS-facing DER message FSM (R04.FR.01/30/41/44/45 and ReportDERControl),
# driven through the inverted grid_support enable path: a published capability
# enables the controller, then the CSMS exercises Set/Get/Clear DER control.
# -----------------------------------------------------------------------------

@grid_support_markers
async def test_set_der_control_unsupported_type_not_supported(
    central_system_v21: CentralSystem,
    test_controller: TestController,
    test_utility: TestUtility,
    probe_grid_support: ProbeModule,
):
    """R04.FR.01: with the controller enabled but the controlType absent from
    ModesSupported, SetDERControl returns NotSupported.
    """
    provide_grid_support(probe_grid_support)
    probe_grid_support.start()
    await probe_grid_support.wait_to_be_ready()

    charge_point_v21 = await central_system_v21.wait_for_chargepoint(
        wait_for_bootnotification=True
    )

    # WattVar deliberately omitted from the declared supported types.
    await enable_der(
        probe_grid_support, charge_point_v21, supported_types=["FreqDroop", "VoltWatt"]
    )

    watt_var_curve = DERCurveType(
        curve_data=[DERCurvePointsType(x=100.0, y=50.0)],
        priority=0,
        y_unit=DERUnitEnumType.pct_max_var,
    )
    r = await charge_point_v21.set_der_control_req(
        is_default=True,
        control_id="ctrl-wv",
        control_type=DERControlEnumType.watt_var,
        curve=watt_var_curve,
    )
    assert r is not None and r.status == DERControlStatusEnumType.not_supported


@grid_support_markers
async def test_get_der_control_no_controls_not_found(
    central_system_v21: CentralSystem,
    test_controller: TestController,
    test_utility: TestUtility,
    probe_grid_support: ProbeModule,
):
    """R04.FR.30: GetDERControl when no matching controls exist returns NotFound."""
    provide_grid_support(probe_grid_support)
    probe_grid_support.start()
    await probe_grid_support.wait_to_be_ready()

    charge_point_v21 = await central_system_v21.wait_for_chargepoint(
        wait_for_bootnotification=True
    )

    await enable_der(probe_grid_support, charge_point_v21)

    r = await charge_point_v21.get_der_control_req(
        request_id=42,
        control_type=DERControlEnumType.freq_droop,
    )
    assert r is not None and r.status == DERControlStatusEnumType.not_found


@grid_support_markers
async def test_clear_der_control_no_match_not_found(
    central_system_v21: CentralSystem,
    test_controller: TestController,
    test_utility: TestUtility,
    probe_grid_support: ProbeModule,
):
    """R04.FR.41: ClearDERControl when no matching controls exist returns NotFound."""
    provide_grid_support(probe_grid_support)
    probe_grid_support.start()
    await probe_grid_support.wait_to_be_ready()

    charge_point_v21 = await central_system_v21.wait_for_chargepoint(
        wait_for_bootnotification=True
    )

    await enable_der(probe_grid_support, charge_point_v21)

    r = await charge_point_v21.clear_der_control_req(
        is_default=True,
        control_type=DERControlEnumType.freq_droop,
    )
    assert r is not None and r.status == DERControlStatusEnumType.not_found


@grid_support_markers
async def test_clear_by_type_then_clear_all(
    central_system_v21: CentralSystem,
    test_controller: TestController,
    test_utility: TestUtility,
    probe_grid_support: ProbeModule,
):
    """TC_R_102 (R04.FR.30/44/45): set two defaults of distinct types, clear one by
    type, verify the other survives, then clear all and verify the table is empty.
    """
    provide_grid_support(probe_grid_support)
    probe_grid_support.start()
    await probe_grid_support.wait_to_be_ready()

    charge_point_v21 = await central_system_v21.wait_for_chargepoint(
        wait_for_bootnotification=True
    )

    await enable_der(probe_grid_support, charge_point_v21)

    r = await charge_point_v21.set_der_control_req(
        is_default=True,
        control_id="ctrl-default-fd",
        control_type=DERControlEnumType.freq_droop,
        freq_droop=FREQ_DROOP,
    )
    assert r.status == DERControlStatusEnumType.accepted

    volt_watt_curve = DERCurveType(
        curve_data=[DERCurvePointsType(x=240.0, y=100.0)],
        priority=1,
        y_unit=DERUnitEnumType.pct_maxw,
    )
    r = await charge_point_v21.set_der_control_req(
        is_default=True,
        control_id="ctrl-default-vw",
        control_type=DERControlEnumType.volt_watt,
        curve=volt_watt_curve,
    )
    assert r.status == DERControlStatusEnumType.accepted

    # Both rows exist.
    r_get = await charge_point_v21.get_der_control_req(
        request_id=1, is_default=True, control_type=DERControlEnumType.freq_droop
    )
    assert r_get.status == DERControlStatusEnumType.accepted
    r_get = await charge_point_v21.get_der_control_req(
        request_id=2, is_default=True, control_type=DERControlEnumType.volt_watt
    )
    assert r_get.status == DERControlStatusEnumType.accepted

    # Clear by type=FreqDroop; VoltWatt survives.
    r_clear = await charge_point_v21.clear_der_control_req(
        is_default=True, control_type=DERControlEnumType.freq_droop
    )
    assert r_clear.status == DERControlStatusEnumType.accepted
    r_get = await charge_point_v21.get_der_control_req(
        request_id=3, is_default=True, control_type=DERControlEnumType.freq_droop
    )
    assert r_get.status == DERControlStatusEnumType.not_found
    r_get = await charge_point_v21.get_der_control_req(
        request_id=4, is_default=True, control_type=DERControlEnumType.volt_watt
    )
    assert r_get.status == DERControlStatusEnumType.accepted

    # Clear all defaults; the table is empty.
    r_clear = await charge_point_v21.clear_der_control_req(is_default=True)
    assert r_clear.status == DERControlStatusEnumType.accepted
    r_get = await charge_point_v21.get_der_control_req(
        request_id=5, is_default=True, control_type=DERControlEnumType.volt_watt
    )
    assert r_get.status == DERControlStatusEnumType.not_found


@grid_support_markers
async def test_get_emits_report_der_control(
    central_system_v21: CentralSystem,
    test_controller: TestController,
    test_utility: TestUtility,
    probe_grid_support: ProbeModule,
):
    """TC_R_104 (R04.FR.32-33): GetDERControl(Accepted) is followed by an outbound
    ReportDERControl carrying the stored control. Regression guard for a libocpp
    bug where ReportDERControl was missing from the message-type enum.
    """
    provide_grid_support(probe_grid_support)
    probe_grid_support.start()
    await probe_grid_support.wait_to_be_ready()

    charge_point_v21 = await central_system_v21.wait_for_chargepoint(
        wait_for_bootnotification=True
    )

    await enable_der(probe_grid_support, charge_point_v21)

    r_set = await charge_point_v21.set_der_control_req(
        is_default=True,
        control_id="ctrl-r104",
        control_type=DERControlEnumType.freq_droop,
        freq_droop=FREQ_DROOP,
    )
    assert r_set.status == DERControlStatusEnumType.accepted

    r_get = await charge_point_v21.get_der_control_req(
        request_id=104, is_default=True, control_type=DERControlEnumType.freq_droop
    )
    assert r_get.status == DERControlStatusEnumType.accepted

    assert await wait_for_and_validate(
        test_utility,
        charge_point_v21,
        "ReportDERControl",
        {"requestId": 104},
    )


# -----------------------------------------------------------------------------
# A CSMS DER disable survives a reboot.
#
# DERCtrlr Enabled=false persists in the device model, but the module's in-memory
# disable state must be restored on boot, or a persisted directive is re-pushed to
# a disabled EVSE after restart.
# -----------------------------------------------------------------------------

@grid_support_markers
async def test_der_disable_survives_reboot(
    central_system_v21: CentralSystem,
    test_controller: TestController,
    test_utility: TestUtility,
    probe_grid_support: ProbeModule,
    everest_core,
):
    """A CSMS DER disable survives a reboot: after Enabled=false and a restart,
    a previously-persisted directive is not re-pushed to the disabled device.

    Enable DER, set a default directive, disable via SetVariables Enabled=false
    (the device is cleared), reboot, re-declare the capability, and assert the
    device does not get the persisted directive back.
    """
    pushed = provide_grid_support(probe_grid_support)
    probe_grid_support.start()
    await probe_grid_support.wait_to_be_ready()

    charge_point_v21 = await central_system_v21.wait_for_chargepoint(
        wait_for_bootnotification=True
    )

    await enable_der(probe_grid_support, charge_point_v21)

    r_set = await charge_point_v21.set_der_control_req(
        is_default=True,
        control_id="ctrl-reboot",
        control_type=DERControlEnumType.freq_droop,
        freq_droop=FREQ_DROOP,
    )
    assert r_set is not None and r_set.status == DERControlStatusEnumType.accepted

    # The directive reaches the enabled device.
    drain(pushed)
    assert "FreqDroop" in latest_directive_types(pushed)

    # CSMS disables the DER controller; the device is cleared.
    disable = await charge_point_v21.set_variables_req(
        set_variable_data=[
            SetVariableDataType(
                attribute_value="false",
                attribute_type=AttributeEnumType.actual,
                component=ComponentType(
                    name="DCDERCtrlr", evse=EVSEType(id=DER_EVSE_ID)
                ),
                variable=VariableType(name="Enabled"),
            )
        ]
    )
    assert disable.set_variable_result[0]["attribute_status"] == "Accepted"
    drain(pushed)
    assert "FreqDroop" not in latest_directive_types(pushed)

    # Reboot: only the process bounces; the device model DB (Enabled=false and the
    # persisted directive) survives.
    test_controller.stop()
    await asyncio.sleep(1)
    test_controller.start()

    # The restarted manager waits for the standalone probe, whose C++ module is
    # bound to the pre-reboot manager, so re-create it against the new session
    # before waiting for the charge point.
    probe_after = ProbeModule(everest_core.get_runtime_session())
    pushed = provide_grid_support(probe_after)
    probe_after.start()
    await probe_after.wait_to_be_ready()

    charge_point_v21 = await central_system_v21.wait_for_chargepoint(
        wait_for_bootnotification=True
    )

    # The device re-declares its capability, re-registering the EVSE so the module
    # pushes its active set again.
    await enable_der(probe_after, charge_point_v21)

    # The persisted directive must not return: the DER block is gated on the
    # persisted Enabled=false, so the disabled EVSE receives an empty set.
    drain(pushed)
    assert "FreqDroop" not in latest_directive_types(pushed)

    # Re-enabling proves the directive was persisted and merely suppressed: once
    # Enabled=true, the stored control is rebuilt and pushed to the device again.
    reenable = await charge_point_v21.set_variables_req(
        set_variable_data=[
            SetVariableDataType(
                attribute_value="true",
                attribute_type=AttributeEnumType.actual,
                component=ComponentType(
                    name="DCDERCtrlr", evse=EVSEType(id=DER_EVSE_ID)
                ),
                variable=VariableType(name="Enabled"),
            )
        ]
    )
    assert reenable.set_variable_result[0]["attribute_status"] == "Accepted"
    drain(pushed)
    assert "FreqDroop" in latest_directive_types(pushed)
