# SPDX-License-Identifier: Apache-2.0
# Copyright Pionix GmbH and Contributors to EVerest
"""Unit tests for EvSimulatorTestController.

These tests exercise the publish surface and state demux logic of
`EvSimulatorTestController` without booting a real EVerest manager.

* Every `m2e/*` publish method is asserted against the resolved
  topic and the JSON payload that the C++ codec's `from_json` will
  consume on the simulator side.
* `BptParams` dict shapes are round-tripped through
  `json.dumps`/`json.loads` to confirm the field names match the
  C++ codec contract (`discharge_max_current_limit`, etc.).
* `StateCollector` is fed synthetic MQTT messages via its registered
  callbacks and asserted to demux them into the right buckets.
"""

import json
import sys
import threading
import time
import types
from typing import Any, Dict
from unittest.mock import MagicMock, call

import pytest


# Stub out the heavy `everest.testing.core_utils.everest_core` import
# so the controller module loads without booting libeverestpy. The
# controller only uses `EverestCore` as a type hint, so a placeholder
# class is sufficient.
_stub_everest_core_mod = types.ModuleType(
    "everest.testing.core_utils.everest_core"
)


class _StubEverestCore:  # pragma: no cover - placeholder only
    pass


_stub_everest_core_mod.EverestCore = _StubEverestCore  # type: ignore[attr-defined]
sys.modules.setdefault(
    "everest.testing.core_utils.everest_core", _stub_everest_core_mod
)

# Ensure repo-local `src/` is importable when pytest is invoked
# directly from the package root or via the worktree path.
import os  # noqa: E402

_PKG_SRC = os.path.normpath(
    os.path.join(os.path.dirname(__file__), "..", "src")
)
if _PKG_SRC not in sys.path:
    sys.path.insert(0, _PKG_SRC)

from everest.testing.core_utils.controller.evsim_test_controller import (  # noqa: E402
    DEFAULT_BPT,
    EvSimulatorTestController,
    StateCollector,
)


# ---------------------------------------------------------------------------
# Fixtures
# ---------------------------------------------------------------------------


@pytest.fixture
def mock_everest_core() -> MagicMock:
    """Mimic the surface of `EverestCore` that the controller reads."""
    core = MagicMock()
    core.mqtt_external_prefix = "ext/"
    core.everest_uuid = "uuid-1234"
    core.everest_config = {
        "active_modules": {
            "ev_manager": {
                "module": "EvSimulator",
                "config_module": {},
            },
            "other": {"module": "EvseManager"},
        }
    }
    return core


@pytest.fixture
def controller(monkeypatch, mock_everest_core) -> EvSimulatorTestController:
    """Construct a controller with paho.Client fully mocked.

    The MQTT client's `publish`, `connect`, `loop_start`,
    `subscribe`, `message_callback_add`, and `disconnect` methods
    are all MagicMocks so we can assert on `publish` calls directly.
    """
    import everest.testing.core_utils.controller.evsim_test_controller as mod

    fake_client = MagicMock()
    fake_client.publish = MagicMock()
    fake_client.connect = MagicMock()
    fake_client.loop_start = MagicMock()
    fake_client.loop_stop = MagicMock()
    fake_client.disconnect = MagicMock()
    fake_client.subscribe = MagicMock()
    fake_client.message_callback_add = MagicMock()

    # Replace the Client factory so __init__ uses our mock.
    monkeypatch.setattr(mod.mqtt, "Client", MagicMock(return_value=fake_client))

    ctrl = EvSimulatorTestController(mock_everest_core)
    # Hand back both for convenience.
    ctrl._fake_client = fake_client  # type: ignore[attr-defined]
    return ctrl


# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------


def _last_publish(ctrl: EvSimulatorTestController):
    """Return (topic, payload_dict) from the most recent publish call."""
    fake = ctrl._fake_client  # type: ignore[attr-defined]
    assert fake.publish.call_count >= 1
    args, _kwargs = fake.publish.call_args
    topic, payload_str = args
    return topic, json.loads(payload_str)


def _all_publishes(ctrl: EvSimulatorTestController):
    """Return list of (topic, payload_dict) for every publish call."""
    fake = ctrl._fake_client  # type: ignore[attr-defined]
    out = []
    for call in fake.publish.call_args_list:
        args, _kwargs = call
        topic, payload_str = args
        out.append((topic, json.loads(payload_str)))
    return out


