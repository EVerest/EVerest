# SPDX-License-Identifier: Apache-2.0
# Copyright Pionix GmbH and Contributors to EVerest
"""Unit tests for SimRegistry and YetiSimDriver."""

import json
import logging
import os
import sys
import textwrap
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
    EvSimDriver,
    SimRegistry,
    YetiSimDriver,
)
from everest.testing.core_utils.controller.everest_test_controller import (  # noqa: E402
    EverestTestController,
    _translate_legacy_error_type,
)


# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------

_CONFIG_YAML = textwrap.dedent("""\
    active_modules:
      ev_sim_1:
        module: EvSimulator
        config_module:
          connector_id: 1
      yeti_sim_1:
        module: YetiSimulator
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


def test_ev_by_connector_returns_ev_sim_driver(registry):
    drv = registry.ev_by_connector(1)
    assert isinstance(drv, EvSimDriver)


def test_yeti_by_connector_returns_yeti_sim_driver(registry):
    drv = registry.yeti_by_connector(1)
    assert isinstance(drv, YetiSimDriver)


def test_yeti_by_connector_missing_returns_none(registry):
    assert registry.yeti_by_connector(2) is None


# ---------------------------------------------------------------------------
# YetiSimDriver publish surface
# ---------------------------------------------------------------------------


def test_yeti_raise_error_publishes_once(registry, mqtt_client):
    drv = registry.yeti_by_connector(1)
    assert drv is not None
    payload = {"type": "evse_board_support/MREC6UnderVoltage"}
    drv.raise_error(payload)
    assert mqtt_client.publish.call_count == 1
    args, _ = mqtt_client.publish.call_args
    topic, body = args
    assert topic == "pfx/everest_api/1/yeti_simulator/yeti_sim_1/m2e/raise_error"
    assert json.loads(body) == payload


def test_yeti_raise_error_passes_severity(registry, mqtt_client):
    drv = registry.yeti_by_connector(1)
    assert drv is not None
    payload = {"type": "evse_board_support/MREC6UnderVoltage", "severity": "Low"}
    drv.raise_error(payload)
    args, _ = mqtt_client.publish.call_args
    _, body = args
    assert json.loads(body) == payload


def test_yeti_clear_error_publishes_once(registry, mqtt_client):
    drv = registry.yeti_by_connector(1)
    assert drv is not None
    payload = {"type": "evse_board_support/MREC6UnderVoltage"}
    drv.clear_error(payload)
    assert mqtt_client.publish.call_count == 1
    args, _ = mqtt_client.publish.call_args
    topic, body = args
    assert topic == "pfx/everest_api/1/yeti_simulator/yeti_sim_1/m2e/clear_error"
    assert json.loads(body) == payload


# ---------------------------------------------------------------------------
# _translate_legacy_error_type
# ---------------------------------------------------------------------------


def test_translate_bare_short_name():
    assert _translate_legacy_error_type("MREC6UnderVoltage") == "evse_board_support/MREC6UnderVoltage"


def test_translate_underscore_prefixed_ac_rcd():
    assert _translate_legacy_error_type("ac_rcd_MREC2GroundFailure") == "ac_rcd/MREC2GroundFailure"


def test_translate_underscore_prefixed_lock():
    assert _translate_legacy_error_type("lock_ConnectorLockFailedLock") == "connector_lock/ConnectorLockFailedLock"


def test_translate_already_slash_prefixed_powermeter():
    assert _translate_legacy_error_type("powermeter/CommunicationFault") == "powermeter/CommunicationFault"


def test_translate_unknown_passthrough():
    assert _translate_legacy_error_type("SomeNewError") == "SomeNewError"


# ---------------------------------------------------------------------------
# EverestTestController raise_error / clear_error routing
# ---------------------------------------------------------------------------


@pytest.fixture
def controller_with_yeti(monkeypatch, config_path):
    """EverestTestController wired with a mocked MQTT client and a tmpdir config
    that has one YetiSimulator at connector_id=1.
    """
    import everest.testing.core_utils.controller.everest_test_controller as mod

    fake_client = MagicMock()
    fake_client.publish = MagicMock()
    fake_client.connect = MagicMock()
    fake_client.loop_start = MagicMock()
    fake_client.loop_stop = MagicMock()
    fake_client.disconnect = MagicMock()
    monkeypatch.setattr(mod.mqtt, "Client", MagicMock(return_value=fake_client))

    core = MagicMock()
    core.mqtt_external_prefix = "pfx/"
    core.everest_uuid = "test-uuid"
    core.everest_config_path = str(config_path)

    ctrl = EverestTestController(core)
    # Wire the registry directly so we bypass the real start() flow.
    ctrl._mqtt_client = fake_client
    ctrl._registry = SimRegistry(fake_client, "pfx/", config_path)
    ctrl._fake_client = fake_client
    return ctrl


def test_raise_error_routes_to_yeti_topic(controller_with_yeti):
    ctrl = controller_with_yeti
    ctrl.raise_error("MREC6UnderVoltage", connector_id=1)

    topics = [c.args[0] for c in ctrl._fake_client.publish.call_args_list]
    assert any("yeti_simulator" in t and "m2e/raise_error" in t for t in topics), topics
    assert not any("carsim/error" in t for t in topics), topics
    assert ctrl._fake_client.publish.call_count == 1


def test_clear_error_routes_to_yeti_topic(controller_with_yeti):
    ctrl = controller_with_yeti
    ctrl.clear_error("MREC6UnderVoltage", connector_id=1)

    topics = [c.args[0] for c in ctrl._fake_client.publish.call_args_list]
    assert any("yeti_simulator" in t and "m2e/clear_error" in t for t in topics), topics
    assert not any("carsim/error" in t for t in topics), topics
    assert ctrl._fake_client.publish.call_count == 1


def test_raise_error_payload_contains_full_type(controller_with_yeti):
    ctrl = controller_with_yeti
    ctrl.raise_error("MREC6UnderVoltage", connector_id=1)

    args, _ = ctrl._fake_client.publish.call_args
    body = json.loads(args[1])
    assert body == {"type": "evse_board_support/MREC6UnderVoltage"}


_CONFIG_YAML_EV_ONLY = textwrap.dedent("""\
    active_modules:
      ev_sim_1:
        module: EvSimulator
        config_module:
          connector_id: 1
""")


@pytest.fixture
def controller_ev_only(monkeypatch, tmp_path):
    """EverestTestController with only an EvSimulator (no YetiSimulator)."""
    import everest.testing.core_utils.controller.everest_test_controller as mod

    cfg = tmp_path / "everest-config-ev-only.yaml"
    cfg.write_text(_CONFIG_YAML_EV_ONLY)

    fake_client = MagicMock()
    fake_client.publish = MagicMock()
    fake_client.connect = MagicMock()
    fake_client.loop_start = MagicMock()
    fake_client.loop_stop = MagicMock()
    fake_client.disconnect = MagicMock()
    monkeypatch.setattr(mod.mqtt, "Client", MagicMock(return_value=fake_client))

    core = MagicMock()
    core.mqtt_external_prefix = "pfx/"
    core.everest_uuid = "test-uuid"
    core.everest_config_path = str(cfg)

    ctrl = EverestTestController(core)
    ctrl._mqtt_client = fake_client
    ctrl._registry = SimRegistry(fake_client, "pfx/", cfg)
    ctrl._fake_client = fake_client
    return ctrl


def test_raise_error_warns_when_no_yeti_instance(monkeypatch, caplog, controller_ev_only):
    # Build a controller with EvSimulator only (no YetiSimulator) and verify
    # raise_error against any connector logs a warning and emits no publish.
    ctrl = controller_ev_only
    with caplog.at_level(logging.WARNING):
        ctrl.raise_error("MREC6UnderVoltage", connector_id=1)

    assert ctrl._fake_client.publish.call_count == 0
    assert any(
        "yeti_sim: no YetiSimulator instance for connector_id" in r.message
        for r in caplog.records
    )


# ---------------------------------------------------------------------------
# Readiness gate
#
# The gate exists because m2e commands published before EvSimulator's `ready()`
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
        module: EvSimulator
        config_module:
          connector_id: 1
      ev_sim_2:
        module: EvSimulator
        config_module:
          connector_id: 2
""")


