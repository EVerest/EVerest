# SPDX-License-Identifier: Apache-2.0
# Copyright Pionix GmbH and Contributors to EVerest
"""Typed-API MQTT test controller for the EvSimulator module.

This controller publishes commands on the EvSimulator's `m2e/*` topics
and consumes `e2m/*` state/fault/command_ack events back from the
module. It targets the topic layout:

    {prefix}everest_api/1/ev_simulator/{module_id}/{m2e|e2m}/{suffix}

where `{prefix}` is the per-run external MQTT prefix exposed by
EverestCore and `{module_id}` is the active EvSimulator module id from
the deployed config (defaults to "ev_manager" when nothing matches).
"""

import json
import logging
import os
import threading
import time
from typing import Any, Dict, List, Optional

import paho.mqtt.client as mqtt
from paho.mqtt import __version__ as paho_mqtt_version

from everest.testing.core_utils.everest_core import EverestCore


DEFAULT_BPT: Dict[str, float] = {
    "discharge_max_current_limit": 50.0,
    "discharge_max_power_limit": 11000.0,
    "discharge_target_current": 30.0,
    "discharge_minimal_soc": 20.0,
}

_API_PATH = "everest_api/1/ev_simulator"
_DEFAULT_MODULE_ID = "ev_manager"


class StateCollector:
    """Capture e2m/state, e2m/fault, and e2m/command_ack payloads.

    Maintains a rolling list of every state value seen, plus the most
    recent state for fast polling, alongside fault and command-ack
    histories. Callers can block on `wait_for_state` to synchronize
    against FSM transitions.
    """

    def __init__(self, mqtt_client: mqtt.Client, base_e2m: str) -> None:
        self._base_e2m = base_e2m
        self._state_topic = f"{base_e2m}/state"
        self._fault_topic = f"{base_e2m}/fault"
        self._command_ack_topic = f"{base_e2m}/command_ack"
        self._ev_info_topic = f"{base_e2m}/ev_info"
        self._bsp_event_topic = f"{base_e2m}/bsp_event"
        self.states: List[str] = []
        self.faults: List[Dict[str, Any]] = []
        self.command_acks: List[Dict[str, Any]] = []
        self.ev_infos: List[Dict[str, Any]] = []
        self.bsp_events: List[Dict[str, Any]] = []
        self.last_state: Optional[str] = None
        self._cv = threading.Condition()

        mqtt_client.message_callback_add(self._state_topic, self._on_state)
        mqtt_client.message_callback_add(self._fault_topic, self._on_fault)
        mqtt_client.message_callback_add(
            self._command_ack_topic, self._on_command_ack
        )
        mqtt_client.message_callback_add(self._ev_info_topic, self._on_ev_info)
        mqtt_client.message_callback_add(
            self._bsp_event_topic, self._on_bsp_event
        )
        mqtt_client.subscribe(f"{base_e2m}/+")

    def _on_state(self, _client, _userdata, msg) -> None:
        try:
            value = json.loads(msg.payload.decode())
        except (json.JSONDecodeError, UnicodeDecodeError):
            return
        if not isinstance(value, str):
            return
        with self._cv:
            self.states.append(value)
            self.last_state = value
            self._cv.notify_all()

    def _on_fault(self, _client, _userdata, msg) -> None:
        try:
            value = json.loads(msg.payload.decode())
        except (json.JSONDecodeError, UnicodeDecodeError):
            return
        if not isinstance(value, dict):
            return
        with self._cv:
            self.faults.append(value)
            self._cv.notify_all()

    def _on_command_ack(self, _client, _userdata, msg) -> None:
        try:
            value = json.loads(msg.payload.decode())
        except (json.JSONDecodeError, UnicodeDecodeError):
            return
        if not isinstance(value, dict):
            return
        with self._cv:
            self.command_acks.append(value)
            self._cv.notify_all()

    def _on_ev_info(self, _client, _userdata, msg) -> None:
        try:
            value = json.loads(msg.payload.decode())
        except (json.JSONDecodeError, UnicodeDecodeError):
            return
        if not isinstance(value, dict):
            return
        with self._cv:
            self.ev_infos.append(value)
            self._cv.notify_all()

    def _on_bsp_event(self, _client, _userdata, msg) -> None:
        try:
            value = json.loads(msg.payload.decode())
        except (json.JSONDecodeError, UnicodeDecodeError):
            return
        if not isinstance(value, dict):
            return
        with self._cv:
            self.bsp_events.append(value)
            self._cv.notify_all()

    def wait_for_fault(
        self, fault_type: str, timeout: float = 30.0
    ) -> Optional[Dict[str, Any]]:
        """Block until an `e2m/fault` of `fault_type` is seen, or timeout.

        Returns the matching FaultReport dict (keys: ``type``,
        optional ``message``/``rcd_mA``) on success, None on timeout.
        Asserting the fault TYPE — not merely that state left Charging —
        prevents a spurious unrelated transition from passing a
        fault-injection test falsely.
        """
        deadline = time.time() + timeout
        with self._cv:
            scanned = 0
            while True:
                while scanned < len(self.faults):
                    f = self.faults[scanned]
                    if f.get("type") == fault_type:
                        return f
                    scanned += 1
                remaining = deadline - time.time()
                if remaining <= 0:
                    return None
                self._cv.wait(timeout=remaining)

    def wait_for_soc_progress(
        self, min_increase: float = 0.01, timeout: float = 30.0
    ) -> bool:
        """Block until SoC advances by at least `min_increase` percent.

        Anchored on the first `e2m/ev_info` observed from call entry;
        returns True once a later ev_info reports
        ``soc_pct >= baseline + min_increase``. This is a positive
        observable that the session is actually charging (not merely
        that the FSM reached `Charging` at 0 A). False on timeout or if
        no ev_info ever arrives.
        """
        deadline = time.time() + timeout
        with self._cv:
            scanned = len(self.ev_infos)
            baseline: Optional[float] = None
            while True:
                while scanned < len(self.ev_infos):
                    soc = self.ev_infos[scanned].get("soc_pct")
                    if isinstance(soc, (int, float)):
                        if baseline is None:
                            baseline = float(soc)
                        elif float(soc) >= baseline + min_increase:
                            return True
                    scanned += 1
                remaining = deadline - time.time()
                if remaining <= 0:
                    return False
                self._cv.wait(timeout=remaining)

    def wait_for_state(self, target: str, timeout: float = 30.0) -> bool:
        """Block until `target` is observed on `e2m/state`, or `timeout`.

        Matches against the full state history rather than only
        `last_state`: the FSM can pass through `target` transiently
        between two e2m publishes, and a `last_state`-only check would
        miss it and spuriously time out. A high-water mark anchored at
        call entry ensures only states observed from this call onward
        are considered (a stale earlier occurrence from a prior session
        does not produce a false positive). Returns True on a match,
        False on timeout.
        """
        deadline = time.time() + timeout
        with self._cv:
            # Already in the target state when the call is made.
            if self.last_state == target:
                return True
            scanned = len(self.states)
            while True:
                while scanned < len(self.states):
                    if self.states[scanned] == target:
                        return True
                    scanned += 1
                remaining = deadline - time.time()
                if remaining <= 0:
                    # Final scan in case states were appended right at
                    # the deadline boundary.
                    return target in self.states[scanned:] or self.last_state == target
                self._cv.wait(timeout=remaining)

    def wait_for_state_not(self, target: str, timeout: float = 30.0) -> bool:
        """Block until `last_state != target` or until `timeout` elapses.

        Returns True once the state differs from `target`, False on timeout.
        """
        deadline = time.time() + timeout
        with self._cv:
            while self.last_state == target:
                remaining = deadline - time.time()
                if remaining <= 0:
                    return self.last_state != target
                self._cv.wait(timeout=remaining)
            return True

    def wait_for_command_ack(
        self,
        command: str,
        status: str,
        timeout: float = 10.0,
    ) -> Optional[Dict[str, Any]]:
        """Block until a ``command_ack`` matching ``command`` and ``status`` arrives.

        Scans ``command_acks`` from a high-water mark set at call entry
        so earlier acks from a prior command are not matched. Returns the
        matching ack dict on success, None on timeout.
        """
        deadline = time.time() + timeout
        with self._cv:
            scanned = len(self.command_acks)
            while True:
                while scanned < len(self.command_acks):
                    ack = self.command_acks[scanned]
                    if ack.get("command") == command and ack.get("status") == status:
                        return ack
                    scanned += 1
                remaining = deadline - time.time()
                if remaining <= 0:
                    return None
                self._cv.wait(timeout=remaining)


