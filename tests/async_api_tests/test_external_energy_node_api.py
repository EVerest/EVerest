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
