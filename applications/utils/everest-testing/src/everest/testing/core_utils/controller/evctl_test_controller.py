# SPDX-License-Identifier: Apache-2.0
# Copyright Pionix GmbH and Contributors to EVerest
"""Typed-API MQTT test controller for the EvController module.

This controller publishes commands on the EvController's `m2e/*` topics
and consumes `e2m/*` state/fault/command_ack events back from the
module. It targets the topic layout:

    {prefix}everest_api/1/ev_controller/{module_id}/{m2e|e2m}/{suffix}

where `{prefix}` is the per-run external MQTT prefix exposed by
EverestCore and `{module_id}` is the active EvController module id from
the deployed config (the first configured car).
"""

import json
import time
from typing import Any, Callable, Dict, List, Optional, Union

from everest.testing.core_utils.everest_core import EverestCore
from everest.testing.core_utils.sim_registry import SimRegistry, NoSimulatorError, EvControllerDriver, EvDriverSimDriver


DEFAULT_BPT: Dict[str, float] = {
    "discharge_max_current_limit": 50.0,
    "discharge_max_power_limit": 11000.0,
    "discharge_target_current": 30.0,
    "discharge_minimal_soc": 20.0,
}


class StateCollector:
    """Boolean state and SoC waits over the registry driver's payload history."""

    def __init__(self, driver):
        self._driver = driver
        for var in ("state", "fault", "ev_info", "command_ack"):
            driver.subscribe_var(var)

    @property
    def states(self) -> List[str]:
        return [s for s in self._driver.payloads("state") if isinstance(s, str)]

    @property
    def faults(self) -> List[Dict[str, Any]]:
        return [f for f in self._driver.payloads("fault") if isinstance(f, dict)]

    @property
    def ev_infos(self) -> List[Dict[str, Any]]:
        return [i for i in self._driver.payloads("ev_info") if isinstance(i, dict)]

    def wait_for_state(self, target: str, timeout: float = 30.0) -> bool:
        after = self._driver.payload_count("state")
        states = self.states
        if states and states[-1] == target:
            return True
        try:
            self._driver.wait_for("state", lambda s: s == target, timeout, after=after)
            return True
        except AssertionError:
            return False

    def wait_for_state_not(self, target: str, timeout: float = 30.0) -> bool:
        deadline = time.monotonic() + timeout
        while True:
            states = self.states
            if not states or states[-1] != target:
                return True
            if time.monotonic() >= deadline:
                return False
            time.sleep(0.01)

    def wait_for_soc_progress(self, min_increase: float = 0.01, timeout: float = 30.0) -> bool:
        after = self._driver.payload_count("ev_info")
        baseline = None

        def progressed(info):
            nonlocal baseline
            soc = info.get("soc_pct") if isinstance(info, dict) else None
            if isinstance(soc, (int, float)):
                if baseline is None:
                    baseline = float(soc)
                elif soc >= baseline + min_increase:
                    return True
            return False

        try:
            self._driver.wait_for("ev_info", progressed, timeout, after=after)
            return True
        except AssertionError:
            return False


