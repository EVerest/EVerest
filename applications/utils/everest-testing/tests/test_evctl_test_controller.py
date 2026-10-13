# SPDX-License-Identifier: Apache-2.0
# Copyright Pionix GmbH and Contributors to EVerest
"""Unit tests for EvControllerTestController.

These tests exercise the publish surface and state demux logic of
`EvControllerTestController` without booting a real EVerest manager.

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
import yaml
import sys
import threading
import time
import types
from typing import Any, Dict
from unittest.mock import MagicMock

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

from everest.testing.core_utils.controller.evctl_test_controller import (  # noqa: E402
    DEFAULT_BPT,
    EvControllerTestController,
    StateCollector,
)
from everest.testing.core_utils.sim_registry import NoSimulatorError  # noqa: E402


# ---------------------------------------------------------------------------
# Fixtures
# ---------------------------------------------------------------------------


@pytest.fixture
def mock_everest_core(tmp_path) -> MagicMock:
    """Mimic the surface of `EverestCore` that the controller reads."""
    core = MagicMock()
    core.mqtt_external_prefix = "ext/"
    core.everest_uuid = "uuid-1234"
    core.everest_config = {
        "active_modules": {
            "ev_manager": {
                "module": "EvController",
                "config_module": {"connector_id": 1},
            },
            "other": {"module": "EvseManager"},
        }
    }
    core.everest_config_path = tmp_path / "config.yaml"
    core.everest_config_path.write_text(yaml.safe_dump(core.everest_config))
    return core


@pytest.fixture
def controller(monkeypatch, mock_everest_core) -> EvControllerTestController:
    """Construct a controller with paho.Client fully mocked.

    The MQTT client's `publish`, `connect`, `loop_start`,
    `subscribe`, `message_callback_add`, and `disconnect` methods
    are all MagicMocks so we can assert on `publish` calls directly.
    """
    import everest.testing.core_utils.sim_registry as mod

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

    mock_everest_core.everest_config_path.write_text(yaml.safe_dump(mock_everest_core.everest_config))
    fake_client.connect.side_effect = lambda *_a: fake_client.on_connect(fake_client, None, {}, 0)
    ctrl = EvControllerTestController(mock_everest_core)
    # Hand back both for convenience.
    ctrl._fake_client = fake_client  # type: ignore[attr-defined]
    return ctrl


# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------


def _last_publish(ctrl: EvControllerTestController):
    """Return (topic, payload_dict) from the most recent publish call."""
    fake = ctrl._fake_client  # type: ignore[attr-defined]
    assert fake.publish.call_count >= 1
    args, _kwargs = fake.publish.call_args
    topic, payload_str = args
    return topic, json.loads(payload_str)


def _all_publishes(ctrl: EvControllerTestController):
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


# ---------------------------------------------------------------------------
# Tests: one per public publishing method
# ---------------------------------------------------------------------------


def test_start_publishes_enable_true(monkeypatch, controller):
    # Stub the readiness wait, as the plug_in tests below do. Without it the
    # mocked client never produces an e2m/state, so start() re-publishes for
    # its whole 15 s bound and this test costs 15 s to assert one payload.
    monkeypatch.setattr(
        controller.state_collector, "wait_for_state", lambda *_a, **_k: True
    )
    controller.start(connector_id=1)
    topic, payload = _last_publish(controller)
    assert topic == "ext/everest_api/1/ev_controller/ev_manager/m2e/enable"
    # Bare bool: the C++ CommandRouter deserializes m2e/enable into `bool`.
    assert payload is True
    # One publish, not fifteen: the retry loop must exit on the first success
    # rather than run to its deadline regardless.
    fake = controller._fake_client  # type: ignore[attr-defined]
    assert fake.publish.call_count == 1


def test_start_gives_up_after_its_bound_and_still_publishes(controller):
    # The FSM never reports Unplugged, so start() exhausts the retry loop.
    # `timeout` keeps that path cheap to cover.
    controller.start(connector_id=1, timeout=0.2)
    fake = controller._fake_client  # type: ignore[attr-defined]
    # It retried, and it left a final enable on the wire so a caller's own
    # wait_for_state is what surfaces the failure.
    assert fake.publish.call_count >= 2
    topic, payload = _last_publish(controller)
    assert topic == "ext/everest_api/1/ev_controller/ev_manager/m2e/enable"
    assert payload is True


def test_plug_in_publishes_configure_session_then_plug_ac_iec(monkeypatch, world_controller):
    controller = world_controller
    # Defeat the synchronous wait_for_state inside plug_in by stubbing it.
    monkeypatch.setattr(
        controller.state_collector, "wait_for_state", lambda *_a, **_k: True
    )
    controller.plug_in(connector_id=1)
    publishes = _all_publishes(controller)
    # configure-before-plug model: configure_session is published first,
    # then plug so the module latches the session config before connecting.
    assert publishes[0] == (
        "ext/everest_api/1/ev_controller/ev_manager/m2e/configure_session",
        {
            "mode": "AcIec",
            "params": {"charging_current_a": 32.0, "three_phases": True},
        },
    )
    assert publishes[1] == (
        "ext/everest_api/1/ev_driver_sim/world/m2e/plug",
        None,
    )


def test_plug_in_ac_iso_uses_AcIso2_mode_and_optional_payment(world_controller):
    controller = world_controller
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
    assert publishes[1][1] is None


def test_plug_in_ac_iso_omits_payment_when_none(world_controller):
    controller = world_controller
    controller.plug_in_ac_iso()
    publishes = _all_publishes(controller)
    assert "payment" not in publishes[0][1]["params"]
    assert publishes[0][1]["mode"] == "AcIso2"


def test_plug_in_dc_iso_uses_DcIso2_mode(world_controller):
    controller = world_controller
    controller.plug_in_dc_iso(payment_type="eim")
    publishes = _all_publishes(controller)
    assert publishes[0][0].endswith("/m2e/configure_session")
    assert publishes[0][1] == {
        "mode": "DcIso2",
        "params": {"payment": "eim"},
    }


def test_plug_in_dc_d20_uses_DcIsoD20_mode(world_controller):
    controller = world_controller
    controller.plug_in_dc_d20()
    publishes = _all_publishes(controller)
    assert publishes[0][0].endswith("/m2e/configure_session")
    assert publishes[0][1] == {"mode": "DcIsoD20", "params": {}}


def test_plug_in_dc_d20_bpt_none_sends_no_bpt(world_controller):
    controller = world_controller
    controller.plug_in_dc_d20(bpt=None)
    publishes = _all_publishes(controller)
    assert publishes[0][0].endswith("/m2e/configure_session")
    assert "bpt" not in publishes[0][1]["params"]


def test_plug_in_dc_d20_bpt_true_sends_default_bpt(world_controller):
    controller = world_controller
    controller.plug_in_dc_d20(bpt=True)
    publishes = _all_publishes(controller)
    # configure-before-plug: configure_session (index 0) before plug (index 1).
    assert publishes[0][0].endswith("/m2e/configure_session")
    assert publishes[0][1] == {"mode": "DcIsoD20", "params": {"bpt": DEFAULT_BPT}}


def test_plug_in_dc_d20_bpt_dict_sends_that_dict(world_controller):
    controller = world_controller
    custom: Dict[str, float] = {
        "discharge_max_current_limit": 99.0,
        "discharge_max_power_limit": 22000.0,
        "discharge_target_current": 12.5,
        "discharge_minimal_soc": 5.0,
    }
    controller.plug_in_dc_d20(bpt=custom)
    publishes = _all_publishes(controller)
    assert publishes[0][1] == {"mode": "DcIsoD20", "params": {"bpt": custom}}


def test_plug_in_dc_d20_mcs_sends_mcs_enabled_flag(world_controller):
    controller = world_controller
    controller.plug_in_dc_d20(mcs=True)
    publishes = _all_publishes(controller)
    assert publishes[0][0].endswith("/m2e/configure_session")
    assert publishes[0][1] == {"mode": "DcIsoD20", "params": {"mcs_enabled": True}}


def test_plug_out_publishes_unplug(world_controller):
    controller = world_controller
    controller.plug_out()
    topic, payload = _last_publish(controller)
    assert topic.endswith("/m2e/unplug")
    assert payload is None


def test_plug_out_iso_stops_then_unplugs(monkeypatch, world_controller):
    controller = world_controller
    monkeypatch.setattr(
        controller.state_collector, "wait_for_state_not", lambda *_a, **_k: True
    )
    controller.plug_out_iso()
    publishes = _all_publishes(controller)
    assert publishes[0][0].endswith("/m2e/stop_session")
    assert publishes[0][1] == {}
    assert publishes[1][0].endswith("/m2e/unplug")
    assert publishes[1][1] is None


def test_stop_session_defaults_to_the_clean_teardown(controller):
    controller.stop_session()
    topic, payload = _last_publish(controller)
    assert topic.endswith("/m2e/stop_session")
    # The default payload stays exactly what callers published before the
    # teardown selector existed; the C++ side reads a missing key as no abort.
    assert payload == {}


def test_stop_session_abort_asks_for_the_abrupt_teardown(controller):
    controller.stop_session(abort=True)
    topic, payload = _last_publish(controller)
    assert topic.endswith("/m2e/stop_session")
    assert payload == {"abort": True}


def test_plug_out_iso_can_stage_an_aborted_teardown(monkeypatch, world_controller):
    controller = world_controller
    monkeypatch.setattr(
        controller.state_collector, "wait_for_state_not", lambda *_a, **_k: True
    )
    controller.plug_out_iso(abort=True)
    publishes = _all_publishes(controller)
    assert publishes[0][0].endswith("/m2e/stop_session")
    assert publishes[0][1] == {"abort": True}
    assert publishes[1][0].endswith("/m2e/unplug")


def test_set_present_values_sends_only_the_overridden_field(controller):
    controller.set_present_values(present_voltage=350.0)
    topic, payload = _last_publish(controller)
    assert topic.endswith("/m2e/set_present_values")
    # Omitting the other field is what keeps it echoing the delivered value
    # instead of being reported as a measured zero.
    assert payload == {"present_voltage": 350.0}


def test_set_present_values_sends_both_when_both_are_given(controller):
    controller.set_present_values(present_voltage=350.0, present_active_power=1000.0)
    _topic, payload = _last_publish(controller)
    assert payload == {"present_voltage": 350.0, "present_active_power": 1000.0}


def test_set_present_values_with_no_arguments_overrides_nothing(controller):
    controller.set_present_values()
    _topic, payload = _last_publish(controller)
    assert payload == {}


def test_set_present_values_keeps_an_explicit_zero(controller):
    # A deliberate zero is a measurement the test asked for, so it must be
    # sent rather than dropped as if it were absent.
    controller.set_present_values(present_voltage=0.0)
    _topic, payload = _last_publish(controller)
    assert payload == {"present_voltage": 0.0}


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


def test_diode_fail_publishes_inject_fault_diode(world_controller):
    controller = world_controller
    controller.diode_fail()
    topic, payload = _last_publish(controller)
    assert topic.endswith("/m2e/inject_fault")
    assert payload == {"type": "DiodeFail"}


@pytest.mark.parametrize(
    "fault",
    ["DiodeFail", "RcdError", "CpErrorE"],
)
def test_inject_fault_supports_each_fault_type(world_controller, fault):
    controller = world_controller
    controller.inject_fault(fault)
    topic, payload = _last_publish(controller)
    assert topic.endswith("/m2e/inject_fault")
    assert payload == {"type": fault}


def test_clear_fault_publishes_clear_fault(world_controller):
    controller = world_controller
    controller.clear_fault()
    topic, payload = _last_publish(controller)
    assert topic.endswith("/m2e/clear_fault")
    assert payload is None


def test_run_scenario_publishes_name(world_controller):
    controller = world_controller
    controller.run_scenario("AcIecBasic")
    topic, payload = _last_publish(controller)
    assert topic == "ext/everest_api/1/ev_driver_sim/world/m2e/run_scenario"
    assert payload == {"name": "AcIecBasic"}


def test_run_scenario_without_timing_omits_timing_key(world_controller):
    controller = world_controller
    """When no timing is given, the wire payload must not carry a `timing` key."""
    controller.run_scenario("AcIecBasic")
    topic, payload = _last_publish(controller)
    assert topic == "ext/everest_api/1/ev_driver_sim/world/m2e/run_scenario"
    assert payload == {"name": "AcIecBasic"}
    assert "timing" not in payload


def test_run_scenario_with_timing_includes_timing_dict(world_controller):
    controller = world_controller
    """When timing is provided, the wire payload embeds it verbatim."""
    timing = {
        "pause_at_ms": 2000,
        "resume_at_ms": 4000,
        "stop_after_ms": 6000,
        "unplug_after_ms": 7000,
    }
    controller.run_scenario("AcIecPauseResume", timing=timing)
    topic, payload = _last_publish(controller)
    assert topic == "ext/everest_api/1/ev_driver_sim/world/m2e/run_scenario"
    assert payload == {"name": "AcIecPauseResume", "timing": timing}


def test_run_scenario_with_partial_timing_only_sends_provided_fields(world_controller):
    controller = world_controller
    """Only the fields present in the `timing` dict are sent on the wire."""
    timing = {"stop_after_ms": 5000}
    controller.run_scenario("AcIecBasic", timing=timing)
    _topic, payload = _last_publish(controller)
    assert payload["timing"] == {"stop_after_ms": 5000}
    # Other timing fields must not be synthesized.
    assert "pause_at_ms" not in payload["timing"]
    assert "resume_at_ms" not in payload["timing"]


def test_run_scenario_with_empty_timing_omits_timing_key(world_controller):
    controller = world_controller
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
# query_state: drives the real m2e/query_state command
# ---------------------------------------------------------------------------


def _state_callback(controller):
    """Resolve the e2m/state callback paho would invoke, and its topic."""
    fake = controller._fake_client  # type: ignore[attr-defined]
    callbacks = {
        call.args[0]: call.args[1]
        for call in fake.message_callback_add.call_args_list
    }
    state_topic = controller.base_e2m + "/state"
    assert state_topic in callbacks
    return callbacks[state_topic], state_topic


def test_query_state_publishes_the_command_and_returns_the_answer(controller):
    # Stand in for the simulator: on seeing m2e/query_state, answer on
    # e2m/state, which is what the C++ handle_query_state does.
    on_state, state_topic = _state_callback(controller)
    fake = controller._fake_client  # type: ignore[attr-defined]

    def answer(topic, _payload):
        if topic.endswith("/m2e/query_state"):
            on_state(None, None, _make_msg(state_topic, "Faulted"))

    fake.publish.side_effect = answer

    # The answer must come back, and the command must actually go out.
    assert controller.query_state(timeout=5.0) == "Faulted"
    topics = [topic for topic, _payload in _all_publishes(controller)]
    assert any(topic.endswith("/m2e/query_state") for topic in topics)


def test_query_state_returns_none_when_the_simulator_never_answers(controller):
    # No side effect on publish, so nothing lands on e2m/state. A cache read
    # would have returned a stale value here; the real command reports the
    # silence instead.
    on_state, state_topic = _state_callback(controller)
    on_state(None, None, _make_msg(state_topic, "Charging"))

    assert controller.query_state(timeout=0.2) is None


def test_last_observed_state_reads_the_cache_without_publishing(controller):
    on_state, state_topic = _state_callback(controller)
    fake = controller._fake_client  # type: ignore[attr-defined]

    on_state(None, None, _make_msg(state_topic, "Charging"))
    before = fake.publish.call_count
    assert controller.last_observed_state() == "Charging"

    on_state(None, None, _make_msg(state_topic, "Idle"))
    assert controller.last_observed_state() == "Idle"
    assert fake.publish.call_count == before


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
    from everest.testing.core_utils.sim_registry import EvControllerDriver
    client = MagicMock()
    collector = StateCollector(EvControllerDriver(client, "ext/", "ev_manager"))
    callbacks = {c.args[0]: c.args[1] for c in client.message_callback_add.call_args_list}
    return collector, client, callbacks


def test_state_collector_demuxes_state_messages():
    collector, _client, callbacks = _build_collector()
    state_topic = "ext/everest_api/1/ev_controller/ev_manager/e2m/state"
    callbacks[state_topic](None, None, _make_msg(state_topic, "PluggedIn"))
    callbacks[state_topic](None, None, _make_msg(state_topic, "Charging"))
    assert collector.states == ["PluggedIn", "Charging"]


def test_state_collector_ignores_malformed_state_payload():
    collector, _client, callbacks = _build_collector()
    state_topic = "ext/everest_api/1/ev_controller/ev_manager/e2m/state"
    bad = types.SimpleNamespace()
    bad.topic = state_topic
    bad.payload = b"\xff\xfe not json"
    callbacks[state_topic](None, None, bad)
    # State payload must decode to a string; numeric/object inputs are
    # also dropped to avoid corrupting `last_state` semantics.
    callbacks[state_topic](None, None, _make_msg(state_topic, {"not": "a string"}))
    assert collector.states == []


def test_state_collector_wait_for_state_returns_true_when_already_matching():
    collector, _client, callbacks = _build_collector()
    state_topic = "ext/everest_api/1/ev_controller/ev_manager/e2m/state"
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
    state_topic = "ext/everest_api/1/ev_controller/ev_manager/e2m/state"

    def feed_after_block():
        time.sleep(0.05)  # let wait_for_state block first
        callbacks[state_topic](None, None, _make_msg(state_topic, "PluggedIn"))
        callbacks[state_topic](None, None, _make_msg(state_topic, "Charging"))
        callbacks[state_topic](None, None, _make_msg(state_topic, "Idle"))

    t = threading.Thread(target=feed_after_block)
    t.start()
    try:
        assert collector.wait_for_state("Charging", timeout=2.0) is True
        assert collector.states[-1] == "Idle"
    finally:
        t.join()


def test_wait_for_soc_progress_detects_increase():
    collector, _client, callbacks = _build_collector()
    ev_info_topic = "ext/everest_api/1/ev_controller/ev_manager/e2m/ev_info"

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
    ev_info_topic = "ext/everest_api/1/ev_controller/ev_manager/e2m/ev_info"
    callbacks[ev_info_topic](
        None, None, _make_msg(ev_info_topic, {"soc_pct": 42.0})
    )
    callbacks[ev_info_topic](
        None, None, _make_msg(ev_info_topic, {"soc_pct": 42.0})
    )
    assert collector.wait_for_soc_progress(min_increase=0.1, timeout=0.05) is False


@pytest.fixture
def world_controller(monkeypatch, mock_everest_core):
    import everest.testing.core_utils.sim_registry as mod

    mock_everest_core.everest_config["active_modules"].update({
        "wrong_world": {
            "module": "EvDriverSim", "config_module": {"ev_controller_module_id": "other"},
        },
        "world": {
            "module": "EvDriverSim", "config_module": {"ev_controller_module_id": "ev_manager"},
        },
    })
    fake = MagicMock()
    monkeypatch.setattr(mod.mqtt, "Client", MagicMock(return_value=fake))
    mock_everest_core.everest_config_path.write_text(yaml.safe_dump(mock_everest_core.everest_config))
    fake.connect.side_effect = lambda *_a: fake.on_connect(fake, None, {}, 0)
    ctrl = EvControllerTestController(mock_everest_core)
    ctrl._fake_client = fake
    callbacks = {c.args[0]: c.args[1] for c in fake.message_callback_add.call_args_list}

    def accept_configuration(topic, _payload):
        if topic.endswith("/m2e/configure_session"):
            ack_topic = f"{ctrl.base_e2m}/command_ack"
            callbacks[ack_topic](None, None, _make_msg(
                ack_topic, {"command": "configure_session", "status": "Accepted"}))
        elif topic.endswith("/m2e/run_scenario"):
            base = "ext/everest_api/1/ev_driver_sim/world/e2m"
            body = json.loads(_payload)
            callbacks[f"{base}/scenario_event"](None, None, _make_msg(
                f"{base}/scenario_event", {"name": body["name"], "kind": "Started"}))
            callbacks[f"{base}/command_ack"](None, None, _make_msg(
                f"{base}/command_ack", {"command": "run_scenario", "status": "Accepted"}))

    fake.publish.side_effect = accept_configuration
    return ctrl


@pytest.mark.parametrize("helper,mode", [
    ("plug_in", "AcIec"), ("plug_in_ac_iso", "AcIso2"),
    ("plug_in_dc_iso", "DcIso2"), ("plug_in_dc_d20", "DcIsoD20"),
    ("plug", None),
])
def test_plug_uses_world_driver_and_keeps_intent_on_car(world_controller, helper, mode):
    ctrl = world_controller
    callbacks = {c.args[0]: c.args[1] for c in ctrl._fake_client.message_callback_add.call_args_list}
    topic = f"{ctrl.base_e2m}/state"
    callbacks[topic](None, None, _make_msg(topic, "Charging"))
    getattr(ctrl, helper)()
    publishes = _all_publishes(ctrl)
    if mode:
        assert publishes[0][0] == f"{ctrl.base_m2e}/configure_session"
        assert publishes[0][1]["mode"] == mode
    assert publishes[-1] == ("ext/everest_api/1/ev_driver_sim/world/m2e/plug", None)
    assert len(publishes) == (2 if mode else 1)


@pytest.mark.parametrize("helper", ["plug_out", "plug_out_iso"])
def test_unplug_uses_world_driver(world_controller, helper):
    getattr(world_controller, helper)()
    expected = [("ext/everest_api/1/ev_driver_sim/world/m2e/unplug", None)]
    if helper == "plug_out_iso":
        expected.insert(0, (f"{world_controller.base_m2e}/stop_session", {}))
    assert _all_publishes(world_controller) == expected


@pytest.mark.parametrize("fault", [
    "DiodeFail", "RcdError", "CpErrorE",
])
def test_fault_routes_to_world(world_controller, fault):
    world_controller.inject_fault(fault)
    assert _all_publishes(world_controller) == [
        ("ext/everest_api/1/ev_driver_sim/world/m2e/inject_fault", {"type": fault})
    ]


def test_diode_fail_helper_uses_world_driver(world_controller):
    world_controller.diode_fail()
    assert _all_publishes(world_controller) == [
        ("ext/everest_api/1/ev_driver_sim/world/m2e/inject_fault", {"type": "DiodeFail"})
    ]


@pytest.mark.parametrize("fault", [None, "DiodeFail", "RcdError", "CpErrorE"])
def test_clear_fault_routes_to_world(world_controller, fault):
    ctrl = world_controller
    if fault is not None:
        ctrl.inject_fault(fault)
    ctrl._fake_client.publish.reset_mock()
    ctrl.clear_fault()
    assert _all_publishes(ctrl) == [
        ("ext/everest_api/1/ev_driver_sim/world/m2e/clear_fault", None),
    ]


def test_clear_without_tracked_fault_tolerates_rejected_ack(world_controller):
    ctrl = world_controller
    callbacks = {c.args[0]: c.args[1] for c in ctrl._fake_client.message_callback_add.call_args_list}

    def reject(topic, _payload):
        ack_topic = topic.replace("/m2e/clear_fault", "/e2m/command_ack")
        callbacks[ack_topic](None, None, _make_msg(
            ack_topic, {"command": "clear_fault", "status": "Rejected", "reason": "no active fault"}))

    ctrl._fake_client.publish.side_effect = reject
    ctrl.clear_fault()
    assert len(_all_publishes(ctrl)) == 1
    assert ctrl.ev_driver.wait_for_command_ack("clear_fault", "Rejected", timeout=0.1)["reason"] == "no active fault"


def test_world_driver_runs_scenario(world_controller):
    world_controller.ev_driver.run_scenario = MagicMock()
    world_controller.run_scenario("AcIecBasic", timing={"stop_after_ms": 40})
    world_controller.ev_driver.run_scenario.assert_called_once_with(
        "AcIecBasic", timing={"stop_after_ms": 40})
    assert _all_publishes(world_controller) == []


def test_scenario_requires_a_world_driver(controller):
    with pytest.raises(AssertionError, match="requires EvDriverSim"):
        controller.run_scenario("AcIecBasic")
    assert _all_publishes(controller) == []


@pytest.mark.parametrize("method,args", [
    ("plug", ()), ("plug_in", ()), ("plug_out", ()), ("plug_out_iso", ()),
    ("inject_fault", ("DiodeFail",)), ("clear_fault", ()),
])
def test_world_commands_without_driver_fail_before_publishing(controller, method, args):
    controller.state_collector.wait_for_state = MagicMock()
    controller.state_collector.wait_for_state_not = MagicMock()
    controller._fake_client.publish.reset_mock()
    with pytest.raises(NoSimulatorError, match="EvDriverSim"):
        getattr(controller, method)(*args)
    controller._fake_client.publish.assert_not_called()


def test_state_wait_ignores_old_charging_and_state_not_checks_current():
    collector, _client, callbacks = _build_collector()
    topic = next(t for t in callbacks if t.endswith("/state"))
    send = lambda value: callbacks[topic](None, None, _make_msg(topic, value))
    assert collector.wait_for_state_not("Charging", timeout=0) is True
    send("Charging")
    assert collector.wait_for_state("Charging", timeout=0) is True
    assert collector.wait_for_state_not("Charging", timeout=0.02) is False
    send("Unplugged")
    assert collector.wait_for_state("Charging", timeout=0.02) is False
    assert collector.wait_for_state_not("Charging", timeout=0) is True


def test_soc_progress_ignores_pre_call_history():
    collector, _client, callbacks = _build_collector()
    topic = next(t for t in callbacks if t.endswith("/ev_info"))
    for value in (10, 20):
        callbacks[topic](None, None, _make_msg(topic, {"soc_pct": value}))
    assert collector.wait_for_soc_progress(timeout=0.02) is False


def test_state_collector_lists_faults_and_ev_infos():
    collector, _client, callbacks = _build_collector()
    fault_topic = next(t for t in callbacks if t.endswith("/fault"))
    info_topic = next(t for t in callbacks if t.endswith("/ev_info"))
    callbacks[fault_topic](None, None, _make_msg(fault_topic, {"type": "DiodeFail"}))
    callbacks[info_topic](None, None, _make_msg(info_topic, {"soc_pct": 42.0}))
    assert collector.faults == [{"type": "DiodeFail"}]
    assert collector.ev_infos == [{"soc_pct": 42.0}]


def test_message_observers_preserve_state_history(controller):
    seen = []
    controller.add_message_observer("state", lambda _c, _u, msg: seen.append(json.loads(msg.payload)))
    callback, topic = _state_callback(controller)
    callback(None, None, _make_msg(topic, "Charging"))
    assert seen == controller.state_collector.states == ["Charging"]


def test_direct_facade_uses_first_registry_car_without_connector_one(monkeypatch, mock_everest_core):
    import everest.testing.core_utils.sim_registry as mod

    mock_everest_core.everest_config["active_modules"]["ev_manager"]["config_module"] = {"connector_id": 7}
    mock_everest_core.everest_config_path.write_text(yaml.safe_dump(mock_everest_core.everest_config))
    client = MagicMock()
    client.connect.side_effect = lambda *_: client.on_connect(client, None, {}, 0)
    factory = MagicMock(return_value=client)
    monkeypatch.setattr(mod.mqtt, "Client", factory)
    ctrl = EvControllerTestController(mock_everest_core)
    try:
        ctrl.start(timeout=0)
        factory.assert_called_once()
        assert client.publish.call_args.args == (
            "ext/everest_api/1/ev_controller/ev_manager/m2e/enable", "true")
        mock_everest_core.start.assert_not_called()
    finally:
        ctrl.stop()
    mock_everest_core.stop.assert_not_called()


def test_command_ack_observers_preserve_configuration_wait(world_controller):
    ctrl = world_controller
    first, second = [], []
    ctrl.add_command_ack_observer(first.append)
    ctrl.add_command_ack_observer(second.append)
    ctrl.play_charging_curve([{"t_offset_ms": 0, "current_a": 6.0, "three_phases": True}])
    ctrl.plug()
    expected = [{"command": "configure_session", "status": "Accepted"}]
    assert first == second == expected
    assert first[0] is not second[0]
    assert _all_publishes(ctrl)[-1][0].endswith("/m2e/plug")


def test_command_ack_observer_failure_does_not_block_other_observers(world_controller, caplog):
    ctrl = world_controller
    seen = []

    def failing_observer(ack):
        ack["status"] = "Changed"
        raise ValueError("observer failed")

    ctrl.add_command_ack_observer(failing_observer)
    ctrl.add_command_ack_observer(seen.append)
    ctrl.play_charging_curve([{"t_offset_ms": 0, "current_a": 6.0, "three_phases": True}])
    assert seen == [{"command": "configure_session", "status": "Accepted"}]
    assert "observer failed" in caplog.text
