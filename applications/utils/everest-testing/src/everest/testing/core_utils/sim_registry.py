# SPDX-License-Identifier: Apache-2.0
# Copyright Pionix GmbH and Contributors to EVerest
"""
EV and Yeti simulator MQTT drivers used by the test harness.

Replaces the legacy `everest_external/nodered/<id>/carsim/*` topic surface
with typed m2e topics:

    everest_api/1/ev_controller/<module_id>/m2e/<command>
    everest_api/1/ev_driver_sim/<module_id>/m2e/<command>
    everest_api/1/yeti_simulator/<module_id>/m2e/<command>

`SimRegistry` discovers EvController, EvDriverSim and YetiSimulator instances
in a resolved everest config. Drivers are indexed by their car's `connector_id`.
"""

from __future__ import annotations

import json
import logging
import os
import threading
import time
from dataclasses import dataclass
from pathlib import Path
from typing import Any, Callable, Dict, List, Optional

import yaml
import paho.mqtt.client as mqtt
from paho.mqtt import __version__ as paho_mqtt_version
from everest.testing.core_utils.common import close_mqtt_client

log = logging.getLogger(__name__)


class SimNotReadyError(AssertionError):
    """An EV simulator instance never proved its m2e subscriptions were wired.

    Derives from AssertionError so a test that trips it reports as a failure
    rather than an error, and so callers already catching AssertionError around
    the driver waits keep working.
    """


class BrokerNotConnectedError(AssertionError):
    """The harness never established its MQTT session with the broker.

    Separate from SimNotReadyError: nothing is known about the simulators yet,
    because the transport the readiness proof travels on is not up.
    """


class NoSimulatorError(AssertionError):
    """The controller has no simulator instance to drive for a connector.

    Derives from AssertionError so a test that asks for a car to be plugged in
    when no car can be reached reports as a failure rather than carrying on
    against a charge point nothing is plugged into.
    """


@dataclass
class _SubInfo:
    var: str
    payloads: List[Any]


class _MqttSimDriver:
    """MQTT publishing, subscriptions and waits shared by EV simulators."""

    def __init__(self, mqtt_client, mqtt_external_prefix: str, module_id: str, *, api: str):
        self._client = mqtt_client
        self._module_id = module_id
        self._api = api
        self._base = f"{mqtt_external_prefix}everest_api/1/{api}/{module_id}"
        self._subs: Dict[str, _SubInfo] = {}
        self._observers: Dict[str, list] = {}
        self._lock = threading.Lock()

    @property
    def module_id(self) -> str:
        return self._module_id

    def _publish(self, var: str, payload: Any = None) -> None:
        topic = f"{self._base}/m2e/{var}"
        body = json.dumps(payload)
        log.debug("%s publish %s %s", self._api, topic, body)
        self._client.publish(topic, body)

    def arm(self) -> None:
        self.subscribe_var("heartbeat")

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
                self._record_payload(info, payload)

            for observer in list(self._observers.get(info.var, [])):
                try:
                    observer(_client, _userdata, msg)
                except Exception:
                    log.exception("simulator message observer failed")

        return _cb

    def _record_payload(self, info: _SubInfo, payload: Any) -> None:
        """Record a received payload while the subscription lock is held."""
        info.payloads.append(payload)

    def resubscribe_all(self) -> None:
        """Restore subscriptions after a broker reconnect."""
        for var in list(self._subs):
            self._client.subscribe(f"{self._base}/e2m/{var}")

    @property
    def base_e2m(self) -> str:
        return f"{self._base}/e2m"

    def payloads(self, var: str) -> List[Any]:
        self.subscribe_var(var)
        with self._lock:
            return list(self._subs[var].payloads)

    def add_message_observer(self, var: str, observer: Callable[..., None]) -> None:
        """Observe MQTT messages after recording them; callbacks must not block."""
        self._observers.setdefault(var, []).append(observer)
        self.subscribe_var(var)

    def seen_any(self, var: str) -> bool:
        """True once at least one payload has arrived on `var`.

        Non-consuming, unlike `wait_for`, so several waiters can poll the same
        variable against one shared deadline.
        """
        info = self._subs.get(var)
        with self._lock:
            return bool(info and info.payloads)

    def payload_count(self, var: str) -> int:
        """Payloads received on `var` so far; pass as `wait_for(after=...)`."""
        info = self._subs.get(var)
        with self._lock:
            return len(info.payloads) if info else 0

    def wait_for(self, var: str, predicate: Callable[[Any], bool], timeout: float = 10.0,
                 after: int = 0) -> Any:
        """Return the first payload on `var` past the first `after` that matches."""
        self.subscribe_var(var)
        info = self._subs[var]
        deadline = time.time() + timeout
        seen = after
        while True:
            expired = time.time() >= deadline
            with self._lock:
                items = info.payloads[seen:]
                seen += len(items)
            for it in items:
                if predicate(it):
                    return it
            if expired:
                break
            time.sleep(0.05)
        raise AssertionError(
            f"{self._api}[{self._module_id}] timeout waiting for {var}; last seen: {info.payloads}"
        )


