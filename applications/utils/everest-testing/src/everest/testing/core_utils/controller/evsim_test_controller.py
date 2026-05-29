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

DEFAULT_MCS: Dict[str, Any] = {}

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
        self.states: List[str] = []
        self.faults: List[Dict[str, Any]] = []
        self.command_acks: List[Dict[str, Any]] = []
        self.last_state: Optional[str] = None
        self._cv = threading.Condition()

        mqtt_client.message_callback_add(self._state_topic, self._on_state)
        mqtt_client.message_callback_add(self._fault_topic, self._on_fault)
        mqtt_client.message_callback_add(
            self._command_ack_topic, self._on_command_ack
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

    def wait_for_state(self, target: str, timeout: float = 30.0) -> bool:
        """Block until `last_state == target` or until `timeout` elapses.

        Returns True on a match, False on timeout.
        """
        deadline = time.time() + timeout
        with self._cv:
            while self.last_state != target:
                remaining = deadline - time.time()
                if remaining <= 0:
                    return self.last_state == target
                self._cv.wait(timeout=remaining)
            return True

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
        self._publish("enable", {"enable": True})

    def stop(self) -> None:
        """Tear down the MQTT client and stop the background network loop."""
        if self._mqtt_client is not None:
            try:
                self._mqtt_client.loop_stop()
            finally:
                self._mqtt_client.disconnect()
            self._mqtt_client = None

    def plug_in(self, connector_id: int = 1) -> None:
        """Plug in and start a default AC IEC session, then wait for Charging."""
        del connector_id
        self._publish("plug", {})
        self._publish(
            "start_session",
            {
                "mode": "AcIec",
                "charging_current_a": 32.0,
                "three_phases": True,
            },
        )
        self.state_collector.wait_for_state("Charging", timeout=30.0)

    def plug_in_ac_iso(
        self,
        connector_id: int = 1,
        payment_type: Optional[str] = None,
    ) -> None:
        """Plug in and start an AC ISO 15118-2 session."""
        del connector_id
        self._publish("plug", {})
        payload: Dict[str, Any] = {
            "mode": "AcIso2",
            "charging_current_a": 16.0,
            "three_phases": True,
        }
        if payment_type is not None:
            payload["payment"] = payment_type
        self._publish("start_session", payload)

    def plug_in_dc_iso(
        self,
        connector_id: int = 1,
        payment_type: Optional[str] = None,
    ) -> None:
        """Plug in and start a DC ISO 15118-2 session."""
        del connector_id
        self._publish("plug", {})
        payload: Dict[str, Any] = {"mode": "DcIso2"}
        if payment_type is not None:
            payload["payment"] = payment_type
        self._publish("start_session", payload)

    def plug_in_dc_d20(
        self,
        connector_id: int = 1,
        payment_type: Optional[str] = None,
    ) -> None:
        """Plug in and start a DC ISO 15118-20 session."""
        del connector_id
        self._publish("plug", {})
        payload: Dict[str, Any] = {"mode": "DcIsoD20"}
        if payment_type is not None:
            payload["payment"] = payment_type
        self._publish("start_session", payload)

    def plug_in_dc_bpt(
        self,
        connector_id: int = 1,
        bpt_params: Optional[Dict[str, float]] = None,
    ) -> None:
        """Plug in and start a DC ISO 15118-20 BPT session."""
        del connector_id
        params = dict(DEFAULT_BPT) if bpt_params is None else dict(bpt_params)
        self._publish("plug", {})
        self._publish(
            "start_session",
            {"mode": "DcIsoD20", "bpt": params},
        )

    def plug_in_dc_mcs(
        self,
        connector_id: int = 1,
        mcs_profile: Optional[Dict[str, Any]] = None,
    ) -> None:
        """Plug in and start a DC ISO 15118-20 MCS session."""
        del connector_id
        profile = dict(DEFAULT_MCS) if mcs_profile is None else dict(mcs_profile)
        self._publish("plug", {})
        self._publish(
            "start_session",
            {"mode": "DcIsoD20", "mcs": profile},
        )

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

    def run_scenario(self, name: str, connector_id: int = 1) -> None:
        """Run a named end-to-end scenario."""
        del connector_id
        self._publish("run_scenario", {"name": name})

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
        """Re-issue `start_session` with an attached charging curve.

        Each entry in `points` is a `CurvePoint` dict with the keys
        `t_offset_ms`, `current_a`, `three_phases`, and optionally
        `ramp_ms`. The published payload has the shape
        `{"mode": mode, "curve": {"points": points, "loop": loop}}`.

        This method re-issues `m2e/start_session` with the given curve;
        it is typically used by tests already inside a session that
        want to inject a profile mid-test.
        """
        del connector_id
        self._publish(
            "start_session",
            {"mode": mode, "curve": {"points": list(points), "loop": loop}},
        )
