# SPDX-License-Identifier: Apache-2.0
# Copyright Pionix GmbH and Contributors to EVerest
"""Unit tests for SimRegistry and its drivers."""

import json
import os
import sys
import textwrap
import threading
import time
import types
from unittest.mock import MagicMock

import pytest

# Ensure repo-local src/ is importable.
_PKG_SRC = os.path.normpath(
    os.path.join(os.path.dirname(__file__), "..", "src")
)
if _PKG_SRC not in sys.path:
    sys.path.insert(0, _PKG_SRC)

# Stub heavy everest_core and test_controller_interface so controller loads
# without booting libeverestpy.
_stub_everest_core_mod = types.ModuleType("everest.testing.core_utils.everest_core")


class _StubEverestCore:  # pragma: no cover
    pass


_stub_everest_core_mod.EverestCore = _StubEverestCore  # type: ignore[attr-defined]
sys.modules.setdefault("everest.testing.core_utils.everest_core", _stub_everest_core_mod)
# Another test module may have installed the stub first, so add to whichever
# one is registered. `_configuration.everest_environment_setup` imports
# Requirement from here and fixtures.py pulls that in, so without it the
# fixture cannot be imported without libeverestpy.
sys.modules["everest.testing.core_utils.everest_core"].Requirement = _StubEverestCore

_stub_tci_mod = types.ModuleType(
    "everest.testing.core_utils.controller.test_controller_interface"
)


class _StubTestController:  # pragma: no cover
    pass


_stub_tci_mod.TestController = _StubTestController  # type: ignore[attr-defined]
sys.modules.setdefault(
    "everest.testing.core_utils.controller.test_controller_interface", _stub_tci_mod
)

from everest.testing.core_utils.sim_registry import (  # noqa: E402
    EvControllerDriver,
    EvDriverSimDriver,
    NoSimulatorError,
    SimRegistry,
)
# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------

_CONFIG_YAML = textwrap.dedent("""\
    active_modules:
      ev_sim_1:
        module: EvController
        config_module:
          connector_id: 1
""")


@pytest.fixture
def config_path(tmp_path):
    p = tmp_path / "everest-config-test.yaml"
    p.write_text(_CONFIG_YAML)
    return p


@pytest.fixture
def mqtt_client():
    client = MagicMock()
    client.publish = MagicMock()
    return client


@pytest.fixture
def registry(mqtt_client, config_path):
    return SimRegistry(mqtt_client, "pfx/", config_path)


# ---------------------------------------------------------------------------
# SimRegistry accessor tests
# ---------------------------------------------------------------------------


def test_ev_by_connector_returns_ev_controller_driver(registry):
    drv = registry.ev_by_connector(1)
    assert isinstance(drv, EvControllerDriver)


# ---------------------------------------------------------------------------
# Readiness gate
#
# The gate exists because m2e commands published before EvController's `ready()`
# registers its subscriptions are dropped without a trace. Three properties are
# load-bearing and each was violated by the original implementation:
#
#   1. It must not spend its budget inside `start()`. Every OCPP test calls
#      `TestController.start()` from a running asyncio loop that also serves the
#      in-process CSMS, so a blocking wait there stops the charge point's
#      websocket handshake from being answered and the test dies much later as
#      an unrelated OCPP timeout.
#   2. It must be armed before EVerest boots. The heartbeat is not retained, so
#      a subscription registered after `ready()` catches a tick only by luck.
#   3. When readiness is genuinely absent it must fail, naming the instance,
#      rather than publish into a module that cannot receive.
# ---------------------------------------------------------------------------

_CONFIG_YAML_TWO_EV = textwrap.dedent("""\
    active_modules:
      ev_sim_1:
        module: EvController
        config_module:
          connector_id: 1
      ev_sim_2:
        module: EvController
        config_module:
          connector_id: 2
""")


