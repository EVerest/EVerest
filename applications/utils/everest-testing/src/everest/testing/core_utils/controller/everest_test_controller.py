import json
import logging
import os
import threading
from pathlib import Path
from typing import Optional

import paho.mqtt.client as mqtt
from paho.mqtt import __version__ as paho_mqtt_version

from everest.testing.core_utils.everest_core import EverestCore
from everest.testing.core_utils.sim_registry import EvSimDriver, SimRegistry, YetiSimDriver

from everest.testing.core_utils.controller.test_controller_interface import TestController


log = logging.getLogger(__name__)

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

    @property
    def _mqtt_external_prefix(self):
        return self._everest_core.mqtt_external_prefix

    def start(self):
        self._initialize_external_mqtt_client()
        # Start the mqtt loop early so registry subscriptions registered
        # before/while EvSimulator's heartbeat starts firing are delivered.
        self._mqtt_client.loop_start()
        self._everest_core.start()
        # Build simulator driver registry from the resolved config so
        # plug_in/plug_out helpers below dispatch to typed m2e topics
        # instead of the legacy `everest_external/nodered/...` surface.
        self._registry = SimRegistry(
            mqtt_client=self._mqtt_client,
            mqtt_external_prefix=self._mqtt_external_prefix,
            config_path=Path(self._everest_core.everest_config_path),
        )
        # Enable the simulators so the explicit plug_in_* helpers below can
        # drive sessions. Scenario replay stays off (default): the legacy
        # sidecar DSL would race and corrupt the test-driven session.
        self._registry.autostart()

    def stop(self, *exc_details):
        self._everest_core.stop()
        self._destroy_mqtt_client()

    def _initialize_external_mqtt_client(self):
        mqtt_server_uri = os.environ.get("MQTT_SERVER_ADDRESS", "127.0.0.1")
        mqtt_server_port = int(os.environ.get("MQTT_SERVER_PORT", "1883"))
        if paho_mqtt_version < '2.0':
            self._mqtt_client = mqtt.Client(self._everest_core.everest_uuid)
        else:
            self._mqtt_client = mqtt.Client(
                callback_api_version=mqtt.CallbackAPIVersion.VERSION2, client_id=self._everest_core.everest_uuid)
        self._mqtt_client.connect(mqtt_server_uri, mqtt_server_port)

    # ---- internal: connector_id -> driver --------------------------------
    def _ev_drv(self, connector_id: int) -> Optional[EvSimDriver]:
        if self._registry is None:
            return None
        d = self._registry.ev_by_connector(connector_id)
        if d is None:
            log.warning("ev_sim: no EvSimulator instance for connector_id=%s", connector_id)
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
