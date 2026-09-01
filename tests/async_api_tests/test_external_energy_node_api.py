# SPDX-License-Identifier: Apache-2.0
# Copyright Pionix GmbH and Contributors to EVerest
import json
import time
from queue import Empty, Queue

import paho.mqtt.client as mqtt
import pytest

from everest.testing.core_utils.everest_core import EverestCore
from everest.testing.core_utils.fixtures import *
from everest.testing.core_utils.probe_module import ProbeModule

MODULE_ID = "external_energy_node_api"
TIMEOUT_S = 2


def _enforce_limits(uuid: str) -> dict:
    return {
        "uuid": uuid,
        "valid_for": 60,
        "limits_root_side": {},
        "schedule": [],
    }


def _utc_now_rfc3339() -> str:
    from datetime import datetime, timezone
    return datetime.now(timezone.utc).strftime("%Y-%m-%dT%H:%M:%S.000Z")


def _evse_energy_flow_request(uuid: str, max_current_A: float = 32.0) -> dict:
    """A minimal but realistic EVSE energy_flow_request, as EvseManager publishes it."""
    return {
        "uuid": uuid,
        "node_type": "Evse",
        "evse_state": "Charging",
        "priority_request": False,
        "children": [],
        "schedule_import": [
            {
                "timestamp": _utc_now_rfc3339(),
                "limits_to_root": {
                    "ac_max_current_A": {"value": max_current_A, "source": "test_root"},
                    "ac_max_phase_count": {"value": 3, "source": "test_phase"},
                    "ac_min_current_A": {"value": 6.0, "source": "test_min"},
                    "ac_min_phase_count": {"value": 1, "source": "test_minphase"},
                    "ac_number_of_active_phases": 3,
                },
                "limits_to_leaves": {
                    "ac_max_current_A": {"value": max_current_A, "source": "test_leave"},
                },
            }
        ],
        "schedule_export": [],
        "schedule_setpoints": [],
    }


def _subscribe_to_queue(client: mqtt.Client, topic: str) -> Queue:
    """Subscribe the paho client to a topic and push incoming payloads to a queue."""
    received = Queue()
    client.message_callback_add(topic, lambda _c, _u, msg: received.put(msg.payload))
    client.subscribe(topic)
    return received


def _await_nonzero_current(forwarded: Queue, uuid: str, timeout_s: float) -> dict:
    """Wait until an EnforcedLimits for uuid with ac_max_current_A > 0 arrives."""
    deadline = time.time() + timeout_s
    last = None
    while time.time() < deadline:
        try:
            value = forwarded.get(timeout=max(0.1, deadline - time.time()))
        except Empty:
            break
        if value["uuid"] != uuid:
            continue
        last = value
        current = value.get("limits_root_side", {}).get("ac_max_current_A")
        if current is not None and current["value"] > 0.0:
            return value
    raise AssertionError(f"no EnforcedLimits with nonzero ac_max_current_A for '{uuid}' "
                         f"within {timeout_s}s; last seen: {last}")


@pytest.fixture
def probe_module(everest_core: EverestCore) -> ProbeModule:
    everest_core.start(standalone_module='probe')
    return ProbeModule(everest_core.get_runtime_session())