class _FakeSocket:
    """Stand-in for the connected socket paho exposes via `Client.socket()`."""

    def getpeername(self):
        return ("127.0.0.1", 1883)

    def getsockname(self):
        return ("127.0.0.1", 54321)


class _FakeMqttClient:
    """Minimal paho stand-in that records subscriptions and can inject messages."""

    def __init__(self):
        self._sock = None
        self.subscriptions = []
        self.published = []
        self.events = []
        self._callbacks = {}
        self.autodeliver = set()
        # Default to a broker that is up, which is the ordinary case. Tests
        # that put the CONNACK handshake itself under test set this False.
        self.autoconnack = True
        self._mid = 0
        self._mid_topic = {}
        # SUBACKs the broker would send; flush_subacks() delivers them.
        self.autosuback = True
        self.pending_subacks = []

    def flush_subacks(self, codes=None):
        for mid in list(self.pending_subacks):
            self.events.append(("suback", self._mid_topic.get(mid)))
            if self.on_subscribe is not None:
                self.on_subscribe(self, None, mid, codes if codes is not None else [0], None)
        self.pending_subacks.clear()

    # -- paho surface used by the registry ---------------------------------
    def subscribe(self, topic):
        self.subscriptions.append(topic)
        self.events.append(("subscribe", topic))
        self._mid += 1
        self._mid_topic[self._mid] = topic
        if self.autosuback:
            self.pending_subacks.append(self._mid)
        return (0, self._mid)

    def message_callback_add(self, topic, callback):
        self._callbacks[topic] = callback

    def publish(self, topic, payload=None):
        self.published.append((topic, payload))
        self.events.append(("publish", topic))

    on_connect = None
    on_subscribe = None
    on_socket_open = None
    on_socket_close = None
    on_connect_fail = None

    def connect(self, *_a, **_kw):
        self.events.append(("connect", None))
        # paho hands back a live socket from connect() onwards, and the
        # controller reads the peer off it to record which broker answered.
        # Modelling that here keeps the double honest about the difference
        # between "transport up, no CONNACK" and "transport down".
        self._sock = _FakeSocket()
        if self.on_socket_open is not None:
            self.on_socket_open(self, None, self._sock)
        if self.autoconnack:
            self.fire_connack()

    def socket(self):
        return self._sock

    def fire_connack(self):
        self.events.append(("connack", None))
        if self.on_connect is not None:
            self.on_connect(self, None, {}, 0, None)

    def loop_start(self):
        self.events.append(("loop_start", None))

    def loop_stop(self):
        pass

    def disconnect(self):
        if self._sock is not None:
            self._sock = None
            if self.on_socket_close is not None:
                self.on_socket_close(self, None, None)

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