class _FakeMqttClient:
    """Minimal paho stand-in that records subscriptions and can inject messages."""

    def __init__(self):
        self.subscriptions = []
        self.published = []
        self.events = []
        self._callbacks = {}
        self.autodeliver = set()
        # Default to a broker that is up, which is the ordinary case. Tests
        # that put the CONNACK handshake itself under test set this False.
        self.autoconnack = True
        self.auto_configure_ack = False
        # e2m/state payloads answered to each m2e/query_state, oldest first.
        self.state_replies = []

    # -- paho surface used by the registry ---------------------------------
    def subscribe(self, topic):
        self.subscriptions.append(topic)
        self.events.append(("subscribe", topic))
        return (0, 1)

    def message_callback_add(self, topic, callback):
        self._callbacks[topic] = callback

    def publish(self, topic, payload=None):
        self.published.append((topic, payload))
        self.events.append(("publish", topic))
        if self.auto_configure_ack and topic.endswith("/m2e/configure_session"):
            ack_topic = topic.replace("/m2e/configure_session", "/e2m/command_ack")
            self.deliver(ack_topic, b'{"command":"configure_session","status":"Accepted"}')
        if self.state_replies and topic.endswith("/m2e/query_state"):
            self.deliver(topic.replace("/m2e/query_state", "/e2m/state"), self.state_replies.pop(0))

    on_connect = None

    def connect(self, *_a, **_kw):
        self.events.append(("connect", None))
        if self.autoconnack:
            self.fire_connack()

    def fire_connack(self):
        self.events.append(("connack", None))
        if self.on_connect is not None:
            self.on_connect(self, None, {}, 0, None)

    def loop_start(self):
        self.events.append(("loop_start", None))

    def loop_stop(self):
        pass

    def disconnect(self):
        pass

    def _reset_sockets(self):
        pass

    # -- test helpers ------------------------------------------------------
    def deliver(self, topic, payload=b"1"):
        cb = self._callbacks.get(topic)
        assert cb is not None, f"no subscriber for {topic}; have {sorted(self._callbacks)}"
        msg = types.SimpleNamespace(topic=topic, payload=payload)
        cb(self, None, msg)

    def deliver_heartbeats(self, prefix="pfx/"):
        for topic in list(self._callbacks):
            if topic.endswith("/e2m/heartbeat"):
                self.deliver(topic)


@pytest.fixture
def two_ev_config(tmp_path):
    p = tmp_path / "everest-config-two-ev.yaml"
    p.write_text(_CONFIG_YAML_TWO_EV)
    return p


@pytest.fixture
def fake_client():
    return _FakeMqttClient()


def test_wait_for_scans_once_more_after_the_deadline(monkeypatch, fake_client):
    import everest.testing.core_utils.sim_registry as mod

    drv = EvControllerDriver(fake_client, "pfx/", "car")
    drv.subscribe_var("state")
    clock = types.SimpleNamespace(now=0.0)

    def sleep(_seconds):
        clock.now = 1.0
        fake_client.deliver("pfx/everest_api/1/ev_controller/car/e2m/state", b'"Charging"')

    monkeypatch.setattr(mod, "time", types.SimpleNamespace(time=lambda: clock.now, sleep=sleep))
    assert drv.wait_for("state", lambda s: s == "Charging", timeout=0.5) == "Charging"


def test_registry_exposes_its_client(fake_client, two_ev_config):
    assert SimRegistry(fake_client, "pfx/", two_ev_config).client is fake_client


def test_autostart_does_not_spend_its_budget_inline(fake_client, two_ev_config):
    """autostart() must return promptly when no heartbeat arrives.

    The budget belongs at the point of use, not in start(): a two-instance
    config used to burn 2 x 15 s on the caller's thread, which in every OCPP
    test is the event loop the CSMS runs on.
    """
    import time as _time

    registry = SimRegistry(fake_client, "pfx/", two_ev_config)
    started = _time.monotonic()
    registry.autostart()
    elapsed = _time.monotonic() - started

    assert elapsed < 2.0, f"autostart() blocked for {elapsed:.1f}s with no heartbeat"


def test_autostart_still_publishes_enable_for_every_instance(fake_client, two_ev_config):
    registry = SimRegistry(fake_client, "pfx/", two_ev_config)
    registry.autostart()

    enables = [t for t, _ in fake_client.published if t.endswith("/m2e/enable")]
    assert sorted(enables) == [
        "pfx/everest_api/1/ev_controller/ev_sim_1/m2e/enable",
        "pfx/everest_api/1/ev_controller/ev_sim_2/m2e/enable",
    ]


_STATE_TOPIC = "pfx/everest_api/1/ev_controller/ev_sim_1/e2m/state"


