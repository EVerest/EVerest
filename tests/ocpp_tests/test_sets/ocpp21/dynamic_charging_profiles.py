# SPDX-License-Identifier: Apache-2.0
# Copyright Pionix GmbH and Contributors to EVerest
"""K28 Dynamic Charging Profile integration test.

Verifies that an UpdateDynamicScheduleRequest applied to a Dynamic profile
propagates through libocpp into the composite schedule returned to the CSMS.

K28 functional-requirement coverage lives in libocpp_unit_tests
(lib/everest/ocpp/tests/lib/ocpp/v21/functional_blocks/test_smart_charging.cpp).
"""

import asyncio
import logging
import math
from datetime import datetime, timezone

import pytest

# fmt: off
from everest.testing.core_utils.controller.test_controller_interface import TestController

from ocpp.v21 import call as call21
from ocpp.v21 import call_result as call_result21
from ocpp.v21.enums import *
from ocpp.v21.datatypes import *
from ocpp.routing import on, create_route_map
from everest.testing.ocpp_utils.fixtures import *
from everest_test_utils import *  # overrides v21 Action with v16; reimport below
from ocpp.v21.enums import (
    Action,
    ChargingProfileKindEnumType,
    ChargingProfilePurposeEnumType,
    ChargingProfileStatusEnumType,
    ChargingRateUnitEnumType,
    ConnectorStatusEnumType,
    GenericStatusEnumType,
    OperationModeEnumType,
)
from validations import validate_status_notification_201
from everest.testing.core_utils._configuration.libocpp_configuration_helper import (
    GenericOCPP2XConfigAdjustment,
    OCPP2XConfigVariableIdentifier,
)
from everest.testing.ocpp_utils.charge_point_utils import wait_for_and_validate, TestUtility
# fmt: on

log = logging.getLogger("dynamicChargingProfilesTest")


# --------------------------------------------------------------------------- #
# Common config marker                                                        #
# --------------------------------------------------------------------------- #
# K01.FR.121 rejects every Dynamic profile when SupportsDynamicProfiles is
# false. The default ships false (lib/everest/ocpp/config/common/component_config/
# standardized/SmartChargingCtrlr.json), so every K28 test toggles it on.
_DYNAMIC_PROFILES_CONFIG = GenericOCPP2XConfigAdjustment(
    [
        (
            OCPP2XConfigVariableIdentifier(
                "InternalCtrlr", "SupportedOcppVersions", "Actual"
            ),
            "ocpp2.1",
        ),
        (
            OCPP2XConfigVariableIdentifier(
                "SmartChargingCtrlr", "SupportsDynamicProfiles", "Actual"
            ),
            "true",
        ),
    ]
)


# --------------------------------------------------------------------------- #
# Helpers                                                                     #
# --------------------------------------------------------------------------- #
def _build_dynamic_profile(
    profile_id: int,
    *,
    purpose: ChargingProfilePurposeEnumType = ChargingProfilePurposeEnumType.tx_default_profile,
    limit: float | None = 10000.0,
    setpoint: float | None = None,
    operation_mode: OperationModeEnumType | None = None,
    duration: int | None = None,
    dyn_update_interval: int | None = None,
    transaction_id: str | None = None,
    schedule_periods: list[ChargingSchedulePeriodType] | None = None,
    kind: ChargingProfileKindEnumType = ChargingProfileKindEnumType.dynamic,
    stack_level: int = 1,
) -> ChargingProfileType:
    """Build a Dynamic ChargingProfile with sensible K28 defaults.

    Defaults to ChargingOnly operationMode, which per the OCPP 2.1 limits/
    setpoints table requires `limit`. Pass operation_mode=CentralSetpoint plus
    setpoint=<value> to test the V2X CentralSetpoint flow (Q04).
    """
    if schedule_periods is None:
        period_kwargs: dict = {"start_period": 0}
        if setpoint is not None:
            period_kwargs["setpoint"] = setpoint
        if limit is not None:
            period_kwargs["limit"] = limit
        if operation_mode is not None:
            period_kwargs["operation_mode"] = operation_mode
        schedule_periods = [ChargingSchedulePeriodType(**period_kwargs)]

    schedule_kwargs: dict = {
        "id": profile_id,
        "charging_rate_unit": ChargingRateUnitEnumType.w,
        "charging_schedule_period": schedule_periods,
        "duration": duration,
    }
    # Absolute / Recurring schedules need a startSchedule; Dynamic does not.
    if kind == ChargingProfileKindEnumType.absolute:
        schedule_kwargs["start_schedule"] = datetime.now(timezone.utc).isoformat()
    schedule = ChargingScheduleType(**schedule_kwargs)

    profile_kwargs: dict = {
        "id": profile_id,
        "stack_level": stack_level,
        "charging_profile_purpose": purpose,
        "charging_profile_kind": kind,
        "charging_schedule": [schedule],
    }
    if dyn_update_interval is not None:
        profile_kwargs["dyn_update_interval"] = dyn_update_interval
        # K28 FR.08: dynUpdateTime sets the anchor for the next pull.
        profile_kwargs["dyn_update_time"] = datetime.now(timezone.utc).isoformat()
    if transaction_id is not None:
        profile_kwargs["transaction_id"] = transaction_id

    return ChargingProfileType(**profile_kwargs)