@pytest.mark.asyncio
@pytest.mark.everest_core_config('probe-external-energy-node.yaml')
async def test_external_timeout_falls_back_to_internal_energy_manager(
    everest_core: EverestCore,
    probe_module: ProbeModule,
    connected_mqtt_client: mqtt.Client,
):
    """Regression test for the external_timeout_watchdog fallback.

    The fallback from external to internal control must trigger on timeout_s
    alone, independent of any local energy_flow_request activity — this test
    never publishes energy_flow_request, only enforce_limits.
    """
    forwarded = Queue()

    def on_enforce_limits(args):
        forwarded.put(args["value"])
        return None

    probe_module.implement_command("energy_consumer", "enforce_limits", on_enforce_limits)
    probe_module.start()
    await probe_module.wait_to_be_ready(timeout=10.0)

    mqtt_prefix = everest_core.mqtt_external_prefix
    external_topic = f"{mqtt_prefix}everest_api/1/external_energy_node/{MODULE_ID}/m2e/enforce_limits"

    # 1. External sends limits over the MQTT bridge -> routed to the local energy_consumer.
    connected_mqtt_client.publish(external_topic, json.dumps(_enforce_limits("external-1")))
    assert forwarded.get(timeout=5)["uuid"] == "external-1"

    # 2. While external is still active (well within timeout_s), limits sent via energy_grid
    #    (simulating the internal EnergyManager) must be discarded, not forwarded.
    await probe_module.call_command("energy_grid", "enforce_limits", {"value": _enforce_limits("internal-1")})
    with pytest.raises(Empty):
        forwarded.get(timeout=1)

    # 3. External goes silent past timeout_s -> the watchdog fires -> falls back to internal.
    time.sleep(TIMEOUT_S + 1)

    # 4. Limits sent via energy_grid are now forwarded, proving the fallback engaged.
    await probe_module.call_command("energy_grid", "enforce_limits", {"value": _enforce_limits("internal-2")})
    assert forwarded.get(timeout=5)["uuid"] == "internal-2"


@pytest.mark.asyncio
@pytest.mark.everest_core_config('probe-external-energy-node.yaml')
async def test_external_recovery_after_fallback(
    everest_core: EverestCore,
    probe_module: ProbeModule,
    connected_mqtt_client: mqtt.Client,
):
    """After a timeout-driven fallback to the internal EnergyManager, a returning
    external EnergyManager must take back priority: its limits are routed to the
    children again and internal limits go back to being discarded.
    """
    forwarded = Queue()

    def on_enforce_limits(args):
        forwarded.put(args["value"])
        return None

    probe_module.implement_command("energy_consumer", "enforce_limits", on_enforce_limits)
    probe_module.start()
    await probe_module.wait_to_be_ready(timeout=10.0)

    mqtt_prefix = everest_core.mqtt_external_prefix
    external_topic = f"{mqtt_prefix}everest_api/1/external_energy_node/{MODULE_ID}/m2e/enforce_limits"

    # 1. External connects and goes silent past timeout_s -> fallback to internal.
    connected_mqtt_client.publish(external_topic, json.dumps(_enforce_limits("external-1")))
    assert forwarded.get(timeout=5)["uuid"] == "external-1"
    time.sleep(TIMEOUT_S + 1)
    await probe_module.call_command("energy_grid", "enforce_limits", {"value": _enforce_limits("internal-1")})
    assert forwarded.get(timeout=5)["uuid"] == "internal-1"

    # 2. External comes back -> its limits are routed to the children again.
    connected_mqtt_client.publish(external_topic, json.dumps(_enforce_limits("external-2")))
    assert forwarded.get(timeout=5)["uuid"] == "external-2"

    # 3. External is active again -> internal limits are discarded again.
    await probe_module.call_command("energy_grid", "enforce_limits", {"value": _enforce_limits("internal-2")})
    with pytest.raises(Empty):
        forwarded.get(timeout=1)