@pytest.fixture
def driver_config(tmp_path):
    cfg = tmp_path / "with-driver.yaml"
    cfg.write_text(textwrap.dedent("""\
        active_modules:
          world:
            module: EvDriverSim
            config_module:
              ev_controller_module_id: car
          unrelated:
            module: EvController
            config_module:
              connector_id: 1
          car:
            module: EvController
            config_module:
              connector_id: 7
    """))
    return cfg


def test_registry_indexes_driver_through_its_car(fake_client, driver_config):
    registry = SimRegistry(fake_client, "pfx/", driver_config)
    driver = registry.by_module("world")
    assert driver is not None
    assert isinstance(driver, EvDriverSimDriver)
    assert registry.ev_driver_by_connector(7) is driver
    assert registry.ev_driver_by_connector(1) is None
    assert registry.ev_by_connector(7).module_id == "car"


@pytest.mark.parametrize("command,kwargs,payload", [
    ("plug", {}, None),
    ("unplug", {}, None),
    ("inject_fault", {"fault_type": "DiodeFail"}, {"type": "DiodeFail"}),
    ("inject_fault", {"fault_type": "CpErrorE", "message": "test fault"},
     {"type": "CpErrorE", "message": "test fault"}),
    ("inject_fault", {"fault_type": "RcdError", "rcd_mA": 30.0},
     {"type": "RcdError", "rcd_mA": 30.0}),
    ("clear_fault", {}, None),
])
def test_world_commands_publish_typed_topics(fake_client, driver_config, command, kwargs, payload):
    driver = SimRegistry(fake_client, "pfx/", driver_config).by_module("world")
    getattr(driver, command)(**kwargs)
    assert fake_client.published == [
        (f"pfx/everest_api/1/ev_driver_sim/world/m2e/{command}", json.dumps(payload))
    ]


def test_world_state_and_command_ack_are_observable(fake_client, driver_config):
    registry = SimRegistry(fake_client, "pfx/", driver_config)
    registry.arm()
    driver = registry.by_module("world")
    base = "pfx/everest_api/1/ev_driver_sim/world/e2m"
    assert {f"{base}/{var}" for var in ("world_state", "command_ack", "heartbeat")} <= set(
        fake_client.subscriptions)
    assert driver.world_state is None
    fake_client.deliver(f"{base}/world_state", b'{"pilot":"Passed","fault":"DiodeFail"}')
    fake_client.deliver(f"{base}/world_state", b'{"pilot":"Withheld"}')
    assert driver.world_state == {"pilot": "Withheld"}
    for command in ("plug", "unplug", "inject_fault", "clear_fault"):
        ack = {"command": command, "status": "Accepted"}
        fake_client.deliver(f"{base}/command_ack", json.dumps(ack).encode())
        if command == "inject_fault":
            driver.inject_fault("DiodeFail")
        else:
            getattr(driver, command)()
        with pytest.raises(AssertionError, match="timeout"):
            driver.wait_for_command_ack(command, timeout=0.01)
        fake_client.deliver(f"{base}/command_ack", b'{"command":"other","status":"Accepted"}')
        fake_client.deliver(f"{base}/command_ack", json.dumps(ack).encode())
        assert driver.wait_for_command_ack(command, timeout=0.1) == ack


def test_registry_without_world_driver_keeps_car_mapping(registry):
    assert registry.ev_driver_by_connector(1) is None
    assert registry.ev_by_connector(1).module_id == "ev_sim_1"


def test_driver_uses_default_car_id(fake_client, tmp_path):
    cfg = tmp_path / "defaults.yaml"
    cfg.write_text(textwrap.dedent("""\
        active_modules:
          world:
            module: EvDriverSim
          ev_manager:
            module: EvController
            config_module:
              connector_id: 3
    """))
    registry = SimRegistry(fake_client, "pfx/", cfg)
    assert registry.ev_driver_by_connector(3) is registry.by_module("world")
    registry.autostart()
    assert fake_client.published == [("pfx/everest_api/1/ev_controller/ev_manager/m2e/enable", "true")]