async def _wait_available(test_utility, charge_point):
    return await wait_for_and_validate(
        test_utility,
        charge_point,
        "StatusNotification",
        call21.StatusNotification(
            1, ConnectorStatusEnumType.available, 1, datetime.now().isoformat()
        ),
        validate_status_notification_201,
    )


async def _composite_schedule_first_period(charge_point, evse_id: int = 1, duration: int = 30):
    """Return the first ChargingSchedulePeriod of the composite schedule for the EVSE.

    The python-ocpp library deserializes nested dicts with snake_case keys, so
    we read `charging_schedule_period`, `operation_mode`, etc., on the returned
    dict — matching what dataclass-based responses expose to the test.
    """
    r: call_result21.GetCompositeSchedule = await charge_point.get_composite_schedule_req(
        duration=duration,
        evse_id=evse_id,
        charging_rate_unit=ChargingRateUnitEnumType.w,
    )
    assert r.status == GenericStatusEnumType.accepted, "GetCompositeSchedule rejected"
    assert r.schedule is not None, "Composite schedule missing in response"
    periods = (
        r.schedule.get("charging_schedule_period")
        or r.schedule.get("chargingSchedulePeriod")
    )
    assert periods, f"Composite schedule has no periods: {r.schedule}"
    return periods[0], r.schedule


# --------------------------------------------------------------------------- #
# 1) Push setpoint update changes the EVSE limit                              #
# --------------------------------------------------------------------------- #
@pytest.mark.asyncio
@pytest.mark.ocpp_version("ocpp2.1")
@pytest.mark.everest_core_config(
    get_everest_config_path_str("everest-config-ocpp201.yaml")
)
@pytest.mark.ocpp_config_adaptions(_DYNAMIC_PROFILES_CONFIG)
@pytest.mark.use_temporary_persistent_store
async def test_k28_push_setpoint_update_changes_evse_limit(
    central_system_v21: CentralSystem,
    test_controller: TestController,
    test_utility: TestUtility,
):
    """K28.FR.06/.09: UpdateDynamicSchedule with a new limit replaces the profile limit.

    Verified through GetCompositeSchedule, which reflects the active limit the
    SmartChargingHandler computes for the EVSE.
    """
    test_controller.start()
    charge_point_v21 = await central_system_v21.wait_for_chargepoint(
        wait_for_bootnotification=True
    )
    assert await _wait_available(test_utility, charge_point_v21)

    profile_id = 8001
    profile = _build_dynamic_profile(profile_id, limit=10000.0)
    set_resp: call_result21.SetChargingProfile = await charge_point_v21.set_charging_profile_req(
        evse_id=1, charging_profile=profile
    )
    assert set_resp.status == ChargingProfileStatusEnumType.accepted

    period, _ = await _composite_schedule_first_period(charge_point_v21)
    initial = period.get("limit", period.get("setpoint"))
    assert initial == pytest.approx(10000.0, abs=1.0), f"baseline composite was {initial}"

    update = call21.UpdateDynamicSchedule(
        charging_profile_id=profile_id,
        schedule_update=ChargingScheduleUpdateType(limit=5000.0),
    )
    upd_resp: call_result21.UpdateDynamicSchedule = await charge_point_v21.call(update)
    assert upd_resp.status == ChargingProfileStatusEnumType.accepted

    # Allow the handler a brief moment to surface the merged value.
    await asyncio.sleep(1.0)
    period, _ = await _composite_schedule_first_period(charge_point_v21)
    after = period.get("limit", period.get("setpoint"))
    assert after == pytest.approx(5000.0, abs=1.0), f"updated composite was {after}"


