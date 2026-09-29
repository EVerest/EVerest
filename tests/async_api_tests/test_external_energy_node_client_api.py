# SPDX-License-Identifier: Apache-2.0
# Copyright Pionix GmbH and Contributors to EVerest
import json
import time
from datetime import datetime, timezone
from queue import Empty, Queue

import paho.mqtt.client as mqtt
import pytest

from everest.testing.core_utils.everest_core import EverestCore
from everest.testing.core_utils.fixtures import *
from everest.testing.core_utils.probe_module import ProbeModule

SERVER_ID = "server_m0_api"
STALE_TIMEOUT_S = 2
NS_PREFIX = f"{SERVER_ID}:"


def _utc_now_rfc3339() -> str:
    return datetime.now(timezone.utc).strftime("%Y-%m-%dT%H:%M:%S.000Z")


def _passthrough_entry() -> dict:
    return {
        "timestamp": _utc_now_rfc3339(),
        "limits_to_root": {},
        "limits_to_leaves": {},
    }


def _server_aggregate() -> dict:
    """An aggregate as external_energy_node_API publishes it on its e2m topic."""
    return {
        "uuid": SERVER_ID,
        "node_type": "Generic",
        "schedule_import": [_passthrough_entry()],
        "schedule_export": [_passthrough_entry()],
        "schedule_setpoints": [],
        "children": [
            {
                "uuid": "cp01",
                "node_type": "Evse",
                "children": [],
                "schedule_import": [_passthrough_entry()],
                "schedule_export": [],
                "schedule_setpoints": [],
            }
        ],
    }


def _enforce_limits(uuid: str) -> dict:
    return {
        "uuid": uuid,
        "valid_for": 60,
        "limits_root_side": {},
        "schedule": [],
    }


@pytest.fixture
def probe_module(everest_core: EverestCore) -> ProbeModule:
    everest_core.start(standalone_module='probe')
    return ProbeModule(everest_core.get_runtime_session())


def _subscribe_to_queue(client: mqtt.Client, topic: str) -> Queue:
    received = Queue()
    client.message_callback_add(topic, lambda _c, _u, msg: received.put(msg.payload))
    client.subscribe(topic)
    return received


@pytest.mark.asyncio
@pytest.mark.everest_core_config('probe-external-energy-node-client.yaml')
async def test_republish_namespaces_all_uuids(
    everest_core: EverestCore,
    probe_module: ProbeModule,
    connected_mqtt_client: mqtt.Client,
):
    """The server's aggregate must be republished locally with EVERY uuid in the
    tree prefixed by '{server_id}:' — energy-tree uuids are module ids, which are
    unique only per process, so an unprefixed remote 'cp01' could collide with a
    local 'cp01'."""
    probe_module.start()
    await probe_module.wait_to_be_ready(timeout=10.0)
    republished = probe_module.subscribe_variable_to_queue("energy_grid", "energy_flow_request")

    mqtt_prefix = everest_core.mqtt_external_prefix
    flow_topic = f"{mqtt_prefix}everest_api/1/external_energy_node/{SERVER_ID}/e2m/energy_flow_request"
    connected_mqtt_client.publish(flow_topic, json.dumps(_server_aggregate()))

    request = republished.get(timeout=10)
    assert request["uuid"] == f"{NS_PREFIX}{SERVER_ID}"
    assert request["children"][0]["uuid"] == f"{NS_PREFIX}cp01"
    # schedules pass through untouched
    assert len(request["schedule_import"]) == 1


@pytest.mark.asyncio
@pytest.mark.everest_core_config('probe-external-energy-node-client.yaml')
async def test_enforce_limits_strips_namespace_and_drops_foreign_uuids(
    everest_core: EverestCore,
    probe_module: ProbeModule,
    connected_mqtt_client: mqtt.Client,
):
    """enforce_limits from the site-level EnergyManager: only limits addressed to
    this server's namespace may cross the wire (with the prefix stripped); local
    EnergyNodes broadcast limits for ALL nodes to every child, and forwarding
    those would apply limits meant for local nodes to remote ones."""
    probe_module.start()
    await probe_module.wait_to_be_ready(timeout=10.0)

    mqtt_prefix = everest_core.mqtt_external_prefix
    limits_topic = f"{mqtt_prefix}everest_api/1/external_energy_node/{SERVER_ID}/m2e/enforce_limits"
    published = _subscribe_to_queue(connected_mqtt_client, limits_topic)

    # 1. Namespaced uuid -> forwarded with the prefix stripped.
    await probe_module.call_command("energy_grid", "enforce_limits",
                                    {"value": _enforce_limits(f"{NS_PREFIX}cp01")})
    forwarded = json.loads(published.get(timeout=10))
    assert forwarded["uuid"] == "cp01"

    # 2. A local (foreign) uuid -> dropped, nothing crosses the wire.
    await probe_module.call_command("energy_grid", "enforce_limits",
                                    {"value": _enforce_limits("local_cp01")})
    with pytest.raises(Empty):
        published.get(timeout=1.5)


@pytest.mark.asyncio
@pytest.mark.everest_core_config('probe-external-energy-node-client.yaml')
async def test_stale_server_subtree_is_withdrawn_and_recovers(
    everest_core: EverestCore,
    probe_module: ProbeModule,
    connected_mqtt_client: mqtt.Client,
):
    """If the server goes silent for longer than stale_timeout_s, the client must
    withdraw the remote subtree: publish a childless ZERO-limit aggregate under
    the same root uuid, so the site-level EnergyManager stops reserving budget
    for EVSEs it can no longer reach. A returning server resumes normal
    republishing."""
    probe_module.start()
    await probe_module.wait_to_be_ready(timeout=10.0)
    republished = probe_module.subscribe_variable_to_queue("energy_grid", "energy_flow_request")

    mqtt_prefix = everest_core.mqtt_external_prefix
    flow_topic = f"{mqtt_prefix}everest_api/1/external_energy_node/{SERVER_ID}/e2m/energy_flow_request"

    # 1. Server publishes once, then goes silent.
    connected_mqtt_client.publish(flow_topic, json.dumps(_server_aggregate()))
    request = republished.get(timeout=10)
    assert request["children"], "expected the live aggregate first"

    # 2. Past stale_timeout_s the zero-limit withdrawal must appear.
    deadline = time.time() + STALE_TIMEOUT_S + 5
    withdrawn = None
    while time.time() < deadline:
        candidate = republished.get(timeout=max(0.1, deadline - time.time()))
        current = candidate.get("schedule_import", [{}])[0].get("limits_to_root", {}).get("ac_max_current_A")
        if current is not None and current["value"] == 0.0:
            withdrawn = candidate
            break
    assert withdrawn is not None, "no zero-limit withdrawal published after the server went stale"
    assert withdrawn["uuid"] == f"{NS_PREFIX}{SERVER_ID}"
    assert withdrawn["children"] == []
    assert withdrawn["schedule_export"][0]["limits_to_root"]["ac_max_current_A"]["value"] == 0.0

    # 3. Server comes back -> normal republishing resumes.
    connected_mqtt_client.publish(flow_topic, json.dumps(_server_aggregate()))
    deadline = time.time() + 10
    while time.time() < deadline:
        candidate = republished.get(timeout=max(0.1, deadline - time.time()))
        if candidate.get("children"):
            assert candidate["children"][0]["uuid"] == f"{NS_PREFIX}cp01"
            break
    else:
        raise AssertionError("live aggregate not republished after the server returned")