def test_world_ack_can_arrive_during_publish(fake_client, driver_config):
    driver = SimRegistry(fake_client, "pfx/", driver_config).by_module("world")
    ack = {"command": "plug", "status": "Rejected", "reason": "already plugged"}
    fake_client.publish = lambda *_: fake_client.deliver(
        "pfx/everest_api/1/ev_driver_sim/world/e2m/command_ack", json.dumps(ack).encode())
    driver.plug()
    assert driver.wait_for_command_ack("plug", "Rejected", timeout=0.1) == ack


def test_world_subscriptions_wait_for_registry_arm(fake_client, driver_config):
    registry = SimRegistry(fake_client, "pfx/", driver_config)
    assert fake_client.subscriptions == []
    registry.arm()
    assert "pfx/everest_api/1/ev_driver_sim/world/e2m/heartbeat" in fake_client.subscriptions


def test_world_ack_satisfies_only_one_publish(fake_client, driver_config):
    driver = SimRegistry(fake_client, "pfx/", driver_config).by_module("world")
    first = driver.plug()
    second = driver.plug()
    ack = {"command": "plug", "status": "Accepted"}
    fake_client.deliver("pfx/everest_api/1/ev_driver_sim/world/e2m/command_ack", json.dumps(ack).encode())
    assert first.wait(timeout=0.1) == ack
    with pytest.raises(AssertionError, match="timeout"):
        second.wait(timeout=0.01)


def test_world_timeout_preserves_pending_publish_and_its_late_ack(fake_client, driver_config):
    driver = SimRegistry(fake_client, "pfx/", driver_config).by_module("world")
    first = driver.unplug()
    with pytest.raises(AssertionError, match="timeout"):
        first.wait(timeout=0.01)
    second = driver.unplug()
    ack = {"command": "unplug", "status": "Accepted"}
    topic = "pfx/everest_api/1/ev_driver_sim/world/e2m/command_ack"
    fake_client.deliver(topic, json.dumps(ack).encode())
    assert first.wait(timeout=0.1) == ack
    with pytest.raises(AssertionError, match="timeout"):
        second.wait(timeout=0.01)
    fake_client.deliver(topic, json.dumps(ack).encode())
    assert second.wait(timeout=0.1) == ack


def test_world_wait_uses_each_publish_cursor(fake_client, driver_config):
    driver = SimRegistry(fake_client, "pfx/", driver_config).by_module("world")
    first = driver.plug()
    ack = {"command": "plug", "status": "Accepted"}
    fake_client.deliver("pfx/everest_api/1/ev_driver_sim/world/e2m/command_ack", json.dumps(ack).encode())
    second = driver.plug()
    assert first.wait(timeout=0.1) == ack
    with pytest.raises(AssertionError, match="timeout"):
        second.wait(timeout=0.01)


@pytest.mark.parametrize("same_publish", [False, True])
def test_concurrent_world_waiters_cannot_share_an_ack(fake_client, driver_config, same_publish):
    from concurrent.futures import ThreadPoolExecutor

    driver = SimRegistry(fake_client, "pfx/", driver_config).by_module("world")
    first = driver.plug()
    publishes = [first, first if same_publish else driver.plug()]
    start = threading.Barrier(3)

    def wait(publish):
        start.wait(timeout=1)
        try:
            return publish.wait(timeout=0.1)
        except AssertionError:
            return None

    with ThreadPoolExecutor(max_workers=2) as pool:
        waits = [pool.submit(wait, publish) for publish in publishes]
        start.wait(timeout=1)
        ack = {"command": "plug", "status": "Accepted"}
        fake_client.deliver("pfx/everest_api/1/ev_driver_sim/world/e2m/command_ack", json.dumps(ack).encode())
        results = [pending.result(timeout=1) for pending in waits]
    assert results.count(ack) == 1
    assert results.count(None) == 1


@pytest.mark.parametrize("first_ack_is_late", [False, True])
def test_unawaited_world_publish_cannot_supply_a_later_wait(fake_client, driver_config, first_ack_is_late):
    driver = SimRegistry(fake_client, "pfx/", driver_config).by_module("world")
    topic = "pfx/everest_api/1/ev_driver_sim/world/e2m/command_ack"
    first_ack = {"command": "unplug", "status": "Accepted", "reason": "first publish"}
    second_ack = {"command": "unplug", "status": "Accepted", "reason": "second publish"}
    driver.unplug()
    if not first_ack_is_late:
        fake_client.deliver(topic, json.dumps(first_ack).encode())
    driver.unplug()
    if first_ack_is_late:
        fake_client.deliver(topic, json.dumps(first_ack).encode())
    with pytest.raises(AssertionError, match="timeout"):
        driver.wait_for_command_ack("unplug", timeout=0.01)
    fake_client.deliver(topic, json.dumps(second_ack).encode())
    assert driver.wait_for_command_ack("unplug", timeout=0.1) == second_ack


