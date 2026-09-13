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


class SimNotReadyError(AssertionError):
    """An EvSimulator instance never proved its m2e subscriptions were wired.

    Derives from AssertionError so a test that trips it reports as a failure
    rather than an error, and so callers already catching AssertionError around
    the driver waits keep working.
    """


class BrokerNotConnectedError(AssertionError):
    """The harness never established its MQTT session with the broker.

    Separate from SimNotReadyError: nothing is known about the simulators yet,
    because the transport the readiness proof travels on is not up.
    """


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
        # SUBACK bookkeeping. A subscription is only established once the
        # broker acknowledges it AND grants it; either half missing means the
        # topic is silent for a transport reason, not a module reason, and the
        # gate has to be able to say which.
        self._mid_var: Dict[int, str] = {}
        self._granted: Dict[str, bool] = {}

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
        result = self._client.subscribe(topic)
        mid = result[1] if isinstance(result, tuple) and len(result) == 2 else None
        if isinstance(mid, int):
            with self._lock:
                self._mid_var[mid] = var
        # paho does not queue a SUBSCRIBE issued while disconnected: it returns
        # MQTT_ERR_NO_CONN and drops it. `message_callback_add` below still
        # succeeds, so a failed subscribe is indistinguishable from a silent
        # peer -- every wait on `var` then burns its whole budget for a reason
        # the log never states. Say so. Non-paho test doubles return None, so
        # only a real (rc, mid) pair is inspected.
        if isinstance(result, tuple) and len(result) == 2 and result[0] != 0:
            log.error("ev_sim[%s]: subscribe to %s failed with rc=%s; no e2m "
                      "message on this topic can be delivered",
                      self._module_id, topic, result[0])
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

    def note_suback(self, mid: int, granted: bool) -> bool:
        """Record a SUBACK. Returns True if `mid` belonged to this driver."""
        with self._lock:
            var = self._mid_var.pop(mid, None)
            if var is None:
                return False
            self._granted[var] = granted
            return True

    def subscription_state(self, var: str) -> str:
        """One of 'granted', 'refused', 'unacknowledged'."""
        with self._lock:
            if var not in self._granted:
                return "unacknowledged"
            return "granted" if self._granted[var] else "refused"

    def resubscribe_all(self) -> None:
        """Re-issue SUBSCRIBE for every already-registered var.

        paho does not restore subscriptions across a reconnect, and a
        subscription lost that way is silent: the topic simply goes quiet.
        Topic filters and callbacks are already registered, so only the wire
        SUBSCRIBE is repeated; buffered payloads are left untouched.
        """
        for var in list(self._subs):
            result = self._client.subscribe(f"{self._base}/e2m/{var}")
            mid = result[1] if isinstance(result, tuple) and len(result) == 2 else None
            with self._lock:
                # The prior grant says nothing about the new session.
                self._granted.pop(var, None)
                if isinstance(mid, int):
                    self._mid_var[mid] = var

    def seen_any(self, var: str) -> bool:
        """True once at least one payload has arrived on `var`.

        Non-consuming, unlike `wait_for`, so several waiters can poll the same
        variable against one shared deadline.
        """
        info = self._subs.get(var)
        with self._lock:
            return bool(info and info.payloads)

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
        readiness_timeout: float = 15.0,
    ):
        self._mqtt = mqtt_client
        self._prefix = mqtt_external_prefix
        self._readiness_timeout = readiness_timeout
        self._ready = False
        self._by_module: Dict[str, EvSimDriver] = {}
        self._by_connector: Dict[int, EvSimDriver] = {}
        self._yeti_by_connector: Dict[int, YetiSimDriver] = {}
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

    @property
    def drivers(self) -> Dict[str, EvSimDriver]:
        return dict(self._by_module)

    def by_module(self, module_id: str) -> Optional[EvSimDriver]:
        return self._by_module.get(module_id)

    def ev_by_connector(self, connector_id: int) -> Optional[EvSimDriver]:
        return self._by_connector.get(connector_id)

    def yeti_by_connector(self, connector_id: int) -> Optional[YetiSimDriver]:
        return self._yeti_by_connector.get(connector_id)

    def arm(self) -> None:
        """Subscribe every EvSimulator instance's heartbeat topic.

        Call before EVerest boots. The heartbeat is not a retained topic, so a
        subscription registered after the module's `ready()` can only pick up a
        later tick; subscribing first makes `require_ready` a budget rather
        than a bet on scheduling order. Idempotent.
        """
        for drv in self._by_module.values():
            drv.subscribe_var("heartbeat")

    def note_suback(self, mid: int, granted: bool) -> None:
        """Route a broker SUBACK to whichever driver issued that mid."""
        for drv in self._by_module.values():
            if drv.note_suback(mid, granted):
                return

    def resubscribe(self) -> None:
        """Re-issue every driver's subscriptions after a reconnect."""
        for drv in self._by_module.values():
            drv.resubscribe_all()

    def _pending_ready(self, timeout: float) -> List[str]:
        """Return the module ids that did not publish a heartbeat in `timeout`.

        One shared budget for all instances: they boot concurrently, so waiting
        per instance multiplies the worst case by the instance count without
        learning anything the concurrent wait does not.
        """
        self.arm()
        deadline = time.time() + timeout
        pending = dict(self._by_module)
        while pending:
            for mid in [m for m, d in pending.items() if d.seen_any("heartbeat")]:
                pending.pop(mid)
            if not pending or time.time() >= deadline:
                break
            time.sleep(0.05)
        return sorted(pending)

    def require_ready(self, timeout: Optional[float] = None) -> None:
        """Block until every instance has proven its m2e wiring, else raise.

        The heartbeat thread starts in `ready()`, immediately after the m2e
        subscriptions are registered, so one heartbeat is proof that subsequent
        m2e publishes will be delivered -- and its absence is proof that they
        will not. Raising here, at the point where a test is about to drive the
        EV, is the whole point: publishing into an unsubscribed module drops the
        command silently and the test dies much later as an unrelated timeout.

        Deliberately NOT called from `autostart`. Every OCPP test reaches
        `TestController.start()` from a running asyncio loop that also serves
        the in-process CSMS, so blocking there stops the charge point's
        websocket handshake from being answered and breaks stacks that never
        touch the EV at all. Probe-module configs cannot satisfy the gate at
        `start()` time in any case: the manager withholds the global ready
        signal -- and therefore `ready()` -- until the probe module connects,
        which happens after `start()` has returned.
        """
        if self._ready:
            return
        budget = self._readiness_timeout if timeout is None else timeout
        pending = self._pending_ready(budget)
        if pending:
            raise SimNotReadyError(self._explain_not_ready(pending, budget))
        self._ready = True

    _TRANSPORT_NOTE = (
        "The broker never acknowledged the harness's subscription, so no "
        "heartbeat could have been observed even if the instance published one "
        "on time. Nothing is known about the simulator here: this is a harness "
        "or transport fault, not an EvSimulator finding."
    )
    _REFUSED_NOTE = (
        "The broker refused the harness's subscription (SUBACK carried a "
        "failure code), so the topic is silent by the broker's decision. This "
        "is a harness or broker fault, not an EvSimulator finding."
    )
    _MODULE_NOTE = (
        "The harness's subscription was acknowledged and granted, so it was "
        "listening the whole time and the instance did not publish. Its m2e "
        "subscriptions are registered in the same ready() that starts the "
        "heartbeat, so every enable/plug/configure command published to it "
        "would be dropped without an error. This one is an EvSimulator "
        "finding, not a harness artifact."
    )

    def _explain_not_ready(self, pending: List[str], budget: float) -> str:
        """Say which of the two causes applies, per instance.

        'No heartbeat' has two causes with opposite owners. Reporting them
        identically is what made the previous readiness check unreadable, so
        the gate resolves them here rather than leaving it to whoever reads
        the failure.
        """
        by_state: Dict[str, List[str]] = {}
        for mid in pending:
            drv = self._by_module[mid]
            by_state.setdefault(drv.subscription_state("heartbeat"), []).append(mid)
        notes = {
            "unacknowledged": self._TRANSPORT_NOTE,
            "refused": self._REFUSED_NOTE,
            "granted": self._MODULE_NOTE,
        }
        parts = [
            f"EvSimulator instance(s) {', '.join(sorted(pending))} never "
            f"published a heartbeat within {budget:.1f}s."
        ]
        # Prefix each note with the instances it applies to only when they
        # disagree; when there is one cause the leading list already said it.
        label = len(by_state) > 1
        for state in ("unacknowledged", "refused", "granted"):
            ids = by_state.get(state)
            if not ids:
                continue
            prefix = f"[{', '.join(sorted(ids))}] " if label else ""
            parts.append(f"{prefix}{notes[state]}")
        return " ".join(parts)

    def autostart(self) -> None:
        """
        Enable each simulator instance.

        Emits `enable true` so the EvSimulator leaves Disabled and accepts the
        plug_in_* commands the test drives explicitly. This publish is
        best-effort by design: `EvSimulator`'s `enabled_at_startup` defaults to
        true, so a dropped `enable` costs nothing, and the readiness gate that
        protects the commands which do matter lives in `require_ready`, driven
        from the point of use.
        """
        for drv in self._by_module.values():
            drv.enable(True)
