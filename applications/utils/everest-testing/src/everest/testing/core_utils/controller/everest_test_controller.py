import json
import logging
import os
import threading
import time
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any, List, Optional

import paho.mqtt.client as mqtt
from paho.mqtt import __version__ as paho_mqtt_version

from everest.testing.core_utils.everest_core import EverestCore
from everest.testing.core_utils.sim_registry import (
    BrokerNotConnectedError,
    EvSimDriver,
    SimRegistry,
    YetiSimDriver,
)

from everest.testing.core_utils.controller.test_controller_interface import TestController


log = logging.getLogger(__name__)

# Bound on establishing the MQTT session. Generous: this is a local broker,
# and the cost of being wrong is a hard failure, not a retry.
_BROKER_CONNECT_TIMEOUT_S = 10.0

# Kept waiting after the budget expires, purely to classify the failure. A
# CONNACK that lands in here proves the broker was serving and the budget was
# short, which is the one thing a client cannot otherwise tell apart from a
# broker that never answered at all.
_BROKER_CONNACK_GRACE_S = 8.0


class _SessionCensus:
    """Per-process tally of MQTT sessions this harness asked the broker for.

    A single failure says nothing about whether the broker was ever usable.
    The tally turns one error message into a timeline: "session 181, and 180
    of them worked" is a broker that stopped serving mid-run, which is
    neither a short budget nor a wrong address, and no amount of detail
    about the failing session alone can say that.
    """

    def __init__(self):
        self._lock = threading.Lock()
        self._attempted = 0
        self._established = 0
        self._last_established_at: Optional[float] = None

    def attempt(self) -> tuple:
        with self._lock:
            self._attempted += 1
            since = (None if self._last_established_at is None
                     else time.monotonic() - self._last_established_at)
            return (self._attempted, self._established, since)

    def established(self) -> None:
        with self._lock:
            self._established += 1
            self._last_established_at = time.monotonic()


_SESSIONS = _SessionCensus()


def _reason_is_failure(code: Any) -> bool:
    """True if a paho reason code denotes a refusal.

    v2 hands back objects exposing `is_failure`; v1 hands back ints where
    non-zero (CONNACK, SUBACK grant >= 0x80) means refused. A refusal still
    arrives as a normal callback, so it has to be checked rather than assumed.
    """
    failed = getattr(code, "is_failure", None)
    if failed is not None:
        return bool(failed)
    return isinstance(code, int) and code != 0


def _first_reason_code(args: tuple) -> Any:
    """Pick the reason code out of a paho callback's positional arguments.

    Both callback API versions put `client` and `userdata` first and the
    reason code somewhere after; the intervening flags argument is a dict
    (v1) or a flags object (v2), neither of which is an int and neither of
    which exposes `is_failure`. Returns None when no candidate is present.
    """
    for arg in args[2:]:
        if isinstance(arg, bool):
            continue
        if isinstance(arg, int) or hasattr(arg, "is_failure"):
            return arg
    return None


def _sock_name(getter) -> Optional[str]:
    try:
        return str(getter())
    except OSError:
        return None


