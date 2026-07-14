# SPDX-License-Identifier: Apache-2.0
# Copyright Pionix GmbH and Contributors to EVerest
"""
EV and Yeti simulator MQTT drivers + DSL replay used by the test harness.

Replaces the legacy `everest_external/nodered/<id>/carsim/*` topic surface
with typed m2e topics:

    everest_api/1/ev_simulator/<module_id>/m2e/<command>
    everest_api/1/yeti_simulator/<module_id>/m2e/<command>

`EvSimDriver` / `YetiSimDriver` are thin per-module-instance publishers.
`SimRegistry` discovers both EvSimulator and YetiSimulator instances in a
resolved everest config and indexes them by `connector_id`.
"""

from __future__ import annotations

import json
import logging
import threading
import time
from dataclasses import dataclass
from pathlib import Path
from typing import Any, Callable, Dict, List, Optional

import yaml

log = logging.getLogger(__name__)


@dataclass
class _SubInfo:
    var: str
    payloads: List[Any]


class EvSimDriver:
    """Publish-side wrapper for a single EvSimulator module instance."""

    def __init__(self, mqtt_client, mqtt_external_prefix: str, module_id: str):
        self._client = mqtt_client
        self._module_id = module_id
        self._base = f"{mqtt_external_prefix}everest_api/1/ev_simulator/{module_id}"
        self._subs: Dict[str, _SubInfo] = {}
        self._lock = threading.Lock()

    @property
    def module_id(self) -> str:
        return self._module_id

    def _publish(self, var: str, payload: Any = None) -> None:
        topic = f"{self._base}/m2e/{var}"
        body = json.dumps(payload)
        log.debug("ev_sim publish %s %s", topic, body)
        self._client.publish(topic, body)

    # ---- subscribe helpers (used for predicates / waits) ------------------
    def subscribe_var(self, var: str) -> None:
        if var in self._subs:
            return
        topic = f"{self._base}/e2m/{var}"
        info = _SubInfo(var=var, payloads=[])
        self._subs[var] = info
        self._client.subscribe(topic)
        self._client.message_callback_add(topic, self._make_on_message(info))

    def _make_on_message(self, info: _SubInfo) -> Callable[..., None]:
        def _cb(_client, _userdata, msg):
            try:
                payload = json.loads(msg.payload.decode())
            except Exception:
                payload = msg.payload
            with self._lock:
                info.payloads.append(payload)

        return _cb

    def wait_for(self, var: str, predicate: Callable[[Any], bool], timeout: float = 10.0) -> Any:
        self.subscribe_var(var)
        info = self._subs[var]
        deadline = time.time() + timeout
        seen = 0
        while time.time() < deadline:
            with self._lock:
                items = info.payloads[seen:]
                seen += len(items)
            for it in items:
                if predicate(it):
                    return it
            time.sleep(0.05)
        raise AssertionError(
            f"ev_sim[{self._module_id}] timeout waiting for {var}; last seen: {info.payloads}"
        )

    # ---- m2e commands -----------------------------------------------------
    def enable(self, value: bool = True) -> None:
        self._publish("enable", value)

    def plug(self) -> None:
        self._publish("plug", None)

    def unplug(self) -> None:
        self._publish("unplug", None)

    def stop_session(self) -> None:
        self._publish("stop_session", None)

    def pause_session(self) -> None:
        self._publish("pause_session", None)

    def resume_session(self) -> None:
        self._publish("resume_session", None)

    def clear_fault(self) -> None:
        self._publish("clear_fault", None)

    def configure_session(self, mode: str, params: Optional[Dict[str, Any]] = None) -> None:
        # mode in {"AcIec", "AcIso2", "AcIsoD20", "DcIso2", "DcIsoD20"}
        body = {"mode": mode, "params": params or {}}
        self._publish("configure_session", body)

    def set_charging_current(self, current_a: float, three_phases: bool, ramp_ms: Optional[int] = None) -> None:
        body: Dict[str, Any] = {"current_a": current_a, "three_phases": three_phases}
        if ramp_ms is not None:
            body["ramp_ms"] = ramp_ms
        self._publish("set_charging_current", body)

    def inject_fault(self, fault_type: str, rcd_mA: Optional[float] = None) -> None:
        body: Dict[str, Any] = {"type": fault_type}
        if rcd_mA is not None:
            body["rcd_mA"] = rcd_mA
        self._publish("inject_fault", body)

    def raise_error(self, error: Dict[str, Any]) -> None:
        self._publish("raise_error", error)

    def clear_error(self, error: Dict[str, Any]) -> None:
        self._publish("clear_error", error)

    def bcb_toggle(self, count: Optional[int] = None) -> None:
        self._publish("bcb_toggle", {} if count is None else {"count": count})

    def run_scenario(self, name: str) -> None:
        self._publish("run_scenario", {"name": name})

    # ---- DSL replay -------------------------------------------------------
    def play_dsl(self, dsl: str) -> None:
        """
        Replay a (subset of the) EvManager auto_exec / nodered DSL.

        Recognised ops (others are logged and skipped):

          sleep N                       -> time.sleep(N)
          iec_wait_pwr_ready            -> wait for bsp_event PowerOn
          iso_wait_slac_matched         -> wait for slac_state Matched
          iso_wait_pwr_ready            -> wait for iso_session_event PowerReady
          iso_wait_v2g_session_stopped  -> wait for iso_session_event V2GFinished
          iso_wait_pwm_is_running       -> wait for bsp_event B (PWM resumed)
          draw_power_regulated A,P      -> set_charging_current(A, three=P==3)
          iso_draw_power_regulated A,P  -> same
          iso_start_v2g_session AC ... / DC ...  -> no-op (EvSim configures on plug)
          iso_start_bcb_toggle N        -> bcb_toggle(N)
          iso_stop_charging             -> stop_session()
          iso_dc_power_on               -> no-op (driven by IsoSessionEvent)
          iso_pause_charging            -> pause_session()
          iso_wait_for_resume           -> no-op (resume is external)
          iso_wait_for_stop N           -> sleep N (charger drives stop)
          unplug                        -> unplug()
          pause                         -> pause_session()
          diode_fail                    -> inject_fault("DiodeFail")
        """
        for raw_token in dsl.split(";"):
            tok = raw_token.strip()
            if not tok:
                continue
            self._run_one(tok)

    def _run_one(self, tok: str) -> None:
        head, *rest = tok.split(maxsplit=1)
        arg = rest[0].strip() if rest else ""
        try:
            if head == "sleep":
                time.sleep(float(arg))
            elif head == "iec_wait_pwr_ready":
                self.wait_for("bsp_event", lambda e: isinstance(e, dict) and e.get("event") == "PowerOn",
                              timeout=30)
            elif head == "iso_wait_slac_matched":
                self.wait_for("slac_state", lambda s: isinstance(s, dict) and s.get("state") == "Matched",
                              timeout=30)
            elif head == "iso_wait_pwr_ready":
                self.wait_for("iso_session_event",
                              lambda e: isinstance(e, dict) and e.get("kind") in ("PowerReady", "DcPowerOn"),
                              timeout=60)
            elif head == "iso_wait_v2g_session_stopped":
                self.wait_for("iso_session_event",
                              lambda e: isinstance(e, dict) and e.get("kind") == "V2GFinished",
                              timeout=60)
            elif head == "iso_wait_pwm_is_running":
                self.wait_for("bsp_event", lambda e: isinstance(e, dict) and e.get("event") == "B",
                              timeout=30)
            elif head in ("draw_power_regulated", "iso_draw_power_regulated"):
                cur_s, *phase_s = (arg or "16,3").split(",")
                current_a = float(cur_s)
                three = (int(phase_s[0]) == 3) if phase_s else True
                self.set_charging_current(current_a, three)
            elif head == "iso_start_v2g_session":
                # Legacy DSL specifies AC|DC + payment_type. EvSimulator's
                # configure_session must already have been issued before plug.
                # We do not synthesize a configure here because mode/payment
                # are not always reliably parsed from this position.
                log.debug("ev_sim_driver: iso_start_v2g_session is a no-op")
            elif head == "iso_start_bcb_toggle":
                try:
                    n = int(arg)
                except (TypeError, ValueError):
                    n = 3
                self.bcb_toggle(n)
            elif head == "iso_stop_charging":
                self.stop_session()
            elif head == "iso_dc_power_on":
                log.debug("ev_sim_driver: iso_dc_power_on is a no-op")
            elif head == "iso_pause_charging" or head == "pause":
                self.pause_session()
            elif head == "iso_wait_for_resume":
                log.debug("ev_sim_driver: iso_wait_for_resume is a no-op")
            elif head == "iso_wait_for_stop":
                try:
                    n = float(arg)
                except (TypeError, ValueError):
                    n = 60.0
                time.sleep(n)
            elif head == "unplug":
                self.unplug()
            elif head == "diode_fail":
                self.inject_fault("DiodeFail")
            else:
                log.warning("ev_sim_driver: unrecognized DSL op '%s'", tok)
        except AssertionError:
            log.warning("ev_sim_driver: DSL wait timed out at '%s' (continuing)", tok)
        except Exception:
            log.exception("ev_sim_driver: DSL step '%s' raised", tok)