def _make_msg(topic: str, payload: Any):
    """Build a minimal paho-shaped message object for callback invocation."""
    msg = types.SimpleNamespace()
    msg.topic = topic
    msg.payload = json.dumps(payload).encode("utf-8")
    return msg


# ---------------------------------------------------------------------------
# Module-id resolution + base topic construction
# ---------------------------------------------------------------------------


def test_module_id_resolved_from_active_modules(controller):
    assert controller.base_m2e == "ext/everest_api/1/ev_simulator/ev_manager/m2e"
    assert controller.base_e2m == "ext/everest_api/1/ev_simulator/ev_manager/e2m"


def test_module_id_falls_back_to_default_when_no_evsimulator(monkeypatch):
    import everest.testing.core_utils.controller.evsim_test_controller as mod

    fake_client = MagicMock()
    monkeypatch.setattr(mod.mqtt, "Client", MagicMock(return_value=fake_client))

    core = MagicMock()
    core.mqtt_external_prefix = ""
    core.everest_uuid = "u"
    core.everest_config = {"active_modules": {"x": {"module": "Other"}}}

    ctrl = EvSimulatorTestController(core)
    assert ctrl.base_m2e.endswith("/ev_manager/m2e")


# ---------------------------------------------------------------------------
# Tests: one per public publishing method
# ---------------------------------------------------------------------------


def test_start_publishes_enable_true(controller):
    controller.start(connector_id=1)
    topic, payload = _last_publish(controller)
    assert topic == "ext/everest_api/1/ev_simulator/ev_manager/m2e/enable"
    # Bare bool: the C++ CommandRouter deserializes m2e/enable into `bool`.
    assert payload is True


def test_plug_in_publishes_configure_session_then_plug_ac_iec(monkeypatch, controller):
    # Defeat the synchronous wait_for_state inside plug_in by stubbing it.
    monkeypatch.setattr(
        controller.state_collector, "wait_for_state", lambda *_a, **_k: True
    )
    controller.plug_in(connector_id=1)
    publishes = _all_publishes(controller)
    # configure-before-plug model: configure_session is published first,
    # then plug so the module latches the session config before connecting.
    assert publishes[0] == (
        "ext/everest_api/1/ev_simulator/ev_manager/m2e/configure_session",
        {
            "mode": "AcIec",
            "params": {"charging_current_a": 32.0, "three_phases": True},
        },
    )
    assert publishes[1] == (
        "ext/everest_api/1/ev_simulator/ev_manager/m2e/plug",
        {},
    )


def test_plug_in_ac_iso_uses_AcIso2_mode_and_optional_payment(controller):
    controller.plug_in_ac_iso(payment_type="contract")
    publishes = _all_publishes(controller)
    # configure-before-plug: configure_session first, then plug.
    assert publishes[0][0].endswith("/m2e/configure_session")
    assert publishes[0][1] == {
        "mode": "AcIso2",
        "params": {
            "charging_current_a": 16.0,
            "three_phases": True,
            "payment": "contract",
        },
    }
    assert publishes[1][0].endswith("/m2e/plug")
    assert publishes[1][1] == {}


def test_plug_in_ac_iso_omits_payment_when_none(controller):
    controller.plug_in_ac_iso()
    publishes = _all_publishes(controller)
    assert "payment" not in publishes[0][1]["params"]
    assert publishes[0][1]["mode"] == "AcIso2"


def test_plug_in_dc_iso_uses_DcIso2_mode(controller):
    controller.plug_in_dc_iso(payment_type="eim")
    publishes = _all_publishes(controller)
    assert publishes[0][0].endswith("/m2e/configure_session")
    assert publishes[0][1] == {
        "mode": "DcIso2",
        "params": {"payment": "eim"},
    }


def test_plug_in_dc_d20_uses_DcIsoD20_mode(controller):
    controller.plug_in_dc_d20()
    publishes = _all_publishes(controller)
    assert publishes[0][0].endswith("/m2e/configure_session")
    assert publishes[0][1] == {"mode": "DcIsoD20", "params": {}}


def test_plug_in_dc_bpt_attaches_default_bpt_params(controller):
    controller.plug_in_dc_bpt()
    publishes = _all_publishes(controller)
    # configure-before-plug: configure_session (index 0) before plug (index 1).
    assert publishes[0][0].endswith("/m2e/configure_session")
    body = publishes[0][1]
    assert body["mode"] == "DcIsoD20"
    bpt = body["params"]["bpt"]
    assert bpt == DEFAULT_BPT
    # Spot-check the field names the C++ codec consumes.
    for key in (
        "discharge_max_current_limit",
        "discharge_max_power_limit",
        "discharge_target_current",
        "discharge_minimal_soc",
    ):
        assert key in bpt