@dataclass
class BrokerConnectProbe:
    """What the transport actually did while the MQTT session was being set up.

    The point of failure is `CONNACK never arrived`, which on its own cannot
    tell a budget that is too short from a broker that accepted the socket and
    then refused to serve the session. Every field here exists to separate
    those two: the timings say whether anything was slow, and the socket and
    reason-code counters say whether the peer was talking to us at all.
    """

    host: str
    port: int
    budget_s: float
    tcp_connect_s: Optional[float] = None
    peer: Optional[str] = None
    local: Optional[str] = None
    waited_s: Optional[float] = None
    connack_late: Optional[bool] = None
    socket_at_timeout: Optional[str] = None
    session_ordinal: int = 0
    sessions_established_before: int = 0
    since_last_established_s: Optional[float] = None
    socket_opens: int = 0
    socket_closes: int = 0
    connect_failures: int = 0
    connack_refusals: List[str] = field(default_factory=list)

    def __post_init__(self):
        self._lock = threading.Lock()
        self._frozen = False

    def freeze(self) -> None:
        """Stop recording, so the probe cannot outlive its own verdict.

        Teardown disconnects the client, which fires the same callbacks the
        counters below are counting. Without this, a probe read after the
        fact reports a socket close that happened during cleanup and
        contradicts the error message that was already rendered from it.
        """
        with self._lock:
            self._frozen = True

    # ---- network-thread callbacks ----------------------------------------
    def note_socket_open(self) -> None:
        with self._lock:
            if not self._frozen:
                self.socket_opens += 1

    def note_socket_close(self) -> None:
        with self._lock:
            if not self._frozen:
                self.socket_closes += 1

    def note_connect_failure(self) -> None:
        with self._lock:
            if not self._frozen:
                self.connect_failures += 1

    def note_connack_refusal(self, code: Any) -> None:
        with self._lock:
            if not self._frozen:
                self.connack_refusals.append(str(code))

    # ---- rendering --------------------------------------------------------
    def describe(self) -> str:
        with self._lock:
            parts = [
                f"target={self.host}:{self.port}",
                f"tcp_connect={self._secs(self.tcp_connect_s)}",
                f"peer={self.peer}",
                f"local={self.local}",
                f"budget={self.budget_s:.1f}s",
                f"waited={self._secs(self.waited_s)}",
                f"connack_late={self.connack_late}",
                f"session={self.session_ordinal} of this process",
                f"established_before={self.sessions_established_before}",
                f"since_last_established={self._secs(self.since_last_established_s)}",
                f"socket_at_timeout={self.socket_at_timeout}",
                f"socket_opens={self.socket_opens}",
                f"socket_closes={self.socket_closes}",
                f"connect_failures={self.connect_failures}",
                f"connack_refusals={self.connack_refusals or 'none'}",
            ]
        return ", ".join(parts)

    def verdict(self) -> str:
        """One sentence naming which cause the numbers point at.

        Deliberately refuses to guess: the `undetermined` arm is a real
        outcome, not a fallback, and says so rather than picking the more
        convenient of the two causes.
        """
        with self._lock:
            refused = bool(self.connack_refusals)
            churned = self.socket_closes > 0 or self.connect_failures > 0
            quiet = self.socket_opens <= 1 and not churned
            waited = self.waited_s
            late = self.connack_late
            budget = self.budget_s
            established_before = self.sessions_established_before
            since_last = self.since_last_established_s
        history = ""
        if established_before:
            history = (
                f". This process already established {established_before} "
                f"session(s) against the same address"
                + (f", the last one {since_last:.0f}s ago"
                   if since_last is not None else "")
                + ", so neither the address nor the budget was wrong at the "
                "start of the run: the broker stopped serving part way "
                "through it.")
        if late:
            return (f"the CONNACK was merely LATE: it arrived after the "
                    f"{budget:.1f}s budget but within the grace that follows "
                    "it, so the broker was serving and the budget is the fix"
                    + history)
        if refused:
            return ("broker REFUSED the session (CONNACK carried a failure "
                    "reason); this is not a timing problem" + history)
        if churned:
            return ("broker accepted the socket then dropped it before CONNACK "
                    "(see socket_closes/connect_failures); the broker is "
                    "listening but not serving this session, so raising the "
                    "budget would not help" + history)
        if waited is not None and waited > budget * 1.5:
            return (f"the waiter itself was descheduled: it asked for "
                    f"{budget:.1f}s and only came back after {waited:.1f}s, so "
                    "the host, not the broker, was starved" + history)
        if quiet:
            return ("one socket opened and stayed open, and nothing at all came "
                    "back on it: the broker never answered CONNECT within the "
                    "budget" + history)
        return ("undetermined: the counters above do not separate the two "
                "causes" + history)

    @staticmethod
    def _secs(value: Optional[float]) -> str:
        return "n/a" if value is None else f"{value:.3f}s"