def test_malformed_ack_does_not_claim_a_publish(fake_client, driver_config):
    driver = SimRegistry(fake_client, "pfx/", driver_config).by_module("world")
    waiter = driver.plug()
    topic = "pfx/everest_api/1/ev_driver_sim/world/e2m/command_ack"
    for malformed in ({"command": [], "status": "Accepted"}, {"command": "plug"}):
        fake_client.deliver(topic, json.dumps(malformed).encode())
    ack = {"command": "plug", "status": "Accepted"}
    fake_client.deliver(topic, json.dumps(ack).encode())
    assert waiter.wait(timeout=0.1) == ack


def test_accepted_wait_fails_with_rejection_reason(fake_client, driver_config):
    driver = SimRegistry(fake_client, "pfx/", driver_config).by_module("world")
    waiter = driver.plug()
    fake_client.deliver(
        "pfx/everest_api/1/ev_driver_sim/world/e2m/command_ack",
        b'{"command":"plug","status":"Rejected","reason":"already plugged"}',
    )
    with pytest.raises(AssertionError, match="plug rejected: already plugged"):
        waiter.wait(timeout=0.1)


@pytest.mark.parametrize("kind,ack_status", [
    ("Started", "Accepted"), ("Rejected", "Rejected"), ("Rejected", "Accepted"),
])
def test_world_scenario_waits_on_its_event(fake_client, driver_config, kind, ack_status):
    driver = SimRegistry(fake_client, "pfx/", driver_config).by_module("world")
    driver.arm()
    base = "pfx/everest_api/1/ev_driver_sim/world"
    published = threading.Event()
    fake_client.publish = lambda *_: published.set()
    errors = []
    finished = threading.Event()

    def run():
        try:
            driver.run_scenario("AcIecBasic", timing={"stop_after_ms": 40}, timeout=0.5)
        except Exception as exc:
            errors.append(str(exc))
        finally:
            finished.set()

    worker = threading.Thread(target=run)
    worker.start()
    try:
        assert published.wait(0.2), errors
        assert not finished.wait(0.05)
        ack = {"command": "run_scenario", "status": ack_status, "reason": "configuration refused"}
        fake_client.deliver(f"{base}/e2m/command_ack", json.dumps(ack).encode())
        if ack_status == "Accepted":
            assert not finished.wait(0.05)
        event = {"name": "AcIecBasic", "kind": kind, "message": "configuration refused"}
        fake_client.deliver(f"{base}/e2m/scenario_event", json.dumps(event).encode())
        assert finished.wait(0.2)
        if kind == "Rejected":
            assert errors and "configuration refused" in errors[0]
        else:
            assert not errors
    finally:
        worker.join(timeout=1)


def test_concurrent_same_name_scenarios_get_their_own_result(fake_client, driver_config):
    from concurrent.futures import ThreadPoolExecutor

    driver = SimRegistry(fake_client, "pfx/", driver_config).by_module("world")
    base = "pfx/everest_api/1/ev_driver_sim/world/e2m"
    first_published = threading.Event()
    second_entered = threading.Event()
    second_published = threading.Event()
    started = {"name": "AcIecBasic", "kind": "Started"}

    def deliver(var, payload):
        fake_client.deliver(f"{base}/{var}", json.dumps(payload).encode())

    def publish(topic, payload):
        assert topic.endswith("/m2e/run_scenario")
        assert json.loads(payload)["name"] == "AcIecBasic"
        if not first_published.is_set():
            deliver("command_ack", {"command": "run_scenario", "status": "Accepted"})
            first_published.set()
        else:
            # Overlap emits Rejected before its command acknowledgement.
            deliver("scenario_event", {
                "name": "AcIecBasic", "kind": "Rejected", "message": "scenario already running",
            })
            deliver("command_ack", {
                "command": "run_scenario", "status": "Rejected", "reason": "scenario already running",
            })
            second_published.set()

    fake_client.publish = publish

    def second_start():
        second_entered.set()
        return driver.run_scenario("AcIecBasic", timeout=1)

    with ThreadPoolExecutor(max_workers=2) as pool:
        first = pool.submit(driver.run_scenario, "AcIecBasic", timeout=1)
        assert first_published.wait(1)
        second = pool.submit(second_start)
        assert second_entered.wait(1)
        published_before_started = second_published.wait(0.1)
        deliver("scenario_event", started)
        assert first.result(timeout=1) == started
        with pytest.raises(AssertionError, match="run_scenario rejected: scenario already running"):
            second.result(timeout=1)
        assert not published_before_started