def test_plug_in_dc_bpt_passes_custom_params(controller):
    custom: Dict[str, float] = {
        "discharge_max_current_limit": 99.0,
        "discharge_max_power_limit": 22000.0,
        "discharge_target_current": 12.5,
        "discharge_minimal_soc": 5.0,
    }
    controller.plug_in_dc_bpt(bpt_params=custom)
    publishes = _all_publishes(controller)
    assert publishes[0][1]["params"]["bpt"] == custom


def test_plug_in_dc_mcs_sends_mcs_enabled_flag(controller):
    controller.plug_in_dc_mcs()
    publishes = _all_publishes(controller)
    assert publishes[0][0].endswith("/m2e/configure_session")
    body = publishes[0][1]
    assert body["mode"] == "DcIsoD20"
    assert body["params"] == {"mcs_enabled": True}


def test_plug_in_dc_mcs_ignores_legacy_profile_arg(controller):
    controller.plug_in_dc_mcs(mcs_profile={"some_field": 1})
    publishes = _all_publishes(controller)
    assert publishes[0][1]["params"] == {"mcs_enabled": True}


def test_plug_out_publishes_unplug(controller):
    controller.plug_out()
    topic, payload = _last_publish(controller)
    assert topic.endswith("/m2e/unplug")
    assert payload == {}


def test_plug_out_iso_stops_then_unplugs(monkeypatch, controller):
    monkeypatch.setattr(
        controller.state_collector, "wait_for_state_not", lambda *_a, **_k: True
    )
    controller.plug_out_iso()
    publishes = _all_publishes(controller)
    assert publishes[0][0].endswith("/m2e/stop_session")
    assert publishes[0][1] == {}
    assert publishes[1][0].endswith("/m2e/unplug")
    assert publishes[1][1] == {}


def test_pause_session_publishes_pause_session(controller):
    controller.pause_session()
    topic, payload = _last_publish(controller)
    assert topic.endswith("/m2e/pause_session")
    assert payload == {}


def test_resume_session_publishes_resume_session(controller):
    controller.resume_session()
    topic, payload = _last_publish(controller)
    assert topic.endswith("/m2e/resume_session")
    assert payload == {}


def test_diode_fail_publishes_inject_fault_diode(controller):
    controller.diode_fail()
    topic, payload = _last_publish(controller)
    assert topic.endswith("/m2e/inject_fault")
    assert payload == {"type": "DiodeFail"}


@pytest.mark.parametrize(
    "fault",
    ["DiodeFail", "RcdError", "CpErrorE", "SlacTimeout", "V2GTimeout", "Internal"],
)
def test_inject_fault_supports_each_fault_type(controller, fault):
    controller.inject_fault(fault)
    topic, payload = _last_publish(controller)
    assert topic.endswith("/m2e/inject_fault")
    assert payload == {"type": fault}


def test_clear_fault_publishes_clear_fault(controller):
    controller.clear_fault()
    topic, payload = _last_publish(controller)
    assert topic.endswith("/m2e/clear_fault")
    assert payload == {}


def test_run_scenario_publishes_name(controller):
    controller.run_scenario("happy_path")
    topic, payload = _last_publish(controller)
    assert topic.endswith("/m2e/run_scenario")
    assert payload == {"name": "happy_path"}


def test_run_scenario_without_timing_omits_timing_key(controller):
    """When no timing is given, the wire payload must not carry a `timing` key."""
    controller.run_scenario("AcIecBasic")
    topic, payload = _last_publish(controller)
    assert topic.endswith("/m2e/run_scenario")
    assert payload == {"name": "AcIecBasic"}
    assert "timing" not in payload


def test_run_scenario_with_timing_includes_timing_dict(controller):
    """When timing is provided, the wire payload embeds it verbatim."""
    timing = {
        "pause_at_ms": 2000,
        "resume_at_ms": 4000,
        "stop_after_ms": 6000,
        "unplug_after_ms": 7000,
    }
    controller.run_scenario("AcIecPauseResume", timing=timing)
    topic, payload = _last_publish(controller)
    assert topic.endswith("/m2e/run_scenario")
    assert payload == {"name": "AcIecPauseResume", "timing": timing}


