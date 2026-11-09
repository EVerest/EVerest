import json
import logging
import threading

from everest.testing.core_utils.common import close_mqtt_client
from everest.testing.core_utils.everest_core import EverestCore
from everest.testing.core_utils.sim_registry import (
    EvControllerDriver,
    NoSimulatorError,
    SimRegistry,
)

from everest.testing.core_utils.controller.test_controller_interface import TestController


log = logging.getLogger(__name__)

# Bound on establishing the MQTT session. Generous: this is a local broker,
# and the cost of being wrong is a hard failure, not a retry.
_BROKER_CONNECT_TIMEOUT_S = 10.0

# EvController's PaymentOption codec accepts only the PascalCase enum spellings
# ("ExternalPayment", "Contract"); the legacy EvManager DSL accepted lowercase
# ("externalpayment", "contract"). Tests still pass the legacy spelling, so
# normalize here (case-insensitively) before it reaches configure_session.
# Source of truth: lib/everest/everest_api_types/.../ev_controller/json_codec.cpp.
_PAYMENT_OPTION_MAP: dict = {
    "contract": "Contract",
    "externalpayment": "ExternalPayment",
    "external_payment": "ExternalPayment",
    "eim": "ExternalPayment",
}


def _normalize_payment_option(payment_type: str) -> str:
    """Map a legacy/lowercase payment name to the EvController PaymentOption enum.

    Raises ValueError on an unknown spelling. A value the EvController codec
    rejects never reaches the session, which then falls back to EIM; failing
    here names the misspelling at the call site instead.
    """
    key = payment_type.strip().lower()
    try:
        return _PAYMENT_OPTION_MAP[key]
    except KeyError as e:
        raise ValueError(
            f"unknown payment_type {payment_type!r}; expected one of "
            f"{sorted(set(_PAYMENT_OPTION_MAP.values()))} (case-insensitive)"
        ) from e


# EvManager's dc_discharge_* config defaults, which its dc_bpt session sent.
_EV_MANAGER_BPT = {
    "discharge_max_current_limit": 300.0,
    "discharge_max_power_limit": 150000.0,
    "discharge_target_current": 5.0,
    "discharge_minimal_soc": 20.0,
}


def _dc_session_for_energy_mode(energy_mode: str, params: dict, d20: bool) -> tuple:
    """Map an EvManager iso_start_v2g_session energy mode to an EvController session.

    d20 picks the ISO 15118-20 mode for DC, for an EV that lists -20 first.
    """
    key = energy_mode.strip().lower()
    if key == "dc":
        return ("DcIsoD20" if d20 else "DcIso2"), params
    if key == "dc_bpt":
        return "DcIsoD20", {**params, "bpt": dict(_EV_MANAGER_BPT)}
    raise ValueError(f"unknown energy_mode {energy_mode!r}; expected DC or dc_bpt (case-insensitive)")