@pytest.mark.asyncio
@pytest.mark.everest_core_config('probe-external-energy-node.yaml')
async def test_aggregation_merges_children_and_always_seeds_schedules(
    everest_core: EverestCore,
    probe_module: ProbeModule,
    connected_mqtt_client: mqtt.Client,
):
    """Regression test for the empty-schedule bug: the published aggregate must
    ALWAYS carry a non-empty schedule_import/schedule_export. An empty schedule is
    read by the EnergyManager optimizer as "0 A available" (Market.cpp:
    zero_schedule_req), which silently clamps every EVSE below the bridge to 0 A.

    Also covers the aggregation path itself: a child energy_flow_request must be
    merged (by uuid) into the aggregate published on the external e2m topic.
    """
    probe_module.implement_command("energy_consumer", "enforce_limits", lambda args: None)
    probe_module.start()
    await probe_module.wait_to_be_ready(timeout=10.0)

    mqtt_prefix = everest_core.mqtt_external_prefix
    flow_topic = f"{mqtt_prefix}everest_api/1/external_energy_node/{MODULE_ID}/e2m/energy_flow_request"
    received = _subscribe_to_queue(connected_mqtt_client, flow_topic)

    # Child publishes -> server merges and republishes the aggregate externally.
    probe_module.publish_variable("energy_consumer", "energy_flow_request",
                                  _evse_energy_flow_request("probe_evse"))

    deadline = time.time() + 10
    aggregate = None
    while time.time() < deadline:
        payload = received.get(timeout=max(0.1, deadline - time.time()))
        candidate = json.loads(payload)
        if any(child["uuid"] == "probe_evse" for child in candidate["children"]):
            aggregate = candidate
            break
    assert aggregate is not None, "aggregate with merged child never published on e2m topic"

    assert aggregate["uuid"] == MODULE_ID
    assert aggregate["node_type"] == "Generic"

    # THE critical assertion: never publish empty schedules.
    assert len(aggregate["schedule_import"]) >= 1
    assert len(aggregate["schedule_export"]) >= 1
    # Default config (fuse_limit_A = 0): the seeded entry must be a pure
    # pass-through, i.e. carry no limits at all.
    assert "ac_max_current_A" not in aggregate["schedule_import"][0]["limits_to_root"]

    # Merge-by-uuid: republishing the same child must update, not duplicate.
    probe_module.publish_variable("energy_consumer", "energy_flow_request",
                                  _evse_energy_flow_request("probe_evse", max_current_A=16.0))
    deadline = time.time() + 10
    while time.time() < deadline:
        payload = received.get(timeout=max(0.1, deadline - time.time()))
        candidate = json.loads(payload)
        children = [c for c in candidate["children"] if c["uuid"] == "probe_evse"]
        if children and children[0]["schedule_import"][0]["limits_to_root"]["ac_max_current_A"]["value"] == 16.0:
            assert len(children) == 1, "child was duplicated instead of merged"
            break
    else:
        raise AssertionError("updated child never appeared in the aggregate")


@pytest.mark.asyncio
@pytest.mark.everest_core_config('probe-external-energy-node.yaml')
async def test_malformed_enforce_limits_is_ignored(
    everest_core: EverestCore,
    probe_module: ProbeModule,
    connected_mqtt_client: mqtt.Client,
):
    """A malformed external enforce_limits payload must be dropped without
    forwarding anything and without killing the subscription."""
    forwarded = Queue()
    probe_module.implement_command("energy_consumer", "enforce_limits",
                                   lambda args: forwarded.put(args["value"]))
    probe_module.start()
    await probe_module.wait_to_be_ready(timeout=10.0)

    mqtt_prefix = everest_core.mqtt_external_prefix
    external_topic = f"{mqtt_prefix}everest_api/1/external_energy_node/{MODULE_ID}/m2e/enforce_limits"

    # 1. Garbage payloads: nothing may be forwarded.
    connected_mqtt_client.publish(external_topic, b"{not json at all")
    connected_mqtt_client.publish(external_topic, json.dumps({"uuid": "x"}))  # missing required fields
    with pytest.raises(Empty):
        forwarded.get(timeout=1.5)

    # 2. The module is still alive: a valid message is forwarded.
    connected_mqtt_client.publish(external_topic, json.dumps(_enforce_limits("still-alive")))
    assert forwarded.get(timeout=5)["uuid"] == "still-alive"