def test_run_scenario_with_partial_timing_only_sends_provided_fields(controller):
    """Only the fields present in the `timing` dict are sent on the wire."""
    timing = {"stop_after_ms": 5000}
    controller.run_scenario("AcIecBasic", timing=timing)
    _topic, payload = _last_publish(controller)
    assert payload["timing"] == {"stop_after_ms": 5000}
    # Other timing fields must not be synthesized.
    assert "pause_at_ms" not in payload["timing"]
    assert "resume_at_ms" not in payload["timing"]


def test_run_scenario_with_empty_timing_omits_timing_key(controller):
    """An empty dict for `timing` is treated the same as None — key omitted."""
    controller.run_scenario("AcIecBasic", timing={})
    _topic, payload = _last_publish(controller)
    assert "timing" not in payload


def test_set_soc_publishes_percent(controller):
    controller.set_soc(42.5)
    topic, payload = _last_publish(controller)
    assert topic.endswith("/m2e/set_soc")
    assert payload == {"soc_pct": 42.5}


def test_set_charging_current_publishes_current_and_phases(controller):
    controller.set_charging_current(16.0, three_phases=False)
    topic, payload = _last_publish(controller)
    assert topic.endswith("/m2e/set_charging_current")
    assert payload == {"current_a": 16.0, "three_phases": False}


def test_set_charging_current_with_ramp_ms(controller):
    controller.set_charging_current(20.0, three_phases=True, ramp_ms=2500)
    topic, payload = _last_publish(controller)
    assert topic.endswith("/m2e/set_charging_current")
    assert payload == {
        "current_a": 20.0,
        "three_phases": True,
        "ramp_ms": 2500,
    }


def test_set_charging_current_without_ramp_ms_omits_key(controller):
    controller.set_charging_current(16.0, three_phases=False)
    _topic, payload = _last_publish(controller)
    assert "ramp_ms" not in payload


def test_ramp_to_current_converts_duration_to_ramp_ms(controller):
    controller.ramp_to_current(32.0, three_phases=True, duration_s=5)
    topic, payload = _last_publish(controller)
    assert topic.endswith("/m2e/set_charging_current")
    assert payload == {
        "current_a": 32.0,
        "three_phases": True,
        "ramp_ms": 5000,
    }


def test_play_charging_curve_publishes_configure_session_with_curve(controller):
    points = [
        {"t_offset_ms": 0, "current_a": 6.0, "three_phases": True},
        {
            "t_offset_ms": 1000,
            "current_a": 16.0,
            "three_phases": True,
            "ramp_ms": 500,
        },
    ]
    controller.play_charging_curve(points, loop=False)
    topic, payload = _last_publish(controller)
    assert topic.endswith("/m2e/configure_session")
    assert payload["mode"] == "DcIso2"
    assert "curve" in payload["params"]
    assert payload["params"]["curve"]["points"] == points
    assert payload["params"]["curve"]["loop"] is False


# ---------------------------------------------------------------------------
# query_state: cached value populated by e2m/state callback
# ---------------------------------------------------------------------------


def test_query_state_returns_cached_value_from_state_callback(controller):
    # Resolve the registered state callback and feed it a synthetic
    # message — this is what paho would do for a real e2m/state event.
    fake = controller._fake_client  # type: ignore[attr-defined]
    callbacks = {
        call.args[0]: call.args[1]
        for call in fake.message_callback_add.call_args_list
    }
    state_topic = controller.base_e2m + "/state"
    assert state_topic in callbacks
    callbacks[state_topic](None, None, _make_msg(state_topic, "Charging"))

    assert controller.query_state() == "Charging"
    # Verify a subsequent state update overwrites the cache.
    callbacks[state_topic](None, None, _make_msg(state_topic, "Idle"))
    assert controller.query_state() == "Idle"


# ---------------------------------------------------------------------------
# JSON round-trip: BPT field-name contract with C++ codec
# ---------------------------------------------------------------------------


def test_bpt_default_dict_round_trips_through_json():
    encoded = json.dumps(DEFAULT_BPT)
    decoded = json.loads(encoded)
    assert decoded == DEFAULT_BPT
    # The C++ codec's from_json reads these exact keys.
    for key in (
        "discharge_max_current_limit",
        "discharge_max_power_limit",
        "discharge_target_current",
        "discharge_minimal_soc",
    ):
        assert key in decoded
        assert isinstance(decoded[key], float)


# ---------------------------------------------------------------------------
# StateCollector: wildcard subscription demux via injected mock messages
# ---------------------------------------------------------------------------