# Map of legacy short-name error types to their full namespaced equivalents.
# Source of truth: modules/Simulation/YetiSimulator/util/errors.cpp.
# Note: fully-qualified types (containing "/") pass through unchanged via
# _translate_legacy_error_type's early-return; they need no map entry.
_LEGACY_ERROR_TYPE_MAP: dict = {
    # evse_board_support
    "DiodeFault": "evse_board_support/DiodeFault",
    "BrownOut": "evse_board_support/BrownOut",
    "EnergyManagement": "evse_board_support/EnergyManagement",
    "PermanentFault": "evse_board_support/PermanentFault",
    "MREC2GroundFailure": "evse_board_support/MREC2GroundFailure",
    "MREC3HighTemperature": "evse_board_support/MREC3HighTemperature",
    "MREC4OverCurrentFailure": "evse_board_support/MREC4OverCurrentFailure",
    "MREC5OverVoltage": "evse_board_support/MREC5OverVoltage",
    "MREC6UnderVoltage": "evse_board_support/MREC6UnderVoltage",
    "MREC8EmergencyStop": "evse_board_support/MREC8EmergencyStop",
    "MREC10InvalidVehicleMode": "evse_board_support/MREC10InvalidVehicleMode",
    "MREC14PilotFault": "evse_board_support/MREC14PilotFault",
    "MREC15PowerLoss": "evse_board_support/MREC15PowerLoss",
    "MREC17EVSEContactorFault": "evse_board_support/MREC17EVSEContactorFault",
    "MREC18CableOverTempDerate": "evse_board_support/MREC18CableOverTempDerate",
    "MREC19CableOverTempStop": "evse_board_support/MREC19CableOverTempStop",
    "MREC20PartialInsertion": "evse_board_support/MREC20PartialInsertion",
    "MREC23ProximityFault": "evse_board_support/MREC23ProximityFault",
    "MREC24ConnectorVoltageHigh": "evse_board_support/MREC24ConnectorVoltageHigh",
    "MREC25BrokenLatch": "evse_board_support/MREC25BrokenLatch",
    "MREC26CutCable": "evse_board_support/MREC26CutCable",
    "TiltDetected": "evse_board_support/TiltDetected",
    "WaterIngressDetected": "evse_board_support/WaterIngressDetected",
    "EnclosureOpen": "evse_board_support/EnclosureOpen",
    # ac_rcd (underscore-prefixed legacy form)
    "ac_rcd_MREC2GroundFailure": "ac_rcd/MREC2GroundFailure",
    "ac_rcd_VendorError": "ac_rcd/VendorError",
    "ac_rcd_Selftest": "ac_rcd/Selftest",
    "ac_rcd_AC": "ac_rcd/AC",
    "ac_rcd_DC": "ac_rcd/DC",
    # connector_lock (underscore-prefixed legacy form)
    "lock_ConnectorLockCapNotCharged": "connector_lock/ConnectorLockCapNotCharged",
    "lock_ConnectorLockUnexpectedOpen": "connector_lock/ConnectorLockUnexpectedOpen",
    "lock_ConnectorLockUnexpectedClose": "connector_lock/ConnectorLockUnexpectedClose",
    "lock_ConnectorLockFailedLock": "connector_lock/ConnectorLockFailedLock",
    "lock_ConnectorLockFailedUnlock": "connector_lock/ConnectorLockFailedUnlock",
    "lock_MREC1ConnectorLockFailure": "connector_lock/MREC1ConnectorLockFailure",
    "lock_VendorError": "connector_lock/VendorError",
}


