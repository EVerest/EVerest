# SPDX-License-Identifier: Apache-2.0
# Copyright Pionix GmbH and Contributors to EVerest

"""
End-to-end check that a CSMS dischargeLimit bounds the enforced export power of
a DC_BPT session served by OCPPmulti.

OCPPmulti sends each composite period's dischargeLimit to the EVSE energy sink
as an export limit. The EnergyManager takes the minimum of every limit and
setpoint on the path, so with ``limit 0`` and no setpoint the enforced
discharge is the tightest export bound, which must be the dischargeLimit. With
a setpoint tighter than the dischargeLimit, the setpoint binds instead.

A probe module observes the EVSE sink's ``energy_flow_request`` and the
EvseManager's ``enforced_limits``. It also calls OCPPmulti's ``ocpp_generic``
``set_variables`` to advertise the operation modes these profiles need:
EverestDeviceModelStorage provisions ``V2XChargingCtrlr.SupportedOperationModes``
read-only as ``ChargingOnly,Idle``, which a CSMS SetVariables cannot change.
"""

import asyncio
import logging
import queue
import time
from copy import deepcopy
from datetime import datetime
from typing import Callable, Dict, Optional

import pytest
import pytest_asyncio

# fmt: off
from everest.testing.core_utils.common import Requirement
from everest.testing.core_utils.controller.test_controller_interface import TestController
from everest.testing.core_utils.probe_module import ProbeModule
from everest.testing.core_utils import EverestConfigAdjustmentStrategy

from ocpp.v21 import call as call21
from ocpp.v21 import call_result as call_result21
from ocpp.routing import on, create_route_map
from everest.testing.ocpp_utils.fixtures import *
from everest_test_utils import *  # must come before v21 datatype re-imports
from everest.testing.core_utils._configuration.libocpp_configuration_helper import GenericOCPP2XConfigAdjustment
from everest.testing.ocpp_utils.charge_point_utils import wait_for_and_validate, wait_for_payload, TestUtility
from validations import validate_status_notification_201

from ocpp.v21.datatypes import (
    ChargingProfileType,
    ChargingSchedulePeriodType,
    ChargingScheduleType,
    ComponentType,
    EVSEType,
    GetVariableDataType,
    IdTokenInfoType,
    TransactionType,
    VariableType,
)
from ocpp.v21.enums import (
    Action,
    AttributeEnumType,
    AuthorizationStatusEnumType,
    ChargingProfileKindEnumType,
    ChargingProfilePurposeEnumType,
    ChargingProfileStatusEnumType,
    ChargingRateUnitEnumType,
    ConnectorStatusEnumType,
    EnergyTransferModeEnumType,
    GetVariableStatusEnumType,
    OperationModeEnumType,
)
# fmt: on

log = logging.getLogger("v2xDischargeLimitsTest")

EVSE_ID = 1
OCPP_EXTERNAL_LIMITS_SOURCE = "ocpp/OCPP_set_external_limits"
OCPP_SETPOINT_SOURCE = "OCPP"
SUPPORTED_OPERATION_MODES = "ChargingOnly,Idle,CentralSetpoint,ExternalSetpoint"
OBSERVATION_TIMEOUT_S = 30.0


class DcBptDischargeConfigurationAdjustment(EverestConfigAdjustmentStrategy):
    """Makes the EV prefer DC_BPT and OCPPmulti share composite schedules in W.

    The DCSupplySimulator is bidirectional by default, so EvseManager already
    offers DC_BPT; PyEvJosev lists DC first unless told otherwise.
    DelayOcppStart lets the V2X device model projection settle before boot.
    """

    def adjust_everest_configuration(self, everest_config: Dict) -> Dict:
        adjusted_config = deepcopy(everest_config)
        modules = adjusted_config["active_modules"]
        ocpp_config = modules["ocpp"].setdefault("config_module", {})
        ocpp_config["DelayOcppStart"] = 3000
        ocpp_config["RequestCompositeScheduleUnit"] = "W"
        modules["iso15118_car"].setdefault("config_module", {})[
            "supported_d20_energy_services"] = "DC_BPT,DC"
        return adjusted_config


def v2x_discharge_markers(func):
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
        DcBptDischargeConfigurationAdjustment()
    )(func)
    func = pytest.mark.probe_module(
        connections={
            "ocpp": [Requirement("ocpp", "ocpp_generic")],
            "evse_sink": [Requirement("evse_manager_1_ocpp_sink", "energy_grid")],
            "evse_manager": [Requirement("evse_manager", "evse")],
        }
    )(func)
    return func


@pytest_asyncio.fixture
async def probe(started_test_controller, everest_core) -> ProbeModule:
    return ProbeModule(everest_core.get_runtime_session())