def _build_collector():
    """Create a StateCollector against a MagicMock paho client."""
    client = MagicMock()
    collector = StateCollector(client, "ext/everest_api/1/ev_simulator/ev_manager/e2m")
    # Recover the registered callbacks so tests can drive them directly.
    callbacks = {
        call.args[0]: call.args[1]
        for call in client.message_callback_add.call_args_list
    }
    return collector, client, callbacks


def test_state_collector_subscribes_to_wildcard_topic():
    _collector, client, _ = _build_collector()
    client.subscribe.assert_called_once_with(
        "ext/everest_api/1/ev_simulator/ev_manager/e2m/+"
    )


def test_state_collector_demuxes_state_messages():
    collector, _client, callbacks = _build_collector()
    state_topic = "ext/everest_api/1/ev_simulator/ev_manager/e2m/state"
    callbacks[state_topic](None, None, _make_msg(state_topic, "PluggedIn"))
    callbacks[state_topic](None, None, _make_msg(state_topic, "Charging"))
    assert collector.states == ["PluggedIn", "Charging"]
    assert collector.last_state == "Charging"
    assert collector.faults == []
    assert collector.command_acks == []


def test_state_collector_demuxes_fault_messages():
    collector, _client, callbacks = _build_collector()
    fault_topic = "ext/everest_api/1/ev_simulator/ev_manager/e2m/fault"
    payload = {"type": "DiodeFail", "active": True}
    callbacks[fault_topic](None, None, _make_msg(fault_topic, payload))
    assert collector.faults == [payload]
    assert collector.states == []
    assert collector.command_acks == []


def test_state_collector_demuxes_command_ack_messages():
    collector, _client, callbacks = _build_collector()
    ack_topic = "ext/everest_api/1/ev_simulator/ev_manager/e2m/command_ack"
    payload = {"command": "plug", "ok": True}
    callbacks[ack_topic](None, None, _make_msg(ack_topic, payload))
    assert collector.command_acks == [payload]
    assert collector.states == []
    assert collector.faults == []


def test_state_collector_ignores_malformed_state_payload():
    collector, _client, callbacks = _build_collector()
    state_topic = "ext/everest_api/1/ev_simulator/ev_manager/e2m/state"
    bad = types.SimpleNamespace()
    bad.topic = state_topic
    bad.payload = b"\xff\xfe not json"
    callbacks[state_topic](None, None, bad)
    # State payload must decode to a string; numeric/object inputs are
    # also dropped to avoid corrupting `last_state` semantics.
    callbacks[state_topic](None, None, _make_msg(state_topic, {"not": "a string"}))
    assert collector.states == []
    assert collector.last_state is None


def test_state_collector_wait_for_state_returns_true_when_already_matching():
    collector, _client, callbacks = _build_collector()
    state_topic = "ext/everest_api/1/ev_simulator/ev_manager/e2m/state"
    callbacks[state_topic](None, None, _make_msg(state_topic, "Charging"))
    assert collector.wait_for_state("Charging", timeout=0.1) is True


def test_state_collector_wait_for_state_times_out_when_no_match():
    collector, _client, _callbacks = _build_collector()
    assert collector.wait_for_state("Charging", timeout=0.05) is False


def test_state_collector_wait_for_state_catches_transient_state():
    # The FSM passes through "Charging" transiently and ends on "Idle".
    # A last_state-only wait would miss it and time out; matching the
    # full history from the call's high-water mark catches it.
    collector, _client, callbacks = _build_collector()
    state_topic = "ext/everest_api/1/ev_simulator/ev_manager/e2m/state"

    def feed_after_block():
        time.sleep(0.05)  # let wait_for_state block first
        callbacks[state_topic](None, None, _make_msg(state_topic, "PluggedIn"))
        callbacks[state_topic](None, None, _make_msg(state_topic, "Charging"))
        callbacks[state_topic](None, None, _make_msg(state_topic, "Idle"))

    t = threading.Thread(target=feed_after_block)
    t.start()
    try:
        assert collector.wait_for_state("Charging", timeout=2.0) is True
        assert collector.last_state == "Idle"  # moved past the transient
    finally:
        t.join()


def test_state_collector_demuxes_ev_info_and_bsp_event():
    collector, _client, callbacks = _build_collector()
    ev_info_topic = "ext/everest_api/1/ev_simulator/ev_manager/e2m/ev_info"
    bsp_event_topic = "ext/everest_api/1/ev_simulator/ev_manager/e2m/bsp_event"
    callbacks[ev_info_topic](
        None, None, _make_msg(ev_info_topic, {"soc_pct": 12.5})
    )
    callbacks[bsp_event_topic](
        None, None, _make_msg(bsp_event_topic, {"event": "PowerOn"})
    )
    assert collector.ev_infos == [{"soc_pct": 12.5}]
    assert collector.bsp_events == [{"event": "PowerOn"}]
    assert collector.states == []