def _translate_legacy_error_type(short_name: str) -> str:
    """Return the full ErrorDefinition type for a legacy short name.

    Handles three forms passed by legacy callers:
      - bare short name: "MREC6UnderVoltage"
      - underscore-prefixed: "ac_rcd_MREC2GroundFailure", "lock_ConnectorLockFailedLock"
      - already-slash-prefixed: "powermeter/CommunicationFault"

    Unknown names are returned verbatim (the router will reject them) and a
    warning is emitted.
    """
    if "/" in short_name:
        # Already a full type; pass through.
        return short_name
    full = _LEGACY_ERROR_TYPE_MAP.get(short_name)
    if full is None:
        log.warning("_translate_legacy_error_type: unknown short name %r — passing through", short_name)
        return short_name
    return full


# EvSimulator's PaymentOption codec accepts only the PascalCase enum spellings
# ("ExternalPayment", "Contract"); the legacy EvManager DSL accepted lowercase
# ("externalpayment", "contract"). Tests still pass the legacy spelling, so
# normalize here (case-insensitively) before it reaches configure_session.
# Source of truth: lib/everest/everest_api_types/.../ev_simulator/json_codec.cpp.
_PAYMENT_OPTION_MAP: dict = {
    "contract": "Contract",
    "externalpayment": "ExternalPayment",
    "external_payment": "ExternalPayment",
    "eim": "ExternalPayment",
}


def _normalize_payment_option(payment_type: str) -> str:
    """Map a legacy/lowercase payment name to the EvSimulator PaymentOption enum.

    Raises ValueError on an unknown spelling. configure_session is
    fire-and-forget, so a value the EvSimulator codec rejects is dropped without
    a test-visible error and the session silently falls back to EIM. Failing
    here makes the misconfiguration loud at the call site instead.
    """
    key = payment_type.strip().lower()
    try:
        return _PAYMENT_OPTION_MAP[key]
    except KeyError as e:
        raise ValueError(
            f"unknown payment_type {payment_type!r}; expected one of "
            f"{sorted(set(_PAYMENT_OPTION_MAP.values()))} (case-insensitive)"
        ) from e