@pytest.mark.parametrize("failure", ["ack_rejected", "event_rejected", "ack_timeout", "event_timeout"])
def test_scenario_start_lock_releases_after_failure(fake_client, driver_config, failure):
    driver = SimRegistry(fake_client, "pfx/", driver_config).by_module("world")
    base = "pfx/everest_api/1/ev_driver_sim/world/e2m"
    accepted = {"command": "run_scenario", "status": "Accepted"}
    started = {"name": "AcIecBasic", "kind": "Started"}

    def deliver(var, payload):
        fake_client.deliver(f"{base}/{var}", json.dumps(payload).encode())

    def fail_start(*_):
        if failure == "ack_rejected":
            deliver("scenario_event", {
                "name": "AcIecBasic", "kind": "Rejected", "message": "no heartbeat",
            })
            deliver("command_ack", {
                "command": "run_scenario", "status": "Rejected", "reason": "no heartbeat",
            })
        elif failure != "ack_timeout":
            deliver("command_ack", accepted)
            if failure == "event_rejected":
                deliver("scenario_event", {
                    "name": "AcIecBasic", "kind": "Rejected", "message": "configuration refused",
                })

    fake_client.publish = fail_start
    with pytest.raises(AssertionError, match="timeout" if "timeout" in failure else "rejected"):
        driver.run_scenario("AcIecBasic", timeout=0.1)

    if failure == "ack_timeout":
        # The timed-out publish still owns its eventual reply.
        deliver("command_ack", accepted)

    def succeed_start(*_):
        deliver("command_ack", accepted)
        deliver("scenario_event", started)

    fake_client.publish = succeed_start
    results = []
    worker = threading.Thread(
        target=lambda: results.append(driver.run_scenario("AcIecBasic", timeout=0.2)), daemon=True,
    )
    worker.start()
    worker.join(timeout=1)
    assert not worker.is_alive()
    assert results == [started]


def test_scenario_start_wait_does_not_block_another_driver(fake_client):
    from concurrent.futures import ThreadPoolExecutor

    first = EvDriverSimDriver(fake_client, "pfx/", "first")
    second = EvDriverSimDriver(fake_client, "pfx/", "second")
    first_published = threading.Event()
    started = {"name": "AcIecBasic", "kind": "Started"}

    def deliver(module, var, payload):
        topic = f"pfx/everest_api/1/ev_driver_sim/{module}/e2m/{var}"
        fake_client.deliver(topic, json.dumps(payload).encode())

    def publish(topic, _):
        module = topic.split("/")[-3]
        deliver(module, "command_ack", {"command": "run_scenario", "status": "Accepted"})
        if module == "first":
            first_published.set()
        else:
            deliver(module, "scenario_event", started)

    fake_client.publish = publish
    with ThreadPoolExecutor(max_workers=2) as pool:
        pending = pool.submit(first.run_scenario, "AcIecBasic", timeout=1)
        assert first_published.wait(1)
        try:
            other = pool.submit(second.run_scenario, "AcIecBasic", timeout=1)
            assert other.result(timeout=0.5) == started
            assert not pending.done()
        finally:
            deliver("first", "scenario_event", started)
        assert pending.result(timeout=1) == started


@pytest.mark.parametrize("verb", ["plug", "unplug", "inject_fault", "clear_fault"])
def test_car_driver_has_no_world_commands(registry, verb):
    assert not hasattr(registry.ev_by_connector(1), verb)