class EvSimulatorTestController:
    """Drive EvSimulator over its typed `m2e/*` MQTT API.

    Each instance resolves the EvSimulator module id from the deployed
    Everest config, opens a paho MQTT client, and wires a
    `StateCollector` against the matching `e2m/+` topics. Commands map
    to single-shot publishes; a small number of helpers also wait for
    `e2m/state` transitions before returning.
    """

    def __init__(self, everest_core: EverestCore) -> None:
        self._everest_core = everest_core
        self._prefix = everest_core.mqtt_external_prefix
        self._module_id = self._resolve_module_id(everest_core)
        self.base_m2e = f"{self._prefix}{_API_PATH}/{self._module_id}/m2e"
        self.base_e2m = f"{self._prefix}{_API_PATH}/{self._module_id}/e2m"

        mqtt_server_uri = os.environ.get("MQTT_SERVER_ADDRESS", "127.0.0.1")
        mqtt_server_port = int(os.environ.get("MQTT_SERVER_PORT", "1883"))
        if paho_mqtt_version < "2.0":
            self._mqtt_client = mqtt.Client(everest_core.everest_uuid)
        else:
            self._mqtt_client = mqtt.Client(
                callback_api_version=mqtt.CallbackAPIVersion.VERSION2,
                client_id=everest_core.everest_uuid,
            )
        self._mqtt_client.connect(mqtt_server_uri, mqtt_server_port)
        self._mqtt_client.loop_start()

        self.state_collector = StateCollector(self._mqtt_client, self.base_e2m)

    @staticmethod
    def _resolve_module_id(everest_core: EverestCore) -> str:
        """Find the first active module whose `module` field is EvSimulator."""
        try:
            config = everest_core.everest_config
        except Exception:  # noqa: BLE001 - config parse is best-effort
            logging.warning(
                "EvSimulator controller: failed to read everest_config; "
                "falling back to default module id"
            )
            return _DEFAULT_MODULE_ID
        active_modules = (config or {}).get("active_modules", {}) or {}
        for module_id, module_cfg in active_modules.items():
            if isinstance(module_cfg, dict) and module_cfg.get("module") == "EvSimulator":
                return module_id
        return _DEFAULT_MODULE_ID

    def _publish(self, suffix: str, payload: Any) -> None:
        topic = f"{self.base_m2e}/{suffix}"
        self._mqtt_client.publish(topic, json.dumps(payload))

    def start(self, connector_id: int = 1) -> None:
        """Enable the simulator (publishes `m2e/enable` for `connector_id`)."""
        del connector_id  # topic is per-module; connector_id reserved for API symmetry
        # m2e/enable carries a bare JSON bool, not an object: CommandRouter
        # deserializes the payload directly into `bool` (a `{"enable": ...}`
        # object fails adl_deserialize, the throw is swallowed, and the FSM
        # never leaves Disabled). Matches evsimulator_smoke_test's json.dumps(True).
        #
        # MQTT has no retained delivery here, so an `enable` published before
        # the module finishes subscribing to its m2e topics is silently lost
        # and the FSM stays Disabled forever. Re-publish (Enable is idempotent)
        # until the FSM reports it left Disabled, or give up after the bound.
        deadline = time.time() + 15.0
        while time.time() < deadline:
            self._publish("enable", True)
            if self.state_collector.wait_for_state("Unplugged", timeout=1.0):
                return
        # Final attempt recorded; callers' own wait_for_state will surface a
        # clear failure if the module never came up.
        self._publish("enable", True)

    def stop(self) -> None:
        """Tear down the MQTT client and stop the background network loop."""
        if self._mqtt_client is not None:
            try:
                # disconnect() before loop_stop() (paho-recommended): the
                # network loop must still be running to flush the DISCONNECT
                # packet, otherwise the broker sees an ungraceful drop.
                self._mqtt_client.disconnect()
            finally:
                self._mqtt_client.loop_stop()
            self._mqtt_client = None

    def plug_in(self, connector_id: int = 1) -> None:
        """Configure a default AC IEC session, plug in, wait for Charging."""
        del connector_id
        self._publish(
            "configure_session",
            {
                "mode": "AcIec",
                "params": {
                    "charging_current_a": 32.0,
                    "three_phases": True,
                },
            },
        )
        self._publish("plug", {})
        self.state_collector.wait_for_state("Charging", timeout=30.0)

    def plug_in_ac_iso(
        self,
        connector_id: int = 1,
        payment_type: Optional[str] = None,
    ) -> None:
        """Configure an AC ISO 15118-2 session, then plug in."""
        del connector_id
        params: Dict[str, Any] = {
            "charging_current_a": 16.0,
            "three_phases": True,
        }
        if payment_type is not None:
            params["payment"] = payment_type
        self._publish(
            "configure_session",
            {"mode": "AcIso2", "params": params},
        )
        self._publish("plug", {})

    def plug_in_dc_iso(
        self,
        connector_id: int = 1,
        payment_type: Optional[str] = None,
    ) -> None:
        """Configure a DC ISO 15118-2 session, then plug in."""
        del connector_id
        params: Dict[str, Any] = {}
        if payment_type is not None:
            params["payment"] = payment_type
        self._publish(
            "configure_session",
            {"mode": "DcIso2", "params": params},
        )
        self._publish("plug", {})

    def plug_in_dc_d20(
        self,
        connector_id: int = 1,
        payment_type: Optional[str] = None,
    ) -> None:
        """Configure a DC ISO 15118-20 session, then plug in."""
        del connector_id
        params: Dict[str, Any] = {}
        if payment_type is not None:
            params["payment"] = payment_type
        self._publish(
            "configure_session",
            {"mode": "DcIsoD20", "params": params},
        )
        self._publish("plug", {})

    def plug_in_dc_bpt(
        self,
        connector_id: int = 1,
        bpt_params: Optional[Dict[str, float]] = None,
    ) -> None:
        """Configure a DC ISO 15118-20 BPT session, then plug in."""
        del connector_id
        bpt = dict(DEFAULT_BPT) if bpt_params is None else dict(bpt_params)
        self._publish(
            "configure_session",
            {"mode": "DcIsoD20", "params": {"bpt": bpt}},
        )
        self._publish("plug", {})

    def plug_in_dc_mcs(
        self,
        connector_id: int = 1,
        mcs_profile: Optional[Dict[str, Any]] = None,
    ) -> None:
        """Configure a DC ISO 15118-20 MCS session, then plug in.

        MCS is now a boolean enable flag on the wire (`mcs_enabled`);
        `mcs_profile` is accepted only for backward call-site
        compatibility and is otherwise ignored.
        """
        del connector_id
        del mcs_profile
        self._publish(
            "configure_session",
            {"mode": "DcIsoD20", "params": {"mcs_enabled": True}},
        )
        self._publish("plug", {})

    def plug(self, connector_id: int = 1) -> None:
        """Plug in using the session config already latched via
        `configure_session` / `play_charging_curve`.

        The `plug_in_dc_*` helpers configure-then-plug in one call, which
        overwrites any previously latched curve. Use this when the session
        (e.g. a looping `ChargingCurve`) was latched separately and must be
        consumed by this plug rather than replaced.
        """
        del connector_id
        self._publish("plug", {})

    def plug_out(self, connector_id: int = 1) -> None:
        """Unplug without negotiating an ISO 15118 stop first."""
        del connector_id
        self._publish("unplug", {})

    def plug_out_iso(self, connector_id: int = 1) -> None:
        """Gracefully end an ISO 15118 session, wait for FSM to leave Charging, then unplug."""
        del connector_id
        self._publish("stop_session", {})
        self.state_collector.wait_for_state_not("Charging", timeout=30.0)
        self._publish("unplug", {})

    def pause_session(self, connector_id: int = 1) -> None:
        """Pause the active charging session."""
        del connector_id
        self._publish("pause_session", {})

    def resume_session(self, connector_id: int = 1) -> None:
        """Resume a paused charging session."""
        del connector_id
        self._publish("resume_session", {})

    def diode_fail(self, connector_id: int = 1) -> None:
        """Inject a CP-diode short fault."""
        del connector_id
        self._publish("inject_fault", {"type": "DiodeFail"})

    def inject_fault(self, fault_type: str, connector_id: int = 1) -> None:
        """Inject a fault by name; covers all `FaultType` variants."""
        del connector_id
        self._publish("inject_fault", {"type": fault_type})

    def clear_fault(self, connector_id: int = 1) -> None:
        """Clear the currently injected fault."""
        del connector_id
        self._publish("clear_fault", {})

    def run_scenario(
        self,
        name: str,
        connector_id: int = 1,
        timing: Optional[Dict[str, int]] = None,
    ) -> None:
        """Run a named end-to-end scenario.

        `timing` is an optional dict of scenario timing override fields.
        Accepted keys (all in milliseconds, all optional):
          ``pause_at_ms``, ``resume_at_ms``, ``stop_after_ms``,
          ``unplug_after_ms``, ``fault_at_ms``, ``clear_fault_at_ms``.
        When ``timing`` is None or empty it is omitted from the wire
        payload and the simulator applies its built-in preset timings.
        """
        del connector_id
        payload: Dict[str, Any] = {"name": name}
        if timing:
            payload["timing"] = timing
        self._publish("run_scenario", payload)

    def query_state(self, connector_id: int = 1) -> Optional[str]:
        """Return the last FSM state observed on `e2m/state`.

        Synchronous cache read. The m2e/query_state publish is omitted
        because StateCollector keeps `last_state` live from e2m updates.
        """
        del connector_id
        return self.state_collector.last_state

    def set_soc(self, soc_pct: float, connector_id: int = 1) -> None:
        """Override the simulated EV state of charge (percent)."""
        del connector_id
        self._publish("set_soc", {"soc_pct": soc_pct})

    def set_charging_current(
        self,
        current_a: float,
        three_phases: bool,
        ramp_ms: Optional[int] = None,
        connector_id: int = 1,
    ) -> None:
        """Step the commanded EV charging current.

        When `ramp_ms` is set, the simulator interpolates from the
        previous setpoint to `current_a` over that duration; when None
        the key is omitted and the simulator applies the step instantly.
        """
        del connector_id
        payload: Dict[str, Any] = {
            "current_a": current_a,
            "three_phases": three_phases,
        }
        if ramp_ms is not None:
            payload["ramp_ms"] = ramp_ms
        self._publish("set_charging_current", payload)

    def ramp_to_current(
        self,
        target_a: float,
        three_phases: bool,
        duration_s: float,
        connector_id: int = 1,
    ) -> None:
        """Ramp the commanded current to `target_a` over `duration_s` seconds."""
        self.set_charging_current(
            target_a,
            three_phases,
            ramp_ms=int(duration_s * 1000),
            connector_id=connector_id,
        )

    def play_charging_curve(
        self,
        points: List[Dict[str, Any]],
        loop: bool = False,
        mode: str = "DcIso2",
        connector_id: int = 1,
    ) -> None:
        """Latch a `configure_session` with an attached charging curve.

        Each entry in `points` is a `CurvePoint` dict with the keys
        `t_offset_ms`, `current_a`, `three_phases`, and optionally
        `ramp_ms`. The published payload has the shape
        `{"mode": mode, "params": {"curve": {"points": points,
        "loop": loop}}}`.

        Publishes `m2e/configure_session`; the curve is latched and
        applied at the next plug (it does not alter a live session).
        """
        del connector_id
        self._publish(
            "configure_session",
            {
                "mode": mode,
                "params": {"curve": {"points": list(points), "loop": loop}},
            },
        )
