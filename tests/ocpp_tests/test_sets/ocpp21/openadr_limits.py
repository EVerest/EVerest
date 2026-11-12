# SPDX-License-Identifier: Apache-2.0
# Copyright Pionix GmbH and Contributors to EVerest
"""Limits from OpenADR (RsOpenLEADR) and OCPP 2.1 (OCPPmulti) applied together.

Each source sets its limits on its own EnergyNode on the path to connector_1
(see everest-config-ocpp21-openadr.yaml), so the EnergyManager enforces the
most restrictive of both. A probe module observes the enforced limits of
connector_1, the RsOpenLEADR module retrieves events from a mock VTN.
"""

import asyncio
import logging
import queue
import sys
import time
from datetime import datetime, timedelta, timezone
from pathlib import Path
from typing import Callable, Optional, Tuple

import pytest
import pytest_asyncio

from everest.testing.core_utils.common import Requirement
from everest.testing.core_utils.controller.test_controller_interface import TestController
from everest.testing.core_utils.probe_module import ProbeModule

from ocpp.v21 import call_result as call_result21
from everest.testing.ocpp_utils.fixtures import *
from everest_test_utils import *  # must come before v21 datatype re-imports
from everest.testing.core_utils._configuration.libocpp_configuration_helper import (
    GenericOCPP2XConfigAdjustment,
    OCPP2XConfigVariableIdentifier,
)

from ocpp.v21.datatypes import ChargingProfileType, ChargingSchedulePeriodType, ChargingScheduleType
from ocpp.v21.enums import (
    ChargingProfileKindEnumType,
    ChargingProfilePurposeEnumType,
    ChargingProfileStatusEnumType,
    ChargingRateUnitEnumType,
)

sys.path.insert(0, str(Path(__file__).parents[3] / "openadr_tests"))
from mock_vtn import MockVtn, event  # noqa: E402
from openadr_test_utils import VtnStrategy  # noqa: E402

log = logging.getLogger("openadrLimitsTest")

EVSE_ID = 1
PROFILE_ID = 4711
OCPP_SOURCE = "ocpp/OCPP_set_external_limits"
OPENADR_SOURCE = "OpenADR"
OBSERVATION_TIMEOUT_S = 30.0


@pytest.fixture
def mock_vtn():
    vtn = MockVtn()
    vtn.start()
    yield vtn
    vtn.stop()


@pytest.fixture
def everest_config_strategies(everest_config_strategies, mock_vtn):
    """Extends the OCPP test strategies to point RsOpenLEADR to the mock VTN."""
    return everest_config_strategies + [VtnStrategy(mock_vtn.url)]


@pytest_asyncio.fixture
async def enforced_limits(started_test_controller, everest_core) -> "queue.Queue":
    """Enforced limits published by connector_1."""
    probe = ProbeModule(everest_core.get_runtime_session())
    values: "queue.Queue" = queue.Queue()
    probe.subscribe_variable("evse_manager", "enforced_limits", values.put)
    probe.start()
    await probe.wait_to_be_ready()
    return values


def enforced_current(limits: dict) -> Optional[Tuple[float, list]]:
    current = limits.get("limits_root_side", {}).get("ac_max_current_A")
    if current is None:
        return None
    return current["value"], current["source"].split(",")


async def expect_enforced_current(values: "queue.Queue", current_A: float, source: str):
    """Waits until connector_1 enforces current_A, limited by source."""

    def matches(limits: dict) -> bool:
        current = enforced_current(limits)
        return current is not None and current[0] == pytest.approx(current_A) and source in current[1]

    deadline = time.monotonic() + OBSERVATION_TIMEOUT_S
    last = None
    while time.monotonic() < deadline:
        try:
            last = values.get_nowait()
        except queue.Empty:
            await asyncio.sleep(0.1)
            continue
        if matches(last):
            log.info(f"Enforced {enforced_current(last)}")
            return
    raise AssertionError(f"connector_1 did not enforce {current_A} A from {source}, last seen: {last}")


async def set_tx_default_profile(charge_point, limit_A: float):
    """Sets (or replaces) the TxDefaultProfile of EVSE 1 with a constant limit."""
    profile = ChargingProfileType(
        id=PROFILE_ID,
        stack_level=0,
        charging_profile_purpose=ChargingProfilePurposeEnumType.tx_default_profile,
        charging_profile_kind=ChargingProfileKindEnumType.absolute,
        charging_schedule=[
            ChargingScheduleType(
                id=PROFILE_ID,
                start_schedule=(datetime.now(timezone.utc) - timedelta(minutes=1)).isoformat(),
                charging_rate_unit=ChargingRateUnitEnumType.a,
                charging_schedule_period=[ChargingSchedulePeriodType(start_period=0, limit=limit_A)],
            )
        ],
    )
    r: call_result21.SetChargingProfile = await charge_point.set_charging_profile_req(
        evse_id=EVSE_ID, charging_profile=profile
    )
    assert r.status == ChargingProfileStatusEnumType.accepted, f"SetChargingProfile: {r}"


@pytest.mark.asyncio
@pytest.mark.ocpp_version("ocpp2.1")
@pytest.mark.everest_core_config("everest-config-ocpp21-openadr.yaml")
@pytest.mark.ocpp_config_adaptions(
    GenericOCPP2XConfigAdjustment(
        [(OCPP2XConfigVariableIdentifier("InternalCtrlr", "SupportedOcppVersions", "Actual"), "ocpp2.1")]
    )
)
@pytest.mark.use_temporary_persistent_store
@pytest.mark.probe_module(connections={"evse_manager": [Requirement("connector_1", "evse")]})
async def test_most_restrictive_of_openadr_and_ocpp_limit_applies(
    central_system_v21: CentralSystem,
    mock_vtn: MockVtn,
    enforced_limits: "queue.Queue",
):
    now = datetime.now(timezone.utc).replace(second=0, microsecond=0)
    mock_vtn.set_events([event("limit", now - timedelta(minutes=1), timedelta(hours=1), value=10.0, units="AMPS")])
    charge_point_v21 = await central_system_v21.wait_for_chargepoint(wait_for_bootnotification=True)

    # only OpenADR limits
    await expect_enforced_current(enforced_limits, 10.0, OPENADR_SOURCE)

    # OCPP limit is more restrictive
    await set_tx_default_profile(charge_point_v21, 6.0)
    await expect_enforced_current(enforced_limits, 6.0, OCPP_SOURCE)

    # OpenADR limit is more restrictive again, it is not overwritten by OCPP
    await set_tx_default_profile(charge_point_v21, 16.0)
    await expect_enforced_current(enforced_limits, 10.0, OPENADR_SOURCE)

    # without OpenADR events, only the OCPP limit is left
    mock_vtn.set_events([])
    await expect_enforced_current(enforced_limits, 16.0, OCPP_SOURCE)