class EvControllerDriver(_MqttSimDriver):
    """Publish-side wrapper for a single EvController module instance."""

    def __init__(self, mqtt_client, mqtt_external_prefix: str, module_id: str):
        super().__init__(mqtt_client, mqtt_external_prefix, module_id, api="ev_controller")
        self.world_driver: Optional[EvDriverSimDriver] = None

    # ---- m2e commands -----------------------------------------------------
    def enable(self, value: bool = True) -> None:
        self._publish("enable", value)

    def stop_session(self, payload=None) -> None:
        self._publish("stop_session", payload)

    def pause_session(self, payload=None) -> None:
        self._publish("pause_session", payload)

    def resume_session(self, payload=None) -> None:
        self._publish("resume_session", payload)

    def query_state(self) -> None:
        self._publish("query_state", {})

    def set_present_values(self, payload: Dict[str, Any]) -> None:
        self._publish("set_present_values", payload)

    def set_soc(self, soc_pct: float) -> None:
        self._publish("set_soc", {"soc_pct": soc_pct})

    def configure_session(self, mode: str, params: Optional[Dict[str, Any]] = None) -> None:
        # mode in {"AcIec", "AcIso2", "AcIsoD20", "DcIso2", "DcIsoD20"}
        body = {"mode": mode, "params": params or {}}
        if self.world_driver is not None:
            self.subscribe_var("command_ack")
            after = self.payload_count("command_ack")
        self._publish("configure_session", body)
        if self.world_driver is not None:
            ack = self.wait_for(
                "command_ack", lambda ack: isinstance(ack, dict) and ack.get("command") == "configure_session",
                after=after,
            )
            if ack.get("status") != "Accepted":
                raise AssertionError(f"configure_session rejected: {ack.get('reason', 'no reason provided')}")

    def set_charging_current(self, current_a: float, three_phases: bool, ramp_ms: Optional[int] = None) -> None:
        body: Dict[str, Any] = {"current_a": current_a, "three_phases": three_phases}
        if ramp_ms is not None:
            body["ramp_ms"] = ramp_ms
        self._publish("set_charging_current", body)

    def raise_error(self, error: Dict[str, Any]) -> None:
        self._publish("raise_error", error)

    def clear_error(self, error: Dict[str, Any]) -> None:
        self._publish("clear_error", error)

    def bcb_toggle(self, count: Optional[int] = None) -> None:
        self._publish("bcb_toggle", {} if count is None else {"count": count})

    def run_scenario(self, name: str, timing: Optional[Dict[str, int]] = None) -> None:
        if self.world_driver is None:
            raise NoSimulatorError("run_scenario requires EvDriverSim")
        self.world_driver.run_scenario(name, timing=timing)


class CommandAckWaiter:
    # No request id is carried on the wire, so each publish owns its reply.
    """One publish's acknowledgement, consumable once even by concurrent waiters."""

    def __init__(self, command: str):
        self._command = command
        self._lock = threading.Lock()
        self._ack: Optional[Dict[str, Any]] = None
        self._consumed = False

    def _deliver(self, ack: Dict[str, Any]) -> None:
        with self._lock:
            self._ack = ack

    def wait(self, status: str = "Accepted", timeout: float = 10.0) -> Dict[str, Any]:
        """Wait for this publish's ack; timeout consumes nothing."""
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            with self._lock:
                if not self._consumed and self._ack is not None:
                    if status == "Accepted" and self._ack.get("status") == "Rejected":
                        self._consumed = True
                        raise AssertionError(
                            f"{self._command} rejected: {self._ack.get('reason', 'no reason provided')}"
                        )
                    if self._ack.get("status") == status:
                        self._consumed = True
                        return self._ack
            time.sleep(min(0.05, max(0, deadline - time.monotonic())))
        raise AssertionError(f"timeout waiting for {self._command} command_ack")