async def wait_for_queued(
    values: "queue.Queue", predicate: Callable[[dict], bool], what: str
) -> dict:
    deadline = time.monotonic() + OBSERVATION_TIMEOUT_S
    last = None
    while time.monotonic() < deadline:
        try:
            last = values.get_nowait()
        except queue.Empty:
            await asyncio.sleep(0.2)
            continue
        if predicate(last):
            return last
    raise AssertionError(
        f"{what} not seen within {OBSERVATION_TIMEOUT_S}s; last value: {last}"
    )


def sink_export_power(flow_request: dict) -> Optional[dict]:
    export = flow_request.get("schedule_export") or []
    if not export:
        return None
    return export[0].get("limits_to_leaves", {}).get("total_power_W")


def enforced_power(enforced_limits: dict) -> Optional[dict]:
    return enforced_limits.get("limits_root_side", {}).get("total_power_W")


async def advertise_operation_modes(probe: ProbeModule, charge_point) -> None:
    request = {
        "component_variable": {
            "component": {"name": "V2XChargingCtrlr", "evse": {"id": EVSE_ID}},
            "variable": {"name": "SupportedOperationModes"},
        },
        "value": SUPPORTED_OPERATION_MODES,
    }
    # OCPPmulti rejects set_variables until its startup event queue has drained
    result = None
    deadline = time.monotonic() + OBSERVATION_TIMEOUT_S
    while time.monotonic() < deadline:
        result = await probe.call_command(
            "ocpp", "set_variables", {"requests": [request], "source": "test"}
        )
        if result and result[0]["status"] == "Accepted":
            break
        await asyncio.sleep(0.5)
    assert result and result[0]["status"] == "Accepted", f"set_variables result: {result}"

    r: call_result21.GetVariables = await charge_point.get_variables_req(
        get_variable_data=[
            GetVariableDataType(
                component=ComponentType(name="V2XChargingCtrlr", evse=EVSEType(id=EVSE_ID)),
                variable=VariableType(name="SupportedOperationModes"),
                attribute_type=AttributeEnumType.actual,
            )
        ]
    )
    read_back = r.get_variable_result[0]
    assert read_back["attribute_status"] == GetVariableStatusEnumType.accepted
    assert read_back["attribute_value"] == SUPPORTED_OPERATION_MODES


async def start_dc_bpt_session(
    central_system_v21: CentralSystem,
    test_controller: TestController,
    test_utility: TestUtility,
    probe: ProbeModule,
):
    """Boots OCPP 2.1, advertises the V2X operation modes and starts a DC_BPT
    transaction. Returns the charge point, the transaction id and the queues of
    the sink's energy_flow_request and the EvseManager's enforced_limits."""
    flow_requests = probe.subscribe_variable_to_queue("evse_sink", "energy_flow_request")
    enforced_limits = probe.subscribe_variable_to_queue("evse_manager", "enforced_limits")
    probe.start()
    await probe.wait_to_be_ready()

    charge_point_v21 = await central_system_v21.wait_for_chargepoint(wait_for_bootnotification=True)
    assert await wait_for_and_validate(
        test_utility,
        charge_point_v21,
        "StatusNotification",
        call21.StatusNotification(
            EVSE_ID, ConnectorStatusEnumType.available, 1, datetime.now().isoformat()
        ),
        validate_status_notification_201,
    )

    await advertise_operation_modes(probe, charge_point_v21)

    @on(Action.authorize)
    def on_authorize(**kwargs):
        return call_result21.Authorize(
            id_token_info=IdTokenInfoType(status=AuthorizationStatusEnumType.accepted),
            allowed_energy_transfer=[EnergyTransferModeEnumType.dc_bpt],
        )

    setattr(charge_point_v21, "on_authorize", on_authorize)
    central_system_v21.chargepoint.route_map = create_route_map(central_system_v21.chargepoint)

    test_controller.plug_in_dc_iso()
    test_controller.swipe("8BADF00D")

    started = call21.TransactionEvent(
        **await wait_for_payload(
            test_utility, charge_point_v21, "TransactionEvent", {"eventType": "Started"}
        )
    )
    transaction = TransactionType(**started.transaction_info)

    assert await wait_for_and_validate(
        test_utility,
        charge_point_v21,
        "NotifyEVChargingNeeds",
        {"evseId": EVSE_ID, "chargingNeeds": {"requestedEnergyTransfer": "DC_BPT"}},
    )

    return charge_point_v21, transaction.transaction_id, flow_requests, enforced_limits