# --------------------------------------------------------------------------- #
# 2) Rapid back-to-back updates converge to the LAST pushed limit             #
# --------------------------------------------------------------------------- #
@pytest.mark.asyncio
@pytest.mark.ocpp_version("ocpp2.1")
@pytest.mark.everest_core_config(
    get_everest_config_path_str("everest-config-ocpp201.yaml")
)
@pytest.mark.ocpp_config_adaptions(_DYNAMIC_PROFILES_CONFIG)
@pytest.mark.use_temporary_persistent_store
async def test_k28_rapid_updates_converge_to_last_limit(
    central_system_v21: CentralSystem,
    test_controller: TestController,
    test_utility: TestUtility,
):
    """K28: a rapid burst of UpdateDynamicSchedule messages converges on the LAST limit.

    Sends several updates back-to-back with distinct limits and asserts the composite
    schedule settles on the LAST pushed value, never an earlier (stale) one — an
    end-to-end guard that a rapid update burst is processed in order and does not
    strand an intermediate limit.

    This asserts via GetCompositeSchedule, which reflects libocpp's profile store
    (mutated synchronously by each UpdateDynamicSchedule) and is therefore independent
    of the OCPP201 recompute callback. The single-flight + coalescing serialization of
    that callback — which keeps a concurrent fire from applying a stale composite to the
    energy sink — is an EVerest-internal change carried by the production logic; this
    test guards the observable OCPP-level convergence, not the internal apply path.
    """
    test_controller.start()
    charge_point_v21 = await central_system_v21.wait_for_chargepoint(
        wait_for_bootnotification=True
    )
    assert await _wait_available(test_utility, charge_point_v21)

    profile_id = 8002
    profile = _build_dynamic_profile(profile_id, limit=10000.0)
    set_resp: call_result21.SetChargingProfile = await charge_point_v21.set_charging_profile_req(
        evse_id=1, charging_profile=profile
    )
    assert set_resp.status == ChargingProfileStatusEnumType.accepted

    # Fire a burst of distinct limits back-to-back; 9000 is pushed LAST.
    burst_limits = [6000.0, 7000.0, 8000.0, 9000.0]
    for limit in burst_limits:
        update = call21.UpdateDynamicSchedule(
            charging_profile_id=profile_id,
            schedule_update=ChargingScheduleUpdateType(limit=limit),
        )
        upd_resp: call_result21.UpdateDynamicSchedule = await charge_point_v21.call(update)
        assert upd_resp.status == ChargingProfileStatusEnumType.accepted

    final_limit = burst_limits[-1]
    # Poll for convergence: the coalesced recompute must settle on the LAST limit and
    # must never expose an intermediate/stale value as the final state.
    converged = None
    for _ in range(10):
        await asyncio.sleep(0.5)
        period, _schedule = await _composite_schedule_first_period(charge_point_v21)
        converged = period.get("limit", period.get("setpoint"))
        if converged == pytest.approx(final_limit, abs=1.0):
            break
    assert converged == pytest.approx(final_limit, abs=1.0), (
        f"final composite was {converged}, expected last pushed limit {final_limit}"
    )