class EvDriverSimDriver(_MqttSimDriver):
    """World-side wrapper for a single EvDriverSim module instance."""

    def __init__(self, mqtt_client, mqtt_external_prefix: str, module_id: str):
        super().__init__(mqtt_client, mqtt_external_prefix, module_id, api="ev_driver_sim")
        self._pending_acks: Dict[str, List[CommandAckWaiter]] = {}
        self._latest_ack: Dict[str, CommandAckWaiter] = {}
        self._publish_lock = threading.Lock()
        self._scenario_start_lock = threading.Lock()

    def arm(self) -> None:
        for var in ("world_state", "command_ack", "scenario_event", "heartbeat"):
            self.subscribe_var(var)

    @property
    def world_state(self) -> Optional[Dict[str, Any]]:
        with self._lock:
            info = self._subs.get("world_state")
            return info.payloads[-1] if info and info.payloads else None

    def _record_payload(self, info: _SubInfo, payload: Any) -> None:
        super()._record_payload(info, payload)
        if (info.var == "command_ack" and isinstance(payload, dict)
                and isinstance(payload.get("command"), str) and payload.get("status") in ("Accepted", "Rejected")):
            pending = self._pending_acks.get(payload["command"], [])
            if pending:
                pending.pop(0)._deliver(payload)

    def _publish(self, var: str, payload: Any = None) -> CommandAckWaiter:
        with self._publish_lock:
            self.arm()
            waiter = CommandAckWaiter(var)
            with self._lock:
                self._pending_acks.setdefault(var, []).append(waiter)
                self._latest_ack[var] = waiter
            super()._publish(var, payload)
            return waiter

    def wait_for_command_ack(self, command: str, status: str = "Accepted",
                             timeout: float = 10.0) -> Dict[str, Any]:
        """Wait on the latest publish of `command`; retain its returned waiter to wait on an earlier one."""
        with self._lock:
            waiter = self._latest_ack.get(command)
        if waiter is None:
            raise AssertionError(f"no {command} command published")
        return waiter.wait(status, timeout)

    def plug(self) -> CommandAckWaiter:
        return self._publish("plug")

    def unplug(self) -> CommandAckWaiter:
        return self._publish("unplug")

    def inject_fault(self, fault_type: str, rcd_mA: Optional[float] = None,
                     message: Optional[str] = None) -> CommandAckWaiter:
        body: Dict[str, Any] = {"type": fault_type}
        if rcd_mA is not None:
            body["rcd_mA"] = rcd_mA
        if message is not None:
            body["message"] = message
        return self._publish("inject_fault", body)

    def clear_fault(self) -> CommandAckWaiter:
        return self._publish("clear_fault")

    def run_scenario(self, name: str, timing: Optional[Dict[str, int]] = None,
                     timeout: float = 10.0) -> Dict[str, Any]:
        """Serialize starts on this driver through Started/Rejected, releasing on timeout too.

        Finished reports completion separately. Without request ids, a same-name
        Rejected from an independent publisher, or from an earlier run on this
        driver that fails after its own start returned, can be taken as this
        start's outcome.
        """
        with self._scenario_start_lock:
            self.arm()
            after = self.payload_count("scenario_event")
            body: Dict[str, Any] = {"name": name}
            if timing:
                body["timing"] = timing
            self._publish("run_scenario", body).wait(timeout=timeout)
            event = self.wait_for(
                "scenario_event",
                lambda event: isinstance(event, dict) and event.get("name") == name
                and event.get("kind") in ("Started", "Rejected"),
                timeout=timeout, after=after,
            )
            if event["kind"] == "Rejected":
                raise AssertionError(f"run_scenario rejected: {event.get('message', 'no reason provided')}")
            return event


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
    Indexes EV controllers, world drivers and Yeti simulators from a resolved config.
    Connector mappings front the test_controller's connector_id API.
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
        self._by_module: Dict[str, EvControllerDriver | EvDriverSimDriver] = {}
        self._by_connector: Dict[int, EvControllerDriver] = {}
        self._ev_driver_by_connector: Dict[int, EvDriverSimDriver] = {}
        self._yeti_by_connector: Dict[int, YetiSimDriver] = {}
        self._load(config_path)

    @classmethod
    def connect(cls, core, timeout: float = 10.0):
        if paho_mqtt_version < "2.0":
            client = mqtt.Client(core.everest_uuid)
        else:
            client = mqtt.Client(callback_api_version=mqtt.CallbackAPIVersion.VERSION2,
                                 client_id=core.everest_uuid)
        registry = cls(client, core.mqtt_external_prefix, Path(core.everest_config_path))
        connected = threading.Event()

        def on_connect(_client, _userdata, _flags, rc, *_rest):
            if rc == 0:
                registry.resubscribe()
                connected.set()

        client.on_connect = on_connect
        try:
            client.connect(os.environ.get("MQTT_SERVER_ADDRESS", "127.0.0.1"),
                           int(os.environ.get("MQTT_SERVER_PORT", "1883")))
            client.loop_start()
            if not connected.wait(timeout):
                raise BrokerNotConnectedError(f"MQTT broker connection timed out after {timeout:.1f}s")
        except Exception:
            close_mqtt_client(client)
            raise
        registry.arm()
        return registry

    @property
    def client(self):
        return self._mqtt

    def close(self) -> None:
        if self._mqtt is not None:
            close_mqtt_client(self._mqtt)
            self._mqtt = None

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
            if module_name == "EvController":
                drv = EvControllerDriver(self._mqtt, self._prefix, mid)
                self._by_module[mid] = drv
                if isinstance(cid, int):
                    self._by_connector[cid] = drv
            elif module_name == "YetiSimulator":
                ydrv = YetiSimDriver(self._mqtt, self._prefix, mid)
                if isinstance(cid, int):
                    self._yeti_by_connector[cid] = ydrv

        for mid, mblock in modules.items():
            if not isinstance(mblock, dict) or mblock.get("module") != "EvDriverSim":
                continue
            drv = EvDriverSimDriver(self._mqtt, self._prefix, mid)
            self._by_module[mid] = drv
            car_id = (mblock.get("config_module") or {}).get("ev_controller_module_id", "ev_manager")
            car = self._by_module.get(car_id)
            if isinstance(car, EvControllerDriver):
                car.world_driver = drv
                cid = (modules[car_id].get("config_module") or {}).get("connector_id")
                if isinstance(cid, int):
                    self._ev_driver_by_connector[cid] = drv

    @property
    def drivers(self) -> Dict[str, EvControllerDriver | EvDriverSimDriver]:
        return dict(self._by_module)

    def by_module(self, module_id: str) -> Optional[EvControllerDriver | EvDriverSimDriver]:
        return self._by_module.get(module_id)

    def ev_by_connector(self, connector_id: int) -> Optional[EvControllerDriver]:
        return self._by_connector.get(connector_id)

    def ev_driver_by_connector(self, connector_id: int) -> Optional[EvDriverSimDriver]:
        return self._ev_driver_by_connector.get(connector_id)

    def yeti_by_connector(self, connector_id: int) -> Optional[YetiSimDriver]:
        return self._yeti_by_connector.get(connector_id)

    def arm(self) -> None:
        """Subscribe every EV controller and world driver's heartbeat topic.

        Call before EVerest boots. The heartbeat is not a retained topic, so a
        subscription registered after the module's `ready()` can only pick up a
        later tick; subscribing first makes `require_ready` a budget rather
        than a bet on scheduling order. Idempotent.
        """
        for drv in self._by_module.values():
            drv.arm()

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
            raise SimNotReadyError(f"No heartbeat from {', '.join(pending)} within {budget:.1f}s")
        self._ready = True

    def autostart(self) -> None:
        """
        Enable each simulator instance.

        Emits `enable true` so the EvController leaves Disabled and accepts the
        plug_in_* commands the test drives explicitly. This publish is
        best-effort by design: `EvController`'s `enabled_at_startup` defaults to
        true, so a dropped `enable` costs nothing, and the readiness gate that
        protects the commands which do matter lives in `require_ready`, driven
        from the point of use.
        """
        for drv in self._by_module.values():
            if isinstance(drv, EvControllerDriver):
                drv.enable(True)
