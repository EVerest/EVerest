import json
import logging
import threading

from everest.testing.core_utils.common import close_mqtt_client
from everest.testing.core_utils.everest_core import EverestCore
from everest.testing.core_utils.sim_registry import (
    EvControllerDriver,
    NoSimulatorError,
    SimRegistry,
    YetiSimDriver,
)

from everest.testing.core_utils.controller.test_controller_interface import TestController


log = logging.getLogger(__name__)

# Bound on establishing the MQTT session. Generous: this is a local broker,
# and the cost of being wrong is a hard failure, not a retry.
_BROKER_CONNECT_TIMEOUT_S = 10.0

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

    Raises ValueError on an unknown spelling. configure_session is
    fire-and-forget, so a value the EvController codec rejects is dropped without
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

    def _ev_drv(self, connector_id: int) -> EvControllerDriver:
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
        # Single funnel for every EV-driving helper below, so the readiness
        # gate is paid exactly once per boot and only by tests that actually
        # drive the EV. Raises SimNotReadyError naming the instance rather
        # than publishing a command that would be silently dropped.
        registry.require_ready()
        return d

    def _yeti_drv(self, connector_id: int) -> YetiSimDriver:
        d = self._require_registry().yeti_by_connector(connector_id)
        if d is None:
            raise NoSimulatorError(self._no_instance("YetiSimulator", connector_id, []))
        return d

    def _no_instance(self, module: str, connector_id: int, known: list) -> str:
        # Naming what was found is what tells a wrong connector_id apart from a
        # config with no simulator in it at all.
        found = f" {module} instances found: {', '.join(known)}." if known else ""
        return (
            f"no {module} instance for connector_id={connector_id} in "
            f"{self._everest_core.everest_config_path}. This controller drives "
            f"EvController and YetiSimulator instances from the config.{found}"
        )

    # ---- public API (matches the legacy EvManager-based controller) -------
    def _world_drv(self, connector_id):
        driver = self._require_registry().ev_driver_by_connector(connector_id)
        if driver is None:
            raise NoSimulatorError(f"connector {connector_id} requires EvDriverSim; HIL uses its physical pilot")
        return driver

    def plug_in(self, connector_id=1):
        drv = self._ev_drv(connector_id)
        world = self._world_drv(connector_id)
        drv.configure_session("AcIec", {"charging_current_a": 32.0, "three_phases": False})
        world.plug()

    def plug_in_ac_iso(self, connector_id=1, payment_type=""):
        drv = self._ev_drv(connector_id)
        world = self._world_drv(connector_id)
        params: dict = {"charging_current_a": 16.0, "three_phases": True,
                        "departure_time_s": 86400, "e_amount_wh": 0}
        if payment_type:
            params["payment"] = _normalize_payment_option(payment_type)
        drv.configure_session("AcIso2", params)
        world.plug()

    def plug_in_dc_iso(self, connector_id=1, payment_type=""):
        drv = self._ev_drv(connector_id)
        world = self._world_drv(connector_id)
        params: dict = {"departure_time_s": 86400, "e_amount_wh": 0}
        if payment_type:
            params["payment"] = _normalize_payment_option(payment_type)
        drv.configure_session("DcIso2", params)
        world.plug()

    def plug_out(self, connector_id=1):
        self._ev_drv(connector_id)
        world = self._world_drv(connector_id)
        world.unplug()

    def plug_out_iso(self, connector_id=1):
        drv = self._ev_drv(connector_id)
        world = self._world_drv(connector_id)
        drv.stop_session()
        world.unplug()

    def run_scenario(self, name, connector_id=1, timing=None):
        self._ev_drv(connector_id).run_scenario(name, timing=timing)

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
        drv.subscribe_var("state")
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
        drv = self._yeti_drv(connector_id)
        drv.raise_error({"type": _translate_legacy_error_type(error_string)})

    def clear_error(self, error_string="MREC6UnderVoltage", connector_id=1):
        drv = self._yeti_drv(connector_id)
        drv.clear_error({"type": _translate_legacy_error_type(error_string)})

    def publish(self, topic, payload):
        self._mqtt_client.publish(topic, payload)

    def _destroy_mqtt_client(self):
        if self._mqtt_client:
            close_mqtt_client(self._mqtt_client)
            self._mqtt_client = None
