# SPDX-License-Identifier: Apache-2.0
# Copyright Pionix GmbH and Contributors to EVerest

"""Tests for the RsOpenLEADR module against a mock OpenADR 3.0 VTN.

The module polls the VTN every second in these tests and publishes the
resulting limits to a probe module implementing external_energy_limits.
"""

from datetime import datetime, timedelta, timezone
from typing import List, Optional, Tuple

import pytest

from everest.testing.core_utils.fixtures import *
from everest.testing.core_utils.everest_core import EverestCore

from openadr_fixtures import everest_config_strategies, mock_vtn
from mock_vtn import MockVtn, event
from openadr_test_utils import start_probe

CONFIG = "config-test-openadr-module-001.yaml"

# (timestamp, total_power_W, ac_max_current_A) of a schedule entry, as limited towards the root
Entry = Tuple[str, Optional[float], Optional[float]]


def now() -> datetime:
    return datetime.now(timezone.utc).replace(second=0, microsecond=0)


def ts(dt: datetime) -> str:
    """Timestamp as formatted by the module."""
    return dt.strftime("%Y-%m-%dT%H:%M:%S.000Z")


def entries(schedule: List[dict]) -> List[Entry]:
    def value(limits: dict, key: str) -> Optional[float]:
        return limits[key]["value"] if key in limits else None

    return [
        (e["timestamp"], value(e["limits_to_root"], "total_power_W"), value(e["limits_to_root"], "ac_max_current_A"))
        for e in schedule
    ]


def limits_after_now(schedule: List[dict]) -> List[Entry]:
    """Entries without the first one, whose timestamp is the time of publishing."""
    return entries(schedule)[1:]


def current_limit(schedule: List[dict]) -> Tuple[Optional[float], Optional[float]]:
    return entries(schedule)[0][1:]


@pytest.mark.asyncio
@pytest.mark.everest_core_config(CONFIG)
async def test_import_and_export_limits(everest_core: EverestCore, mock_vtn: MockVtn):
    t = now()
    mock_vtn.set_events([
        event("import", t - timedelta(minutes=1), timedelta(minutes=31), "IMPORT_CAPACITY_LIMIT", 7.0),
        event("export", t - timedelta(minutes=1), timedelta(minutes=31), "EXPORT_CAPACITY_LIMIT", 3.0),
    ])
    probe = await start_probe(everest_core)

    limits = await probe.wait_for(lambda l: len(l["schedule_import"]) > 1)

    assert current_limit(limits["schedule_import"]) == (7000.0, None)
    assert limits_after_now(limits["schedule_import"]) == [(ts(t + timedelta(minutes=30)), None, None)]
    assert current_limit(limits["schedule_export"]) == (3000.0, None)
    assert limits_after_now(limits["schedule_export"]) == [(ts(t + timedelta(minutes=30)), None, None)]
    assert limits["schedule_setpoints"] == []

    first = limits["schedule_import"][0]
    assert first["limits_to_root"]["total_power_W"]["source"] == "OpenADR"
    assert first["limits_to_leaves"] == first["limits_to_root"]


@pytest.mark.asyncio
@pytest.mark.everest_core_config(CONFIG)
async def test_requests_use_credentials_program_and_target_filter(everest_core: EverestCore, mock_vtn: MockVtn):
    probe = await start_probe(everest_core)
    await probe.wait_for()

    token_requests = mock_vtn.requests("/auth/token")
    assert token_requests, "module did not request a token"
    assert all(r.authorization.startswith("Basic ") for r in token_requests)

    event_requests = mock_vtn.requests("/events")
    assert event_requests, "module did not retrieve events"
    query = event_requests[-1].query
    assert query["programID"] == ["program-1"]
    assert query["targetType"] == ["GROUP"]
    assert query["targetValues"] == ["group-1"]
    assert event_requests[-1].authorization.startswith("Bearer ")


@pytest.mark.asyncio
@pytest.mark.everest_core_config(CONFIG)
async def test_higher_priority_event_takes_precedence(everest_core: EverestCore, mock_vtn: MockVtn):
    t = now()
    mock_vtn.set_events([
        event("long-low-priority", t - timedelta(minutes=1), timedelta(minutes=61), value=4.0, priority=10),
        event("short-high-priority", t + timedelta(minutes=10), timedelta(minutes=10), value=7.0, priority=1),
    ])
    probe = await start_probe(everest_core)

    limits = await probe.wait_for(lambda l: len(l["schedule_import"]) > 1)

    assert current_limit(limits["schedule_import"]) == (4000.0, None)
    assert limits_after_now(limits["schedule_import"]) == [
        (ts(t + timedelta(minutes=10)), 7000.0, None),
        (ts(t + timedelta(minutes=20)), 4000.0, None),
        (ts(t + timedelta(minutes=60)), None, None),
    ]


@pytest.mark.asyncio
@pytest.mark.everest_core_config(CONFIG)
async def test_units(everest_core: EverestCore, mock_vtn: MockVtn):
    t = now()
    mock_vtn.set_events([
        event("amps", t - timedelta(minutes=1), timedelta(minutes=11), value=16.0, units="AMPS"),
        event("no-unit", t + timedelta(minutes=30), timedelta(minutes=10), value=5.0, units=None),
        event("kva", t - timedelta(minutes=1), timedelta(minutes=11), "EXPORT_CAPACITY_LIMIT", 3.0, units="KVA"),
    ])
    probe = await start_probe(everest_core)

    limits = await probe.wait_for(lambda l: len(l["schedule_import"]) > 1)

    # AMPS become a current limit, no unit is interpreted as KW
    assert current_limit(limits["schedule_import"]) == (None, 16.0)
    assert limits_after_now(limits["schedule_import"]) == [
        (ts(t + timedelta(minutes=10)), None, None),
        (ts(t + timedelta(minutes=30)), 5000.0, None),
        (ts(t + timedelta(minutes=40)), None, None),
    ]
    # unsupported units are ignored
    assert entries(limits["schedule_export"])[0][1:] == (None, None)
    assert len(limits["schedule_export"]) == 1


@pytest.mark.asyncio
@pytest.mark.everest_core_config(CONFIG)
async def test_removed_events_clear_limits(everest_core: EverestCore, mock_vtn: MockVtn):
    t = now()
    mock_vtn.set_events([event("import", t - timedelta(minutes=1), timedelta(minutes=31))])
    probe = await start_probe(everest_core)
    await probe.wait_for(lambda l: current_limit(l["schedule_import"]) == (7000.0, None))

    mock_vtn.set_events([])

    limits = await probe.wait_for(lambda l: current_limit(l["schedule_import"]) == (None, None))
    assert len(limits["schedule_import"]) == 1
    assert len(limits["schedule_export"]) == 1


@pytest.mark.asyncio
@pytest.mark.everest_core_config(CONFIG)
async def test_unavailable_vtn_is_retried(everest_core: EverestCore, mock_vtn: MockVtn):
    t = now()
    mock_vtn.set_available(False)
    mock_vtn.set_events([event("import", t - timedelta(minutes=1), timedelta(minutes=31))])
    probe = await start_probe(everest_core)

    # without a VTN the module publishes nothing, but keeps retrying
    assert await probe.expect_none(timeout=3) is None
    assert len(mock_vtn.requests()) >= 2

    mock_vtn.set_available(True)

    limits = await probe.wait_for()
    assert current_limit(limits["schedule_import"]) == (7000.0, None)