class EvControllerTestController:
    """Drive EvController over its typed `m2e/*` MQTT API.

    Commands and state observations share one registry driver.
    """

    def __init__(self, everest_core: EverestCore) -> None:
        self._registry = SimRegistry.connect(everest_core)
        self._driver = next((driver for driver in self._registry.drivers.values()
                             if isinstance(driver, EvControllerDriver)), None)
        if self._driver is None:
            self._registry.close()
            raise NoSimulatorError("no EvController instance in the resolved config")
        self.base_e2m = self._driver.base_e2m
        self.base_m2e = self.base_e2m.rsplit("/", 1)[0] + "/m2e"
        self.state_collector = StateCollector(self._driver)
        self.ev_driver = self._driver.world_driver

    def start(self, connector_id: int = 1, timeout: float = 15.0) -> None:
        """Enable the simulator (publishes `m2e/enable` for `connector_id`).

        `timeout` bounds the re-publish retry below. It is a parameter so a
        unit test can exercise the give-up path without burning the full
        default; SIL callers should leave it alone.
        """
        del connector_id  # topic is per-module; connector_id reserved for API symmetry
        # m2e/enable carries a bare JSON bool, not an object: CommandRouter
        # deserializes the payload directly into `bool` (a `{"enable": ...}`
        # object fails adl_deserialize, the throw is swallowed, and the FSM
        # never leaves Disabled). The payload is a bare JSON true.
        #
        # MQTT has no retained delivery here, so an `enable` published before
        # the module finishes subscribing to its m2e topics is silently lost
        # and the FSM stays Disabled forever. Re-publish (Enable is idempotent)
        # until the FSM reports it left Disabled, or give up after the bound.
        deadline = time.time() + timeout
        while time.time() < deadline:
            self._driver.enable(True)
            if self.state_collector.wait_for_state("Unplugged", timeout=1.0):
                return
        # Final attempt recorded; callers' own wait_for_state will surface a
        # clear failure if the module never came up.
        self._driver.enable(True)

    def stop(self) -> None:
        """Close the registry connection; EVerest is owned by the test."""
        self._registry.close()

    def _require_ev_driver(self) -> EvDriverSimDriver:
        if self.ev_driver is None:
            raise NoSimulatorError("world commands require EvDriverSim; HIL uses its physical pilot")
        return self.ev_driver

    def _configure_and_plug(self, payload: Dict[str, Any]) -> None:
        self._require_ev_driver()
        self.configure_session(payload)
        self.plug()

    def plug_in(self, connector_id: int = 1) -> None:
        """Configure a default AC IEC session, plug in, wait for Charging."""
        del connector_id
        self._configure_and_plug(
            {
                "mode": "AcIec",
                "params": {
                    "charging_current_a": 32.0,
                    "three_phases": True,
                },
            },
        )
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
        self._configure_and_plug(
            {"mode": "AcIso2", "params": params},
        )

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
        self._configure_and_plug(
            {"mode": "DcIso2", "params": params},
        )

    def plug_in_dc_d20(
        self,
        connector_id: int = 1,
        payment_type: Optional[str] = None,
        bpt: Union[None, bool, Dict[str, float]] = None,
        mcs: bool = False,
    ) -> None:
        """Configure a DC ISO 15118-20 session, then plug in.

        `bpt=True` sends `DEFAULT_BPT`, a dict sends that dict, and `mcs=True`
        enables MCS.
        """
        del connector_id
        params: Dict[str, Any] = {}
        if payment_type is not None:
            params["payment"] = payment_type
        if bpt is True:
            params["bpt"] = dict(DEFAULT_BPT)
        elif isinstance(bpt, dict):
            params["bpt"] = dict(bpt)
        if mcs:
            params["mcs_enabled"] = True
        self._configure_and_plug(
            {"mode": "DcIsoD20", "params": params},
        )

    def plug(self, connector_id: int = 1) -> None:
        """Plug in using the session config already latched via
        `configure_session` / `play_charging_curve`.

        The `plug_in_dc_*` helpers configure-then-plug in one call, which
        overwrites any previously latched curve. Use this when the session
        (e.g. a looping `ChargingCurve`) was latched separately and must be
        consumed by this plug rather than replaced.
        """
        del connector_id
        self._require_ev_driver().plug()

    def plug_out(self, connector_id: int = 1) -> None:
        """Unplug without negotiating an ISO 15118 stop first."""
        del connector_id
        self._require_ev_driver().unplug()

    def stop_session(self, connector_id: int = 1, abort: bool = False) -> None:
        """End the active session.

        With ``abort`` the V2G session is terminated immediately instead of
        exchanging SessionStop, which is how a scenario stages a malformed
        teardown. The default is the clean stop.
        """
        del connector_id
        # `abort` is omitted unless asked for, so the default teardown keeps
        # the exact payload every existing caller already publishes.
        self._driver.stop_session({'abort': True} if abort else {})

    def set_present_values(
        self,
        present_voltage: float | None = None,
        present_active_power: float | None = None,
        connector_id: int = 1,
    ) -> None:
        """Override the present values the EV reports over ISO 15118.

        EvController otherwise echoes what the EVSE delivered, so this is how a
        test makes the EV report a value the charger did not deliver. Each
        argument left as None keeps echoing that field, so overriding one value
        does not report the other as a measured zero.
        """
        del connector_id
        payload: dict[str, float] = {}
        if present_voltage is not None:
            payload["present_voltage"] = present_voltage
        if present_active_power is not None:
            payload["present_active_power"] = present_active_power
        self._driver.set_present_values(payload)

    def plug_out_iso(self, connector_id: int = 1, abort: bool = False) -> None:
        """Gracefully end an ISO 15118 session, wait for FSM to leave Charging, then unplug."""
        self._require_ev_driver()
        self.stop_session(connector_id, abort=abort)
        self.state_collector.wait_for_state_not("Charging", timeout=30.0)
        self.plug_out(connector_id)

    def pause_session(self, connector_id: int = 1) -> None:
        """Pause the active charging session."""
        del connector_id
        self._driver.pause_session({})

    def resume_session(self, connector_id: int = 1) -> None:
        """Resume a paused charging session."""
        del connector_id
        self._driver.resume_session({})

    def diode_fail(self, connector_id: int = 1) -> None:
        """Inject a CP-diode short fault."""
        self.inject_fault("DiodeFail", connector_id)

    def inject_fault(self, fault_type: str, connector_id: int = 1) -> None:
        """Inject a world fault through EvDriverSim."""
        del connector_id
        self._require_ev_driver().inject_fault(fault_type)

    def clear_fault(self, connector_id: int = 1) -> None:
        """Clear world faults through EvDriverSim."""
        del connector_id
        self._require_ev_driver().clear_fault()

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
        if self.ev_driver is None:
            raise AssertionError("run_scenario requires EvDriverSim")
        self.ev_driver.run_scenario(name, timing=timing)

    def query_state(self, connector_id: int = 1, timeout: float = 5.0) -> Optional[str]:
        after = self._driver.payload_count("state")
        self._driver.query_state()
        try:
            return self._driver.wait_for("state", lambda s: isinstance(s, str), timeout, after=after)
        except AssertionError:
            return None

    def last_observed_state(self, connector_id: int = 1) -> Optional[str]:
        states = self.state_collector.states
        return states[-1] if states else None

    def set_soc(self, soc_pct: float, connector_id: int = 1) -> None:
        """Override the simulated EV state of charge (percent)."""
        del connector_id
        self._driver.set_soc(soc_pct)

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
        self._driver.set_charging_current(current_a, three_phases, ramp_ms)

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

        The curve is latched for the next session and starts on entry to
        Charging. With EvDriverSim present, this waits for configure_session
        acceptance before returning; rejection or timeout raises.
        """
        del connector_id
        self.configure_session(
            {
                "mode": mode,
                "params": {"curve": {"points": list(points), "loop": loop}},
            },
        )

    def configure_session(self, payload: Dict[str, Any]) -> None:
        self._driver.configure_session(payload["mode"], payload.get("params"))

    def add_command_ack_observer(self, observer: Callable[[Dict[str, Any]], None]) -> None:
        """Observe decoded car acknowledgments after they are recorded for command waits.

        Observers run on the MQTT thread and must not block. Each receives its own
        decoded copy. Observer exceptions are logged; remaining observers still run.
        """
        def on_message(_client, _userdata, msg):
            try:
                ack = json.loads(msg.payload)
            except ValueError:
                return
            if isinstance(ack, dict):
                observer(ack)

        self._driver.add_message_observer("command_ack", on_message)

    def add_message_observer(self, var: str, observer: Callable[..., None]) -> None:
        """Observe a car e2m variable without replacing driver callbacks."""
        self._driver.add_message_observer(var, observer)