class EverestTestController(TestController):

    def __init__(self,
                 everest_core: EverestCore
                 ):
        self._everest_core = everest_core
        self._mqtt_client = None
        self._registry: SimRegistry | None = None
        self._connected = threading.Event()
        self._probe: BrokerConnectProbe | None = None

    @property
    def _mqtt_external_prefix(self):
        return self._everest_core.mqtt_external_prefix

    def start(self, broker_connect_timeout: float = _BROKER_CONNECT_TIMEOUT_S):
        self._initialize_external_mqtt_client(broker_connect_timeout)
        probe = self._probe
        # Start the mqtt loop early so registry subscriptions registered
        # before/while EvSimulator's heartbeat starts firing are delivered.
        self._mqtt_client.loop_start()
        # A SUBSCRIBE issued before CONNACK is written to the socket and
        # returns rc=0, but the subscription may never be established -- the
        # topic then just stays silent. Since arm() now runs microseconds after
        # connect() instead of after the manager has booted, that race is real,
        # so wait for the broker to acknowledge the session first.
        #
        # Measure the wait rather than trusting the budget: `wait(10)` that
        # returns after 40s means this process was descheduled, which is a
        # different defect from a broker that stayed silent for 10s, and the
        # error below has to be able to say which.
        wait_started = time.monotonic()
        acknowledged = self._connected.wait(broker_connect_timeout)
        probe.waited_s = time.monotonic() - wait_started
        if not acknowledged:
            probe.socket_at_timeout = (
                "closed" if self._mqtt_client.socket() is None else "open")
            # Freeze first: every other field is a snapshot at the deadline,
            # and letting the counters run through the grace below would put
            # them on a different clock from socket_at_timeout and waited_s.
            probe.freeze()
            # The budget expiring does not mean the broker never answered, and
            # saying so is the one error this instrument must not make: the
            # reader would go hunting for a broken broker. Keep waiting, and
            # let a CONNACK that lands here name the budget as the fix.
            probe.connack_late = self._connected.wait(_BROKER_CONNACK_GRACE_S)
            error = BrokerNotConnectedError(
                f"MQTT broker did not acknowledge the connection within "
                f"{broker_connect_timeout:.1f}s. No simulator subscription can "
                "be established, so readiness could not be observed even if "
                "every module were healthy; refusing to start EVerest blind.\n"
                f"  transport: {probe.describe()}\n"
                f"  verdict:   {probe.verdict()}"
            )
            error.probe = probe
            raise error
        # Build simulator driver registry from the resolved config so
        # plug_in/plug_out helpers below dispatch to typed m2e topics
        # instead of the legacy `everest_external/nodered/...` surface.
        # EverestCore writes the temporary config in its constructor, so this
        # is available before the manager is spawned -- and it has to be:
        # `arm()` must own the heartbeat subscriptions before the first tick
        # can be published, because that topic is not retained.
        self._registry = SimRegistry(
            mqtt_client=self._mqtt_client,
            mqtt_external_prefix=self._mqtt_external_prefix,
            config_path=Path(self._everest_core.everest_config_path),
        )
        self._registry.arm()
        self._everest_core.start()
        # Enable the simulators so the explicit plug_in_* helpers below can
        # drive sessions. No readiness wait here: see
        # SimRegistry.require_ready.
        self._registry.autostart()

    def stop(self, *exc_details):
        self._everest_core.stop()
        self._destroy_mqtt_client()

    def _initialize_external_mqtt_client(
            self, broker_connect_timeout: float = _BROKER_CONNECT_TIMEOUT_S):
        mqtt_server_uri = os.environ.get("MQTT_SERVER_ADDRESS", "127.0.0.1")
        mqtt_server_port = int(os.environ.get("MQTT_SERVER_PORT", "1883"))
        self._connected.clear()
        probe = BrokerConnectProbe(host=mqtt_server_uri, port=mqtt_server_port,
                                   budget_s=broker_connect_timeout)
        (probe.session_ordinal, probe.sessions_established_before,
         probe.since_last_established_s) = _SESSIONS.attempt()
        self._probe = probe

        if paho_mqtt_version < '2.0':
            self._mqtt_client = mqtt.Client(self._everest_core.everest_uuid)
        else:
            self._mqtt_client = mqtt.Client(
                callback_api_version=mqtt.CallbackAPIVersion.VERSION2, client_id=self._everest_core.everest_uuid)
        self._mqtt_client.on_connect = self._on_broker_connect
        self._mqtt_client.on_subscribe = self._on_broker_suback
        # These four say whether the peer was talking to us at all while the
        # CONNACK failed to arrive. Without them a silent socket and a socket
        # the broker keeps hanging up on produce the same error text.
        self._mqtt_client.on_socket_open = self._on_socket_open
        self._mqtt_client.on_socket_close = self._on_socket_close
        self._mqtt_client.on_connect_fail = self._on_connect_fail

        connect_started = time.monotonic()
        self._mqtt_client.connect(mqtt_server_uri, mqtt_server_port)
        probe.tcp_connect_s = time.monotonic() - connect_started
        # connect() returning means the TCP session is established and CONNECT
        # is on the wire, so a failure after this point is the broker declining
        # to answer, not an unreachable address. Record who answered.
        sock = self._mqtt_client.socket()
        if sock is not None:
            probe.peer = _sock_name(sock.getpeername)
            probe.local = _sock_name(sock.getsockname)

    def _on_socket_open(self, _client, _userdata, _sock):
        if self._probe is not None:
            self._probe.note_socket_open()

    def _on_socket_close(self, _client, _userdata, _sock):
        if self._probe is not None:
            self._probe.note_socket_close()

    def _on_connect_fail(self, _client, _userdata):
        if self._probe is not None:
            self._probe.note_connect_failure()

    def _on_broker_suback(self, _client, _userdata, mid, codes=None, *_rest):
        # Third positional is `mid` under both the v1 and v2 callback APIs; the
        # fourth is granted_qos (v1, ints where 0x80 means failure) or
        # reason_code_list (v2, objects exposing is_failure). A SUBACK that
        # refuses the subscription still arrives, so grant has to be checked
        # rather than assumed. Fires on the network thread.
        # NOTE: not _reason_is_failure(): a v1 SUBACK reports the *granted*
        # QoS, so 1 and 2 are successes and only 0x80 is a refusal. Reusing the
        # CONNACK rule here would report every QoS 1 subscription as refused.
        granted = True
        for code in (codes or []):
            failed = getattr(code, "is_failure", None)
            if failed is None:
                failed = isinstance(code, int) and code >= 0x80
            if failed:
                granted = False
                break
        if self._registry is not None:
            self._registry.note_suback(mid, granted)

    def _on_broker_connect(self, *args):
        # Signature differs between paho v1 and v2 callback APIs; only the
        # CONNACK reason code is wanted. Fires on the network thread.
        #
        # A refused CONNACK arrives through this same callback. Treating it as
        # a connection would arm subscriptions on a session the broker has
        # already declined and then blame the silence on the modules, so the
        # refusal is recorded and the wait is left to expire with the reason
        # code attached.
        code = _first_reason_code(args)
        if _reason_is_failure(code):
            if self._probe is not None:
                self._probe.note_connack_refusal(code)
            log.error("MQTT broker refused the session: CONNACK reason %s", code)
            return
        # Count the session, not the CONNACK: paho reconnects on its own, and
        # counting each reconnect would report a flapping session as several
        # healthy ones and hide exactly the instability the census exists to
        # expose.
        if not self._connected.is_set():
            _SESSIONS.established()
        self._connected.set()
        # paho does not restore subscriptions across a reconnect. Re-issue them
        # so a dropped session does not silently mute the heartbeat topics.
        if self._registry is not None:
            self._registry.resubscribe()

    # ---- internal: connector_id -> driver --------------------------------
    def _ev_drv(self, connector_id: int) -> Optional[EvSimDriver]:
        if self._registry is None:
            return None
        d = self._registry.ev_by_connector(connector_id)
        if d is None:
            log.warning("ev_sim: no EvSimulator instance for connector_id=%s", connector_id)
            return None
        # Single funnel for every EV-driving helper below, so the readiness
        # gate is paid exactly once per boot and only by tests that actually
        # drive the EV. Raises SimNotReadyError naming the instance rather
        # than publishing a command that would be silently dropped.
        self._registry.require_ready()
        return d

    def _yeti_drv(self, connector_id: int) -> Optional[YetiSimDriver]:
        if self._registry is None:
            return None
        d = self._registry.yeti_by_connector(connector_id)
        if d is None:
            log.warning("yeti_sim: no YetiSimulator instance for connector_id=%s", connector_id)
        return d

    # ---- public API (matches the legacy EvManager-based controller) -------
    def plug_in(self, connector_id=1):
        # Legacy DSL: sleep 1;iec_wait_pwr_ready;sleep 1;draw_power_regulated 32,1;sleep 200;unplug
        drv = self._ev_drv(connector_id)
        if drv is None:
            return
        drv.configure_session("AcIec", {"charging_current_a": 32.0, "three_phases": False})
        drv.plug()
        threading.Thread(
            target=drv.play_dsl,
            args=("sleep 1;iec_wait_pwr_ready;sleep 1;draw_power_regulated 32,1;sleep 200;unplug",),
            daemon=True, name=f"evsim-plugin-{connector_id}").start()

    def plug_in_ac_iso(self, connector_id=1, payment_type=""):
        drv = self._ev_drv(connector_id)
        if drv is None:
            return
        params: dict = {"charging_current_a": 16.0, "three_phases": True,
                        "departure_time_s": 86400, "e_amount_wh": 0}
        if payment_type:
            params["payment"] = _normalize_payment_option(payment_type)
        drv.configure_session("AcIso2", params)
        drv.plug()
        threading.Thread(
            target=drv.play_dsl,
            args=("sleep 1;iso_wait_slac_matched;iso_wait_pwr_ready;iso_draw_power_regulated 16,3;sleep 60;iso_stop_charging;iso_wait_v2g_session_stopped;unplug",),
            daemon=True, name=f"evsim-plugin-acico-{connector_id}").start()

    def plug_in_dc_iso(self, connector_id=1, payment_type=""):
        drv = self._ev_drv(connector_id)
        if drv is None:
            return
        params: dict = {"departure_time_s": 86400, "e_amount_wh": 0}
        if payment_type:
            params["payment"] = _normalize_payment_option(payment_type)
        drv.configure_session("DcIso2", params)
        drv.plug()
        threading.Thread(
            target=drv.play_dsl,
            args=("sleep 1;iso_wait_slac_matched;iso_wait_pwr_ready;iso_wait_for_stop 60;iso_wait_v2g_session_stopped;unplug",),
            daemon=True, name=f"evsim-plugin-dciso-{connector_id}").start()

    def plug_out(self, connector_id=1):
        drv = self._ev_drv(connector_id)
        if drv is None:
            return
        drv.unplug()

    def plug_out_iso(self, connector_id=1):
        drv = self._ev_drv(connector_id)
        if drv is None:
            return
        drv.stop_session()
        drv.unplug()

    def pause_session(self, connector_id=1):
        drv = self._ev_drv(connector_id)
        if drv is None:
            return
        drv.pause_session()

    def resume_session(self, connector_id=1):
        drv = self._ev_drv(connector_id)
        if drv is None:
            return
        drv.resume_session()

    def pause_iso_session(self, connector_id=1):
        drv = self._ev_drv(connector_id)
        if drv is None:
            return
        drv.pause_session()

    def resume_iso_session_ac(self, connector_id=1):
        drv = self._ev_drv(connector_id)
        if drv is None:
            return
        drv.resume_session()

    def resume_iso_session_dc(self, connector_id=1):
        drv = self._ev_drv(connector_id)
        if drv is None:
            return
        drv.resume_session()

    def swipe(self, token, connectors=None):
        connectors = connectors if connectors is not None else [1]
        provided_token = {
            "id_token": {
                "value": token,
                "type": "ISO14443"
            },
            "authorization_type": "RFID",
            "connectors": connectors
        }
        self._mqtt_client.publish(
            f"{self._mqtt_external_prefix}everest_api/dummy_token_provider/cmd/provide", json.dumps(provided_token))

    def connect_websocket(self):
        self._mqtt_client.publish(
            f"{self._mqtt_external_prefix}everest_api/ocpp/cmd/connect", "on")

    def disconnect_websocket(self):
        self._mqtt_client.publish(
            f"{self._mqtt_external_prefix}everest_api/ocpp/cmd/disconnect", "off")

    def diode_fail(self, connector_id=1):
        drv = self._ev_drv(connector_id)
        if drv is None:
            return
        drv.configure_session("AcIec", {"charging_current_a": 32.0, "three_phases": True})
        drv.plug()
        threading.Thread(
            target=drv.play_dsl,
            args=("sleep 1;diode_fail;sleep 36000;unplug",),
            daemon=True, name=f"evsim-diodefail-{connector_id}").start()

    def raise_error(self, error_string="MREC6UnderVoltage", connector_id=1):
        drv = self._yeti_drv(connector_id)
        if drv is None:
            return
        drv.raise_error({"type": _translate_legacy_error_type(error_string)})

    def clear_error(self, error_string="MREC6UnderVoltage", connector_id=1):
        drv = self._yeti_drv(connector_id)
        if drv is None:
            return
        drv.clear_error({"type": _translate_legacy_error_type(error_string)})

    def publish(self, topic, payload):
        self._mqtt_client.publish(topic, payload)

    def _destroy_mqtt_client(self):
        if self._mqtt_client:
            try:
                self._mqtt_client.loop_stop()
            except Exception:
                log.debug("loop_stop failed", exc_info=True)
            self._mqtt_client.disconnect()
            self._mqtt_client = None