def test_wait_for_fault_matches_type_and_times_out():
    collector, _client, callbacks = _build_collector()
    fault_topic = "ext/everest_api/1/ev_simulator/ev_manager/e2m/fault"
    callbacks[fault_topic](
        None, None, _make_msg(fault_topic, {"type": "V2GTimeout"})
    )
    callbacks[fault_topic](
        None, None, _make_msg(fault_topic, {"type": "DiodeFail", "message": "x"})
    )
    got = collector.wait_for_fault("DiodeFail", timeout=0.1)
    assert got == {"type": "DiodeFail", "message": "x"}
    assert collector.wait_for_fault("RcdError", timeout=0.05) is None


def test_wait_for_soc_progress_detects_increase():
    collector, _client, callbacks = _build_collector()
    ev_info_topic = "ext/everest_api/1/ev_simulator/ev_manager/e2m/ev_info"

    def feed_after_block():
        time.sleep(0.05)
        callbacks[ev_info_topic](
            None, None, _make_msg(ev_info_topic, {"soc_pct": 30.0})
        )
        callbacks[ev_info_topic](
            None, None, _make_msg(ev_info_topic, {"soc_pct": 30.5})
        )

    t = threading.Thread(target=feed_after_block)
    t.start()
    try:
        assert collector.wait_for_soc_progress(
            min_increase=0.1, timeout=2.0
        ) is True
    finally:
        t.join()


def test_wait_for_soc_progress_times_out_when_flat():
    collector, _client, callbacks = _build_collector()
    ev_info_topic = "ext/everest_api/1/ev_simulator/ev_manager/e2m/ev_info"
    callbacks[ev_info_topic](
        None, None, _make_msg(ev_info_topic, {"soc_pct": 42.0})
    )
    callbacks[ev_info_topic](
        None, None, _make_msg(ev_info_topic, {"soc_pct": 42.0})
    )
    assert collector.wait_for_soc_progress(min_increase=0.1, timeout=0.05) is False


def test_wait_for_command_ack_matches_command_and_status():
    collector, _client, callbacks = _build_collector()
    ack_topic = "ext/everest_api/1/ev_simulator/ev_manager/e2m/command_ack"

    def feed_after_block():
        time.sleep(0.05)
        # An Accepted ack for a different command must not match.
        callbacks[ack_topic](
            None, None, _make_msg(ack_topic, {"command": "plug", "status": "Accepted"})
        )
        # A Rejected ack for the target command must match.
        callbacks[ack_topic](
            None, None,
            _make_msg(ack_topic, {"command": "run_scenario", "status": "Rejected",
                                   "reason": "timing override stop_after_ms not applicable"})
        )

    t = threading.Thread(target=feed_after_block)
    t.start()
    try:
        got = collector.wait_for_command_ack("run_scenario", "Rejected", timeout=2.0)
        assert got is not None
        assert got["status"] == "Rejected"
        assert "reason" in got
    finally:
        t.join()


def test_wait_for_command_ack_returns_none_on_timeout():
    collector, _client, _callbacks = _build_collector()
    got = collector.wait_for_command_ack("run_scenario", "Rejected", timeout=0.05)
    assert got is None


def test_wait_for_command_ack_ignores_pre_call_acks():
    """Acks already in the list before the call must not be matched."""
    collector, _client, callbacks = _build_collector()
    ack_topic = "ext/everest_api/1/ev_simulator/ev_manager/e2m/command_ack"
    # Pre-populate a matching ack so it is at index 0 before the call.
    callbacks[ack_topic](
        None, None,
        _make_msg(ack_topic, {"command": "run_scenario", "status": "Rejected"})
    )
    # Call after the ack is already present — should NOT match the stale entry.
    got = collector.wait_for_command_ack("run_scenario", "Rejected", timeout=0.05)
    assert got is None


def test_stop_disconnects_before_stopping_network_loop(controller):
    # paho requires the network loop to still be running when
    # disconnect() is called so the DISCONNECT packet is flushed.
    manager = MagicMock()
    manager.attach_mock(controller._fake_client.disconnect, "disconnect")
    manager.attach_mock(controller._fake_client.loop_stop, "loop_stop")

    controller.stop()

    assert manager.mock_calls == [call.disconnect(), call.loop_stop()]