def test_ev_lookup_raises_naming_the_instance_when_never_ready(
    monkeypatch, two_ev_config
):
    """Driving an instance that never reported readiness must fail loudly."""
    import everest.testing.core_utils.controller.everest_test_controller as mod

    fake = _FakeMqttClient()
    monkeypatch.setattr(mod.mqtt, "Client", MagicMock(return_value=fake))

    core = MagicMock()
    core.mqtt_external_prefix = "pfx/"
    core.everest_uuid = "test-uuid"
    core.everest_config_path = str(two_ev_config)

    ctrl = EverestTestController(core)
    ctrl.start()
    assert ctrl._registry._readiness_timeout == 15.0
    # Shorten the real budget; this asserts the outcome, not the wall clock.
    ctrl._registry._readiness_timeout = 0.3

    with pytest.raises(AssertionError) as excinfo:
        ctrl.plug_in(connector_id=1)

    assert "ev_sim_1" in str(excinfo.value)
    assert not any(t.endswith("/m2e/plug") for t, _ in fake.published), fake.published


def test_ev_lookup_succeeds_once_the_heartbeat_arrives(monkeypatch, two_ev_config):
    """The gate must pass, not merely warn, when the instance does report."""
    import everest.testing.core_utils.controller.everest_test_controller as mod

    fake = _FakeMqttClient()
    monkeypatch.setattr(mod.mqtt, "Client", MagicMock(return_value=fake))

    core = MagicMock()
    core.mqtt_external_prefix = "pfx/"
    core.everest_uuid = "test-uuid"
    core.everest_config_path = str(two_ev_config)

    ctrl = EverestTestController(core)
    ctrl.start()
    fake.deliver_heartbeats()

    ctrl.plug_in(connector_id=1)

    assert any(t.endswith("/m2e/plug") for t, _ in fake.published), fake.published