class EverestTestController(TestController):

    def __init__(self,
                 everest_core: EverestCore
                 ):
        self._everest_core = everest_core
        self._mqtt_client = None
        self._registry: SimRegistry | None = None
        self._fault_stop = threading.Event()
        self._fault_threads: list[threading.Thread] = []

    @property
    def _mqtt_external_prefix(self):
        return self._everest_core.mqtt_external_prefix

    def start(self, broker_connect_timeout: float = _BROKER_CONNECT_TIMEOUT_S):
        self._fault_stop.clear()
        self._registry = SimRegistry.connect(self._everest_core, broker_connect_timeout)
        self._mqtt_client = self._registry.client
        self._everest_core.start()
        self._registry.autostart()

    def stop(self, *exc_details):
        self._fault_stop.set()
        for thread in self._fault_threads:
            thread.join()
        self._fault_threads.clear()
        self._everest_core.stop()
        self._destroy_mqtt_client()

    # ---- internal: connector_id -> driver --------------------------------
    def _require_registry(self) -> SimRegistry:
        if self._registry is None:
            raise NoSimulatorError(
                "test controller was not started, so no simulator has been "
                "discovered; call start() before driving the EV"
            )
        return self._registry

    def _car(self, connector_id: int) -> EvControllerDriver:
        """The EvController driver for `connector_id`; raises if there is none.

        Returning None here let every public helper below no-op, so a test that
        asked for a car to be plugged in carried on against a charge point with
        no car on it and failed much later as a protocol bug. A controller that
        cannot reach its simulator fails the test instead.
        """
        registry = self._require_registry()
        d = registry.ev_by_connector(connector_id)
        if d is None:
            raise NoSimulatorError(self._no_instance(
                "EvController", connector_id, sorted(registry.drivers)))
        return d

    def _ev_drv(self, connector_id: int) -> EvControllerDriver:
        """`_car`, once the simulators can receive commands."""
        d = self._car(connector_id)
        # Single funnel for every EV-driving helper below, so the readiness
        # gate is paid exactly once per boot and only by tests that actually
        # drive the EV. Raises SimNotReadyError naming the instance rather
        # than publishing a command that would be silently dropped.
        self._require_registry().require_ready()
        return d

    def _no_instance(self, module: str, connector_id: int, known: list) -> str:
        # Naming what was found is what tells a wrong connector_id apart from a
        # config with no simulator in it at all.
        found = f" {module} instances found: {', '.join(known)}." if known else ""
        return (
            f"no {module} instance for connector_id={connector_id} in "
            f"{self._everest_core.everest_config_path}. This controller drives "
            f"EvController instances from the config.{found}"
        )

    # ---- public API (matches the legacy EvManager-based controller) -------
    def _world_drv(self, connector_id):
        registry = self._require_registry()
        driver = registry.ev_driver_by_connector(connector_id)
        if driver is None:
            raise NoSimulatorError(f"connector {connector_id} requires EvDriverSim; HIL uses its physical pilot")
        registry.require_ready()
        return driver

    def plug_in(self, connector_id=1):
        drv = self._ev_drv(connector_id)
        world = self._world_drv(connector_id)
        drv.configure_session("AcIec", {"charging_current_a": 32.0, "three_phases": False})
        world.plug()

    def plug_in_ac_iso(self, connector_id=1, payment_type=""):
        """Plug in using AC ISO15118.

        Raises ValueError if payment_type is missing, empty, or unsupported.
        """
        drv = self._ev_drv(connector_id)
        world = self._world_drv(connector_id)
        params: dict = {"charging_current_a": 16.0, "three_phases": True,
                        "departure_time_s": 86400, "e_amount_wh": 0}
        params["payment"] = _normalize_payment_option(payment_type)
        drv.configure_session("AcIsoD20" if drv.prefers_iso15118_20 else "AcIso2", params)
        world.plug()

    def plug_in_dc_iso(self, connector_id=1, payment_type="", energy_mode="DC"):
        """Plug in using DC ISO15118.

        Raises ValueError if payment_type is missing, empty, or unsupported.
        """
        drv = self._ev_drv(connector_id)
        world = self._world_drv(connector_id)
        params: dict = {"departure_time_s": 86400, "e_amount_wh": 0}
        params["payment"] = _normalize_payment_option(payment_type)
        drv.configure_session(*_dc_session_for_energy_mode(energy_mode, params, drv.prefers_iso15118_20))
        world.plug()

    def plug_out(self, connector_id=1, abrupt=False):
        """Unplug. Ends an active session cleanly first; abrupt=True pulls the plug mid-session."""
        if abrupt:
            self._world_drv(connector_id).unplug()
            return
        drv = self._ev_drv(connector_id)
        world = self._world_drv(connector_id)
        states = drv.payloads("state")
        if states and states[-1] not in ("Unplugged", "Disabled"):
            self._stop_session(drv)
        world.unplug()

    def plug_out_iso(self, connector_id=1):
        drv = self._ev_drv(connector_id)
        world = self._world_drv(connector_id)
        self._stop_session(drv)
        world.unplug()

    def _stop_session(self, drv: EvControllerDriver):
        after = drv.payload_count("state")
        drv.stop_session()
        drv.query_state()
        drv.wait_for("state", lambda s: s not in ("Charging", "Stopping"), timeout=30.0, after=after)

    def pause_session(self, connector_id=1):
        drv = self._ev_drv(connector_id)
        drv.pause_session()

    def resume_session(self, connector_id=1):
        drv = self._ev_drv(connector_id)
        drv.resume_session()

    def pause_iso_session(self, connector_id=1):
        drv = self._ev_drv(connector_id)
        drv.pause_session()

    def resume_iso_session_ac(self, connector_id=1):
        drv = self._ev_drv(connector_id)
        drv.resume_session()

    def resume_iso_session_dc(self, connector_id=1):
        drv = self._ev_drv(connector_id)
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
        # Inject under load, after authorization has allowed Charging.
        drv = self._ev_drv(connector_id)
        world = self._world_drv(connector_id)
        after = drv.payload_count("state")
        drv.configure_session("AcIec", {"charging_current_a": 32.0, "three_phases": True})
        world.plug()

        def inject_when_charging():
            while not self._fault_stop.is_set():
                try:
                    drv.wait_for("state", lambda state: state == "Charging", timeout=0.1, after=after)
                    break
                except AssertionError:
                    continue
            if not self._fault_stop.wait(5.0):
                world.inject_fault("DiodeFail")

        thread = threading.Thread(target=inject_when_charging, daemon=True)
        self._fault_threads.append(thread)
        thread.start()

    def raise_error(self, error_string="MREC6UnderVoltage", connector_id=1):
        raise_error_payload = {
            "error_type": error_string,
            "raise": "true"
        }

        self._mqtt_client.publish(
            f"{self._mqtt_external_prefix}everest_external/nodered/{connector_id}/carsim/error",
            json.dumps(raise_error_payload))

    def clear_error(self, error_string="MREC6UnderVoltage", connector_id=1):
        clear_error_payload = {
            "error_type": error_string,
            "raise": "false"
        }

        self._mqtt_client.publish(
            f"{self._mqtt_external_prefix}everest_external/nodered/{connector_id}/carsim/error",
            json.dumps(clear_error_payload))

    def publish(self, topic, payload):
        self._mqtt_client.publish(topic, payload)

    def _destroy_mqtt_client(self):
        if self._mqtt_client:
            close_mqtt_client(self._mqtt_client)
            self._mqtt_client = None