async def set_tx_profile(charge_point, transaction_id: str, **period) -> None:
    profile = ChargingProfileType(
        id=2934,
        stack_level=0,
        charging_profile_purpose=ChargingProfilePurposeEnumType.tx_profile,
        charging_profile_kind=ChargingProfileKindEnumType.relative,
        transaction_id=transaction_id,
        charging_schedule=[
            ChargingScheduleType(
                id=2934,
                charging_rate_unit=ChargingRateUnitEnumType.w,
                charging_schedule_period=[ChargingSchedulePeriodType(start_period=0, **period)],
            )
        ],
    )
    r: call_result21.SetChargingProfile = await charge_point.set_charging_profile_req(
        evse_id=EVSE_ID, charging_profile=profile
    )
    assert r.status == ChargingProfileStatusEnumType.accepted, f"SetChargingProfile: {r}"


async def wait_for_energy_transfer(charge_point, test_utility: TestUtility) -> None:
    assert await wait_for_and_validate(
        test_utility,
        charge_point,
        "TransactionEvent",
        {"eventType": "Updated", "transactionInfo": {"chargingState": "Charging"}},
        timeout=60,
    )


def drain(values: "queue.Queue") -> None:
    while not values.empty():
        values.get_nowait()


async def stop_session(charge_point, test_controller: TestController, test_utility: TestUtility) -> None:
    # Only once energy flows: stopped during cable check, the d20 DC EV keeps
    # sending DcCableCheckReq and the transaction never ends
    test_controller.swipe("8BADF00D")
    assert await wait_for_and_validate(
        test_utility, charge_point, "TransactionEvent", {"eventType": "Ended"}
    )


async def expect_sink_export(flow_requests: "queue.Queue", power_W: float) -> None:
    def matches(flow_request):
        export = sink_export_power(flow_request)
        return (
            export is not None
            and export["source"] == OCPP_EXTERNAL_LIMITS_SOURCE
            and export["value"] == pytest.approx(power_W)
        )

    seen = await wait_for_queued(flow_requests, matches, f"sink export of {power_W} W from OCPP")
    log.info(f"Sink export: {sink_export_power(seen)}")


def enforced_match(power_W: float, sources_match: Callable[[list], bool]) -> Callable[[dict], bool]:
    def matches(enforced_limits):
        power = enforced_power(enforced_limits)
        return (
            power is not None
            and power["value"] == pytest.approx(power_W)
            and sources_match(power["source"].split(","))
        )

    return matches


async def expect_enforced(enforced_limits: "queue.Queue", matches: Callable[[dict], bool], what: str) -> None:
    seen = await wait_for_queued(enforced_limits, matches, what)
    log.info(f"Seen {what}: {enforced_power(seen)}")


@v2x_discharge_markers
async def test_discharge_limit_binds_export(
    central_system_v21: CentralSystem,
    test_controller: TestController,
    test_utility: TestUtility,
    probe: ProbeModule,
):
    """Without a setpoint, the CSMS dischargeLimit is the enforced export."""
    charge_point_v21, transaction_id, flow_requests, enforced_limits = await start_dc_bpt_session(
        central_system_v21, test_controller, test_utility, probe
    )

    await set_tx_profile(
        charge_point_v21,
        transaction_id,
        operation_mode=OperationModeEnumType.external_setpoint,
        limit=0,
        discharge_limit=-2000,
    )

    await expect_sink_export(flow_requests, 2000.0)

    from_discharge_limit = enforced_match(-2000.0, lambda sources: OCPP_EXTERNAL_LIMITS_SOURCE in sources)
    what = "enforced -2000 W from the discharge limit"
    await expect_enforced(enforced_limits, from_discharge_limit, what)

    await wait_for_energy_transfer(charge_point_v21, test_utility)
    drain(enforced_limits)
    await expect_enforced(enforced_limits, from_discharge_limit, f"{what} during energy transfer")

    await stop_session(charge_point_v21, test_controller, test_utility)


@v2x_discharge_markers
async def test_setpoint_tighter_than_discharge_limit(
    central_system_v21: CentralSystem,
    test_controller: TestController,
    test_utility: TestUtility,
    probe: ProbeModule,
):
    """A setpoint inside the dischargeLimit binds, while the sink still carries
    the dischargeLimit as its export limit."""
    charge_point_v21, transaction_id, flow_requests, enforced_limits = await start_dc_bpt_session(
        central_system_v21, test_controller, test_utility, probe
    )

    await set_tx_profile(
        charge_point_v21,
        transaction_id,
        operation_mode=OperationModeEnumType.central_setpoint,
        limit=0,
        discharge_limit=-2000,
        setpoint=-1500,
    )

    await expect_sink_export(flow_requests, 2000.0)

    from_setpoint = enforced_match(-1500.0, lambda sources: sources == [OCPP_SETPOINT_SOURCE])
    what = "enforced -1500 W from the setpoint"
    await expect_enforced(enforced_limits, from_setpoint, what)

    await wait_for_energy_transfer(charge_point_v21, test_utility)
    drain(enforced_limits)
    await expect_enforced(enforced_limits, from_setpoint, f"{what} during energy transfer")

    await stop_session(charge_point_v21, test_controller, test_utility)