@pytest.mark.asyncio
@pytest.mark.everest_core_config('probe-external-energy-node.yaml')
async def test_enforce_limits_addressed_to_bridge_node_is_not_broadcast(
    everest_core: EverestCore,
    probe_module: ProbeModule,
    connected_mqtt_client: mqtt.Client,
):
    """A limit addressed to the bridge node itself cannot be enforced by any
    child, so it must not be broadcast to them (mirrors the EnergyNode uuid
    check) — but it must still count as external activity for the watchdog."""
    forwarded = Queue()
    probe_module.implement_command("energy_consumer", "enforce_limits",
                                   lambda args: forwarded.put(args["value"]))
    probe_module.start()
    await probe_module.wait_to_be_ready(timeout=10.0)

    mqtt_prefix = everest_core.mqtt_external_prefix
    external_topic = f"{mqtt_prefix}everest_api/1/external_energy_node/{MODULE_ID}/m2e/enforce_limits"

    # Addressed to the bridge node itself -> not forwarded to children...
    connected_mqtt_client.publish(external_topic, json.dumps(_enforce_limits(MODULE_ID)))
    with pytest.raises(Empty):
        forwarded.get(timeout=1.5)

    # ...but it DID arm external control: internal limits are now discarded.
    await probe_module.call_command("energy_grid", "enforce_limits", {"value": _enforce_limits("internal-1")})
    with pytest.raises(Empty):
        forwarded.get(timeout=1)


@pytest.mark.asyncio
@pytest.mark.everest_core_config('probe-external-energy-node-em.yaml')
async def test_internal_energy_manager_delivers_nonzero_current_through_bridge(
    everest_core: EverestCore,
    probe_module: ProbeModule,
):
    """End-to-end (internal side): a real EnergyManager sits above the bridge and
    a probe EVSE below it requests 32 A. The EVSE must be granted a NONZERO
    current — this is the test that fails when the bridge publishes an aggregate
    with empty schedules (optimizer reads that as 0 A available)."""
    forwarded = Queue()
    probe_module.implement_command("energy_consumer", "enforce_limits",
                                   lambda args: forwarded.put(args["value"]))
    probe_module.start()
    await probe_module.wait_to_be_ready(timeout=10.0)

    probe_module.publish_variable("energy_consumer", "energy_flow_request",
                                  _evse_energy_flow_request("probe_evse", max_current_A=32.0))

    value = _await_nonzero_current(forwarded, "probe_evse", timeout_s=15)
    assert value["limits_root_side"]["ac_max_current_A"]["value"] > 0.0


@pytest.mark.asyncio
@pytest.mark.everest_core_config('probe-external-energy-node-pair.yaml')
async def test_full_pair_delivers_power_across_the_wire(
    everest_core: EverestCore,
    probe_module: ProbeModule,
):
    """End-to-end (both instances): the site-level EnergyManager only sees the
    client, the probe EVSE only talks to the server, and client and server
    communicate exclusively over the external MQTT topics — exactly like two
    bridged processes in production.

    Path under test:
      probe EVSE (32 A request) -> server aggregate (+16 A local fuse) -> e2m ->
      client (uuids namespaced) -> site EnergyManager allocates -> enforce_limits
      (namespaced uuid) -> client (uuid stripped) -> m2e -> server (external takes
      priority, clamped to local fuse) -> probe EVSE.

    Asserts REAL power delivery: nonzero ampere at the EVSE, capped by the
    16 A fuse advertised at the bridge point.
    """
    forwarded = Queue()
    probe_module.implement_command("energy_consumer", "enforce_limits",
                                   lambda args: forwarded.put(args["value"]))
    probe_module.start()
    await probe_module.wait_to_be_ready(timeout=10.0)

    probe_module.publish_variable("energy_consumer", "energy_flow_request",
                                  _evse_energy_flow_request("probe_evse", max_current_A=32.0))

    value = _await_nonzero_current(forwarded, "probe_evse", timeout_s=20)
    granted = value["limits_root_side"]["ac_max_current_A"]["value"]
    assert 0.0 < granted <= 16.0, \
        f"granted {granted} A — must be nonzero and within the 16 A bridge fuse"
