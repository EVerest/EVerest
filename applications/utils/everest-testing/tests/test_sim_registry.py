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
