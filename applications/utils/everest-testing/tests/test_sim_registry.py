# SPDX-License-Identifier: Apache-2.0
# Copyright Pionix GmbH and Contributors to EVerest
"""Unit tests for SimRegistry and YetiSimDriver."""

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
        module: EvController
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


def test_ev_by_connector_returns_ev_controller_driver(registry):
    drv = registry.ev_by_connector(1)
    assert isinstance(drv, EvControllerDriver)


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
    import everest.testing.core_utils.sim_registry as mod

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
        module: EvController
        config_module:
          connector_id: 1
""")


@pytest.fixture
def controller_ev_only(monkeypatch, tmp_path):
    """EverestTestController with only an EvController (no YetiSimulator)."""
    import everest.testing.core_utils.sim_registry as mod

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


def test_raise_error_fails_when_no_yeti_instance(controller_ev_only):
    # A controller with EvController only cannot raise a Yeti error. Warning and
    # returning let the test carry on believing the error had been raised.
    ctrl = controller_ev_only
    with pytest.raises(NoSimulatorError, match="no YetiSimulator instance"):
        ctrl.raise_error("MREC6UnderVoltage", connector_id=1)

    assert ctrl._fake_client.publish.call_count == 0


def test_plug_in_fails_when_no_ev_instance_for_that_connector(controller_ev_only):
    # The regression this guards: every plug_in* helper returned silently when
    # the connector had no EvController, so a test asked for a car to be plugged
    # in, no car was plugged in, and the test carried on to fail as a protocol
    # bug much later.
    ctrl = controller_ev_only
    with pytest.raises(NoSimulatorError) as excinfo:
        ctrl.plug_in(connector_id=7)

    # The message has to say what was there, or the reader cannot tell a wrong
    # connector_id from a config with no simulator at all.
    assert "no EvController instance for connector_id=7" in str(excinfo.value)
    assert "ev_sim_1" in str(excinfo.value)
    assert ctrl._fake_client.publish.call_count == 0


# ---------------------------------------------------------------------------
# The session configuration each plug_in* helper publishes
#
# EverestTestController is what every swapped OCPP config's test goes through, and its
# readiness gate was covered while its session configuration was not: dropping the
# configure_session call left the whole suite green, and the module then synthesised a bare
# AcIec session from cfg.max_current_a, so a PnC test negotiated nothing and died much later
# as an unrelated timeout. One assertion per helper on the published payload.
# ---------------------------------------------------------------------------


def _configure_session_payloads(ctrl):
    out = []
    for call_args in ctrl._fake_client.publish.call_args_list:
        topic = call_args.args[0]
        if topic.endswith("/m2e/configure_session"):
            out.append(json.loads(call_args.args[1]))
    return out


@pytest.mark.parametrize(
    "helper,expected_mode,expected_params",
    [
        ("plug_in", "AcIec", {"charging_current_a": 32.0, "three_phases": False}),
        ("plug_in_ac_iso", "AcIso2",
         {"charging_current_a": 16.0, "three_phases": True,
          "departure_time_s": 86400, "e_amount_wh": 0}),
        ("plug_in_dc_iso", "DcIso2", {"departure_time_s": 86400, "e_amount_wh": 0}),
        ("diode_fail", "AcIec", {"charging_current_a": 32.0, "three_phases": True}),
    ],
)
def test_plug_helpers_configure_the_session_before_plugging(
    controller_with_driver, helper, expected_mode, expected_params
):
    ctrl = controller_with_driver
    # The readiness gate has its own tests; this one is about what gets published once it
    # has been satisfied.
    ctrl._registry._ready = True

    getattr(ctrl, helper)(connector_id=7)

    payloads = [json.loads(p) for t, p in ctrl._fake_client.published if t.endswith("/configure_session")]
    assert len(payloads) == 1, f"{helper} published {len(payloads)} configure_session messages"
    assert payloads[0]["mode"] == expected_mode
    assert payloads[0]["params"] == expected_params

    # Configure before plug, or the module latches a session it was not asked for.
    topics = [t for t, _ in ctrl._fake_client.published]
    assert topics.index(
        next(t for t in topics if t.endswith("/m2e/configure_session"))
    ) < topics.index(next(t for t in topics if t.endswith("/m2e/plug")))


def test_driving_before_start_fails(tmp_path):
    # `start()` is what discovers the simulators; driving before it used to
    # no-op rather than say so.
    core = MagicMock()
    core.everest_config_path = str(tmp_path / "unused.yaml")
    with pytest.raises(NoSimulatorError, match="was not started"):
        EverestTestController(core).plug_in(connector_id=1)


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


def test_ev_lookup_raises_naming_the_instance_when_never_ready(
    monkeypatch, two_ev_config
):
    """Driving an instance that never reported readiness must fail loudly."""
    import everest.testing.core_utils.sim_registry as mod

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
    import everest.testing.core_utils.sim_registry as mod

    fake = _FakeMqttClient()
    monkeypatch.setattr(mod.mqtt, "Client", MagicMock(return_value=fake))

    core = MagicMock()
    core.mqtt_external_prefix = "pfx/"
    core.everest_uuid = "test-uuid"
    core.everest_config_path = str(two_ev_config)

    ctrl = EverestTestController(core)
    ctrl.start()
    fake.deliver_heartbeats()

    ctrl.pause_session(connector_id=1)

    assert any(t.endswith("/m2e/pause_session") for t, _ in fake.published), fake.published


def test_heartbeat_subscriptions_are_armed_before_everest_boots(
    monkeypatch, two_ev_config
):
    """The heartbeat is not retained, so subscribing after boot is a bet."""
    import everest.testing.core_utils.sim_registry as mod

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
        "pfx/everest_api/1/ev_controller/ev_sim_1/m2e/enable",
        "pfx/everest_api/1/ev_controller/ev_sim_2/m2e/enable",
    ]


# ---------------------------------------------------------------------------
# Broker connection state
#
# arm() now runs microseconds after connect() rather than ~1.4s later, so the
# CONNACK race is live: paho writes a SUBSCRIBE issued before the connection is
# established, returns rc=0, and the subscription may simply not exist. That is
# indistinguishable from a silent peer and burns the whole readiness budget.
# ---------------------------------------------------------------------------


def _controller_for(monkeypatch, cfg, fake):
    import everest.testing.core_utils.sim_registry as mod
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


def _started(monkeypatch, cfg, fake):
    ctrl = _controller_for(monkeypatch, cfg, fake)
    ctrl.start()
    ctrl._registry._readiness_timeout = 0.2
    return ctrl


_STATE_TOPIC = "pfx/everest_api/1/ev_controller/ev_sim_1/e2m/state"


def test_stop_releases_the_mqtt_client(controller_ev_only):
    ctrl = controller_ev_only
    client = ctrl._fake_client

    ctrl.stop()

    client.disconnect.assert_called_once()
    client.loop_stop.assert_called_once()
    client._reset_sockets.assert_called_once()
    assert ctrl._mqtt_client is None


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


def test_world_driver_readiness_and_reconnect(monkeypatch, driver_config, fake_client):
    ctrl = _started(monkeypatch, driver_config, fake_client)
    fake_client.deliver("pfx/everest_api/1/ev_controller/car/e2m/heartbeat")
    fake_client.deliver("pfx/everest_api/1/ev_controller/unrelated/e2m/heartbeat")
    with pytest.raises(AssertionError) as excinfo:
        ctrl.plug_out(connector_id=7)
    assert "world" in str(excinfo.value)
    assert not any("ev_driver_sim" in t for t, _ in fake_client.published)
    before = list(fake_client.subscriptions)
    fake_client.fire_connack()
    assert sorted(fake_client.subscriptions) == sorted(before + before)
    fake_client.deliver("pfx/everest_api/1/ev_driver_sim/world/e2m/heartbeat")
    ctrl._registry.require_ready(timeout=0.1)


@pytest.mark.parametrize("helper,mode", [
    ("plug_in", "AcIec"), ("plug_in_ac_iso", "AcIso2"),
    ("plug_in_dc_iso", "DcIso2"), ("diode_fail", "AcIec"),
])
@pytest.mark.parametrize("with_driver", [False, True])
def test_controller_plug_routes_world_and_intent(
    monkeypatch, driver_config, config_path, fake_client, helper, mode, with_driver
):
    ctrl = _started(monkeypatch, driver_config if with_driver else config_path, fake_client)
    fake_client.deliver_heartbeats()
    fake_client.published.clear()
    if not with_driver:
        with pytest.raises(NoSimulatorError, match="EvDriverSim"):
            getattr(ctrl, helper)(connector_id=1)
        assert fake_client.published == []
        ctrl.stop()
        return
    fake_client.auto_configure_ack = True
    try:
        getattr(ctrl, helper)(connector_id=7)
        published = [(t, json.loads(p)) for t, p in fake_client.published]
        assert len(published) == 2
        assert published[0][0] == "pfx/everest_api/1/ev_controller/car/m2e/configure_session"
        assert published[0][1]["mode"] == mode
        assert published[1] == ("pfx/everest_api/1/ev_driver_sim/world/m2e/plug", None)
    finally:
        ctrl.stop()


@pytest.mark.parametrize("helper", ["plug_out", "plug_out_iso"])
@pytest.mark.parametrize("with_driver", [False, True])
def test_controller_unplug_routes_world(
    monkeypatch, driver_config, config_path, fake_client, helper, with_driver
):
    ctrl = _started(monkeypatch, driver_config if with_driver else config_path, fake_client)
    fake_client.deliver_heartbeats()
    fake_client.published.clear()
    if not with_driver:
        with pytest.raises(NoSimulatorError, match="EvDriverSim"):
            getattr(ctrl, helper)(connector_id=1)
        assert fake_client.published == []
        ctrl.stop()
        return
    getattr(ctrl, helper)(connector_id=7)
    expected = [("pfx/everest_api/1/ev_driver_sim/world/m2e/unplug", "null")]
    if helper == "plug_out_iso":
        expected.insert(0, ("pfx/everest_api/1/ev_controller/car/m2e/stop_session", "null"))
    assert fake_client.published == expected


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


@pytest.mark.parametrize("helper", ["plug_in", "plug_in_ac_iso", "plug_in_dc_iso", "diode_fail"])
@pytest.mark.parametrize("status", ["Accepted", "Rejected"])
def test_world_plug_waits_for_car_configuration(monkeypatch, driver_config, fake_client, helper, status):
    ctrl = _started(monkeypatch, driver_config, fake_client)
    fake_client.deliver_heartbeats()
    configured = threading.Event()
    finished = threading.Event()
    errors = []
    publish = fake_client.publish

    def record(topic, payload):
        publish(topic, payload)
        if topic.endswith("/m2e/configure_session"):
            configured.set()

    fake_client.publish = record

    def plug():
        try:
            getattr(ctrl, helper)(connector_id=7)
        except AssertionError as exc:
            errors.append(str(exc))
        finally:
            finished.set()

    worker = threading.Thread(target=plug)
    worker.start()
    try:
        assert configured.wait(1)
        assert not finished.wait(0.05)
        assert not any(t.endswith("/m2e/plug") for t, _ in fake_client.published)
        fake_client.deliver("pfx/everest_api/1/ev_controller/car/e2m/command_ack", json.dumps({
            "command": "configure_session", "status": status, "reason": "invalid session",
        }).encode())
        assert finished.wait(1)
        plugs = [t for t, _ in fake_client.published if t.endswith("/m2e/plug")]
        if status == "Accepted":
            assert not errors
            assert plugs == ["pfx/everest_api/1/ev_driver_sim/world/m2e/plug"]
        else:
            assert errors and "invalid session" in errors[0]
            assert not plugs
    finally:
        ctrl.stop()
        worker.join(timeout=1)


def test_world_plug_is_not_sent_when_configuration_times_out(monkeypatch, driver_config, fake_client):
    ctrl = _started(monkeypatch, driver_config, fake_client)
    fake_client.deliver_heartbeats()
    with pytest.raises(AssertionError, match="timeout.*command_ack"):
        ctrl.plug_in_dc_iso(connector_id=7)
    assert not any(t.endswith("/m2e/plug") for t, _ in fake_client.published)


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


def test_general_controller_routes_scenario_to_paired_world(monkeypatch, driver_config, fake_client):
    ctrl = _started(monkeypatch, driver_config, fake_client)
    fake_client.deliver_heartbeats()
    world = ctrl._registry.ev_driver_by_connector(7)
    calls = []
    world.run_scenario = lambda name, timing=None: calls.append((name, timing))
    ctrl.run_scenario("DcIsoMcs", connector_id=7, timing={"stop_after_ms": 40})
    assert calls == [("DcIsoMcs", {"stop_after_ms": 40})]
    assert not any(topic.endswith("/m2e/run_scenario") for topic, _ in fake_client.published)


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


@pytest.fixture
def controller_with_driver(monkeypatch, driver_config, fake_client):
    ctrl = _started(monkeypatch, driver_config, fake_client)
    fake_client.deliver_heartbeats()
    fake_client.auto_configure_ack = True
    fake_client.published.clear()
    ctrl._fake_client = fake_client
    yield ctrl
    ctrl.stop()


@pytest.mark.parametrize("verb", ["plug", "unplug", "inject_fault", "clear_fault"])
def test_car_driver_has_no_world_commands(registry, verb):
    assert not hasattr(registry.ev_by_connector(1), verb)


def test_diode_fail_waits_for_charging_then_injects(monkeypatch, driver_config):
    fake = _FakeMqttClient()
    fake._reset_sockets = lambda: None
    ctrl = _started(monkeypatch, driver_config, fake)
    fake.deliver_heartbeats()
    fake.auto_configure_ack = True
    ctrl._registry.ev_by_connector(7).subscribe_var("state")
    before = set(threading.enumerate())
    ctrl.diode_fail(connector_id=7)
    workers = set(threading.enumerate()) - before
    try:
        assert len(workers) == 1
        assert all(t.daemon for t in workers)
        assert fake.published[-1][0].endswith('/m2e/plug')
        assert not any(t.endswith('/m2e/inject_fault') for t, _ in fake.published)
        charging_at = time.monotonic()
        fake.deliver("pfx/everest_api/1/ev_controller/car/e2m/state", b'"Charging"')
        time.sleep(0.2)
        assert not any(t.endswith('/m2e/inject_fault') for t, _ in fake.published)
        deadline = time.monotonic() + 5.5
        while time.monotonic() < deadline:
            if any(t.endswith('/m2e/inject_fault') for t, _ in fake.published):
                break
            time.sleep(0.05)
        assert time.monotonic() - charging_at >= 4.9
        assert any(t.endswith('/m2e/inject_fault') and json.loads(p) == {'type': 'DiodeFail'}
                   for t, p in fake.published)
    finally:
        ctrl.stop()
    assert not any(t.is_alive() for t in workers)


def test_stop_cancels_diode_fault_before_charging(monkeypatch, driver_config):
    fake = _FakeMqttClient()
    fake._reset_sockets = lambda: None
    ctrl = _started(monkeypatch, driver_config, fake)
    fake.deliver_heartbeats()
    fake.auto_configure_ack = True
    ctrl._registry.ev_by_connector(7).subscribe_var("state")
    before = set(threading.enumerate())
    ctrl.diode_fail(connector_id=7)
    workers = set(threading.enumerate()) - before
    ctrl.stop()
    assert not any(t.is_alive() for t in workers)
    assert not any(t.endswith('/m2e/inject_fault') for t, _ in fake.published)


def test_refused_connack_times_out_without_starting_everest(monkeypatch, two_ev_config):
    fake = _FakeMqttClient()
    fake.fire_connack = lambda: fake.on_connect(fake, None, {}, 5, None)
    ctrl = _controller_for(monkeypatch, two_ev_config, fake)
    with pytest.raises(AssertionError, match="broker.*timed out"):
        ctrl.start(broker_connect_timeout=0.02)
    assert not any(kind == "everest_start" for kind, _ in fake.events)