def test_heartbeat_subscriptions_are_armed_before_everest_boots(
    monkeypatch, two_ev_config
):
    """The heartbeat is not retained, so subscribing after boot is a bet."""
    import everest.testing.core_utils.controller.everest_test_controller as mod

    fake = _FakeMqttClient()
    monkeypatch.setattr(mod.mqtt, "Client", MagicMock(return_value=fake))

    core = MagicMock()
    core.mqtt_external_prefix = "pfx/"
    core.everest_uuid = "test-uuid"
    core.everest_config_path = str(two_ev_config)
    core.start = MagicMock(side_effect=lambda *a, **kw: fake.events.append(("everest_start", None)))

    ctrl = EverestTestController(core)
    ctrl.start()

    kinds = [k for k, _ in fake.events]
    assert "everest_start" in kinds, fake.events
    boot = kinds.index("everest_start")
    subscribed_before = [
        t for i, (k, t) in enumerate(fake.events)
        if k == "subscribe" and i < boot and t.endswith("/e2m/heartbeat")
    ]
    assert len(subscribed_before) == 2, fake.events


def test_autostart_still_publishes_enable_for_every_instance(fake_client, two_ev_config):
    registry = SimRegistry(fake_client, "pfx/", two_ev_config)
    registry.autostart()

    enables = [t for t, _ in fake_client.published if t.endswith("/m2e/enable")]
    assert sorted(enables) == [
        "pfx/everest_api/1/ev_simulator/ev_sim_1/m2e/enable",
        "pfx/everest_api/1/ev_simulator/ev_sim_2/m2e/enable",
    ]


def test_failed_subscribe_is_reported_not_swallowed(caplog, two_ev_config):
    """A SUBSCRIBE that paho refused must not masquerade as a silent peer."""
    fake = _FakeMqttClient()
    fake.subscribe = lambda topic: (4, None)  # MQTT_ERR_NO_CONN

    registry = SimRegistry(fake, "pfx/", two_ev_config)
    with caplog.at_level(logging.ERROR):
        # subscribe_var is the single subscribe funnel; exercise it directly so
        # this asserts the rc handling and nothing about how it is reached.
        for drv in registry.drivers.values():
            drv.subscribe_var("heartbeat")

    messages = [r.getMessage() for r in caplog.records]
    assert any("subscribe to" in m and "rc=4" in m for m in messages), messages
    assert any("ev_sim_1" in m for m in messages), messages


# ---------------------------------------------------------------------------
# Broker connection state
#
# arm() now runs microseconds after connect() rather than ~1.4s later, so the
# CONNACK race is live: paho writes a SUBSCRIBE issued before the connection is
# established, returns rc=0, and the subscription may simply not exist. That is
# indistinguishable from a silent peer and burns the whole readiness budget.
# ---------------------------------------------------------------------------


def _controller_for(monkeypatch, cfg, fake):
    import everest.testing.core_utils.controller.everest_test_controller as mod
    monkeypatch.setattr(mod.mqtt, "Client", MagicMock(return_value=fake))
    core = MagicMock()
    core.mqtt_external_prefix = "pfx/"
    core.everest_uuid = "test-uuid"
    core.everest_config_path = str(cfg)
    core.start = MagicMock(
        side_effect=lambda *a, **kw: fake.events.append(("everest_start", None)))
    return EverestTestController(core)