class YetiSimDriver:
    """Publish-side wrapper for a single YetiSimulator module instance.

    Targets the versioned m2e API:
        <prefix>everest_api/1/yeti_simulator/<module_id>/m2e/{raise_error,clear_error}
    """

    def __init__(self, mqtt_client, mqtt_external_prefix: str, module_id: str):
        self._client = mqtt_client
        self._module_id = module_id
        self._base = f"{mqtt_external_prefix}everest_api/1/yeti_simulator/{module_id}"

    @property
    def module_id(self) -> str:
        return self._module_id

    def raise_error(self, payload: dict) -> None:
        topic = f"{self._base}/m2e/raise_error"
        self._client.publish(topic, json.dumps(payload))

    def clear_error(self, payload: dict) -> None:
        topic = f"{self._base}/m2e/clear_error"
        self._client.publish(topic, json.dumps(payload))


class SimRegistry:
    """
    Indexes EvSimulator and YetiSimulator instances from a resolved everest config.
    The `ev_by_connector` / `yeti_by_connector` mappings front the test_controller's
    connector_id API.
    """

    def __init__(
        self,
        mqtt_client,
        mqtt_external_prefix: str,
        config_path: Path,
    ):
        self._mqtt = mqtt_client
        self._prefix = mqtt_external_prefix
        self._by_module: Dict[str, EvSimDriver] = {}
        self._by_connector: Dict[int, EvSimDriver] = {}
        self._yeti_by_connector: Dict[int, YetiSimDriver] = {}
        self._scenarios_by_module: Dict[str, str] = {}
        self._load(config_path)

    def _load(self, config_path: Path) -> None:
        try:
            text = config_path.read_text()
            data = yaml.safe_load(text) or {}
        except Exception:
            log.exception("sim_registry: failed to read %s", config_path)
            return
        modules = (data.get("active_modules") or {})
        for mid, mblock in modules.items():
            if not isinstance(mblock, dict):
                continue
            module_name = mblock.get("module")
            cid = ((mblock.get("config_module") or {}).get("connector_id"))
            if module_name == "EvSimulator":
                drv = EvSimDriver(self._mqtt, self._prefix, mid)
                self._by_module[mid] = drv
                if isinstance(cid, int):
                    self._by_connector[cid] = drv
            elif module_name == "YetiSimulator":
                ydrv = YetiSimDriver(self._mqtt, self._prefix, mid)
                if isinstance(cid, int):
                    self._yeti_by_connector[cid] = ydrv
        # Load sidecar scenarios. The everest fixture rewrites the active
        # config to a temp path; the sidecar lives next to the original
        # source config (referenced via .stem). We try both: temp dir AND
        # the source location is harder to reach -- the source file path
        # is known to EverestCore but not surfaced; rely on a "name" match
        # by walking known config dirs.
        self._maybe_load_sidecar_from_temp(config_path)

    def _maybe_load_sidecar_from_temp(self, config_path: Path) -> None:
        # Source filename (preserved by EverestCore when writing temp config).
        name = config_path.name  # e.g. "everest-config-sil-ocpp.yaml"
        candidates = [
            Path("config") / name,
            Path("tests/ocpp_tests/test_sets/everest-aux/config") / name,
        ]
        for cand in candidates:
            sidecar = cand.with_suffix(".evsim-scenarios.yaml")
            if not sidecar.exists():
                continue
            try:
                self._scenarios_by_module.update(yaml.safe_load(sidecar.read_text()) or {})
            except Exception:
                log.exception("sim_registry: failed to load sidecar %s", sidecar)

    @property
    def drivers(self) -> Dict[str, EvSimDriver]:
        return dict(self._by_module)

    def by_module(self, module_id: str) -> Optional[EvSimDriver]:
        return self._by_module.get(module_id)

    def ev_by_connector(self, connector_id: int) -> Optional[EvSimDriver]:
        return self._by_connector.get(connector_id)

    def yeti_by_connector(self, connector_id: int) -> Optional[YetiSimDriver]:
        return self._yeti_by_connector.get(connector_id)

    def _wait_ready(self, drv: EvSimDriver, timeout: float = 15.0) -> bool:
        """Wait until the EvSimulator instance publishes its first heartbeat.

        The heartbeat is published from a thread started in `ready()`, after
        the m2e command subscriptions are registered. Receiving one is
        sufficient proof that subsequent m2e publishes will be delivered.
        """
        try:
            drv.wait_for("heartbeat", lambda _: True, timeout=timeout)
            return True
        except AssertionError:
            log.warning("sim_registry: %s never published heartbeat within %.1fs",
                        drv.module_id, timeout)
            return False

    def autostart(self, replay_scenarios: bool = False) -> None:
        """
        Enable each simulator instance, optionally replaying its sidecar DSL.

        Always waits for the heartbeat (so m2e subscriptions are wired) and
        emits `enable true` so the EvSimulator leaves Disabled and accepts the
        plug_in_* commands the test drives explicitly.

        Scenario replay is OFF by default. The sidecar DSLs are the legacy
        EvManager auto_exec sequences (e.g. `iec_wait_pwr_ready;
        draw_power_regulated 16,3; ...; unplug`); they never `plug` themselves,
        so they cannot start a session in the test harness -- they only wake
        once the test plugs and then draw power / unplug on the same connector,
        racing and corrupting the test-driven session (e.g. violating the
        no-energy assertions). The explicit plug_in_* methods already drive
        their own draw, so the replay is redundant here. Pass
        replay_scenarios=True only for a standalone replay that is not also
        driving the connector. Background threads are daemon so teardown does
        not block.
        """
        for mid, drv in self._by_module.items():
            self._wait_ready(drv)
            drv.enable(True)
            if not replay_scenarios:
                continue
            dsl = self._scenarios_by_module.get(mid)
            if not dsl:
                continue
            t = threading.Thread(target=drv.play_dsl, args=(dsl,),
                                 name=f"evsim-replay-{mid}", daemon=True)
            t.start()