def test_subscribe_waits_for_connack(monkeypatch, two_ev_config):
    """No SUBSCRIBE may be issued before the broker has acknowledged CONNECT."""
    import threading

    fake = _FakeMqttClient()
    fake.autoconnack = False
    ctrl = _controller_for(monkeypatch, two_ev_config, fake)
    # CONNACK lands only after a delay, as a real broker would.
    threading.Timer(0.3, fake.fire_connack).start()

    ctrl.start()

    kinds = [k for k, _ in fake.events]
    assert "connack" in kinds, fake.events
    first_sub = next(i for i, (k, _) in enumerate(fake.events) if k == "subscribe")
    assert kinds.index("connack") < first_sub, fake.events


def test_start_raises_when_broker_never_connacks(monkeypatch, two_ev_config):
    """A broker that never acknowledges must fail loudly, not silently arm."""
    fake = _FakeMqttClient()
    fake.autoconnack = False
    ctrl = _controller_for(monkeypatch, two_ev_config, fake)

    with pytest.raises(AssertionError) as excinfo:
        ctrl.start(broker_connect_timeout=0.2)

    assert "broker" in str(excinfo.value).lower()
    assert not any(k == "subscribe" for k, _ in fake.events), fake.events
    assert not any(k == "everest_start" for k, _ in fake.events), fake.events


def test_reconnect_reissues_subscriptions(monkeypatch, two_ev_config):
    """paho does not restore subscriptions across a reconnect; we must."""
    fake = _FakeMqttClient()
    ctrl = _controller_for(monkeypatch, two_ev_config, fake)
    ctrl.start()

    before = [t for k, t in fake.events if k == "subscribe"]
    assert len(before) == 2, fake.events

    fake.fire_connack()  # broker dropped us and we reconnected

    after = [t for k, t in fake.events if k == "subscribe"]
    assert len(after) == 4, fake.events
    assert sorted(set(after)) == sorted(set(before)), fake.events


# ---------------------------------------------------------------------------
# Why the gate refused
#
# "No heartbeat" has two causes with opposite owners, and the whole point of
# the gate is to be readable: either the broker never established the
# harness's subscription (nothing is known about the module), or it did and
# the module said nothing (an EvSimulator finding). A gate that cannot tell
# them apart is the same class of instrument this branch already paid for.
# ---------------------------------------------------------------------------


def _started(monkeypatch, cfg, fake):
    ctrl = _controller_for(monkeypatch, cfg, fake)
    ctrl.start()
    ctrl._registry._readiness_timeout = 0.2
    return ctrl


def test_gate_blames_transport_when_subscription_unacknowledged(
    monkeypatch, two_ev_config
):
    fake = _FakeMqttClient()
    fake.autosuback = False          # broker never SUBACKs
    ctrl = _started(monkeypatch, two_ev_config, fake)

    with pytest.raises(AssertionError) as excinfo:
        ctrl.plug_in(connector_id=1)

    msg = str(excinfo.value)
    assert "never acknowledged" in msg, msg
    assert "ev_sim_1" in msg, msg
    # Must not be reported as a simulator finding.
    assert "did not publish" not in msg, msg


def test_gate_blames_the_module_when_subscription_acknowledged(
    monkeypatch, two_ev_config
):
    fake = _FakeMqttClient()
    ctrl = _started(monkeypatch, two_ev_config, fake)
    fake.flush_subacks()             # broker granted both subscriptions

    with pytest.raises(AssertionError) as excinfo:
        ctrl.plug_in(connector_id=1)

    msg = str(excinfo.value)
    assert "did not publish" in msg, msg
    assert "ev_sim_1" in msg, msg
    assert "never acknowledged" not in msg, msg


def test_gate_reports_a_refused_subscription(monkeypatch, two_ev_config):
    fake = _FakeMqttClient()
    ctrl = _started(monkeypatch, two_ev_config, fake)
    fake.flush_subacks(codes=[0x87])  # SUBACK carrying a failure code

    with pytest.raises(AssertionError) as excinfo:
        ctrl.plug_in(connector_id=1)

    msg = str(excinfo.value)
    assert "refused" in msg, msg
    assert "ev_sim_1" in msg, msg
