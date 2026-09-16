import json
import logging
import os
import re
import threading
import time
import paho.mqtt.client as mqtt
from paho.mqtt import __version__ as paho_mqtt_version

from everest.testing.core_utils.everest_core import EverestCore

from everest.testing.core_utils.controller.test_controller_interface import TestController

# Modules providing the car_simulator interface. Their "enabled" var is the
# handshake that says the simulator is subscribed and accepting commands.
CAR_SIMULATOR_MODULES = ("EvManager",)

CAR_SIMULATOR_ENABLED_TIMEOUT_S = 30.0

# Matches "<everest prefix>modules/<module id>/impl/<impl id>/var/enabled".
_ENABLED_TOPIC_PATTERN = re.compile(r"modules/([^/]+)/impl/[^/]+/var/enabled$")


class EverestTestController(TestController):

    def __init__(self,
                 everest_core: EverestCore
                 ):
        self._everest_core = everest_core
        self._mqtt_client = None
        self._enabled_car_simulators = set()
        self._enabled_lock = threading.Lock()

    @property
    def _mqtt_external_prefix(self):
        return self._everest_core.mqtt_external_prefix

    @property
    def _mqtt_everest_prefix(self):
        """Internal EVerest topic prefix, read back from the config actually written."""
        prefix = self._everest_core.everest_config.get("settings", {}).get("mqtt_everest_prefix", "everest")
        return prefix if prefix.endswith("/") else f"{prefix}/"

    def start(self):
        self._initialize_external_mqtt_client()
        self._everest_core.start()
        self._initialize_nodered_sil()

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
        self._mqtt_client.on_message = self._on_mqtt_message
        self._mqtt_client.connect(mqtt_server_uri, mqtt_server_port)
        # A network loop is required to receive the car simulator "enabled" handshake.
        self._mqtt_client.loop_start()

    def _on_mqtt_message(self, client, userdata, message):
        match = _ENABLED_TOPIC_PATTERN.search(message.topic)
        if match is None:
            return
        try:
            payload = json.loads(message.payload.decode())
        except (UnicodeDecodeError, json.JSONDecodeError):
            return
        # Var payloads are {"msg_type": "var", "data": {"data": <value>}}.
        value = payload.get("data")
        if isinstance(value, dict):
            value = value.get("data")
        if value is True:
            with self._enabled_lock:
                self._enabled_car_simulators.add(match.group(1))

    def _car_simulator_module_ids(self):
        active_modules = self._everest_core.everest_config.get("active_modules", {})
        return {module_id for module_id, module in active_modules.items()
                if module.get("module") in CAR_SIMULATOR_MODULES}

    def _publish_enable(self):
        self._mqtt_client.publish(
            f"{self._mqtt_external_prefix}everest_external/nodered/1/carsim/cmd/enable", "true")
        self._mqtt_client.publish(
            f"{self._mqtt_external_prefix}everest_external/nodered/2/carsim/cmd/enable", "true")

    def _initialize_nodered_sil(self):
        with self._enabled_lock:
            self._enabled_car_simulators.clear()
        # Subscribe before the first enable so no confirmation can be missed.
        self._mqtt_client.subscribe(f"{self._mqtt_everest_prefix}modules/+/impl/+/var/enabled")
        self._publish_enable()

    def wait_for_car_simulators_enabled(self, timeout: float = CAR_SIMULATOR_ENABLED_TIMEOUT_S):
        """Block until every car simulator in the config reports enabled == true.

        The simulator subscribes to its external command topics and applies
        auto_enable in ready(), so an enable published before that is dropped and
        an execute_charging_session published before that is rejected with
        "Simulation disabled". A module that reaches ready() quickly loses the
        race that a slow one hides, so the plug-in must gate on this handshake
        rather than on any other module's readiness.

        Because ready() only runs once every module (including a probe module)
        has signalled init done, this cannot be folded into start(): waiting
        there would deadlock a test whose probe has not started yet.
        """
        expected = self._car_simulator_module_ids()
        if not expected:
            return
        started = time.time()
        deadline = started + timeout
        while True:
            with self._enabled_lock:
                missing = expected - self._enabled_car_simulators
            if not missing:
                logging.info(
                    f"Car simulators enabled after {time.time() - started:.3f}s: {sorted(expected)}")
                return
            if time.time() >= deadline:
                raise TimeoutError(
                    f"Timeout waiting for car simulator(s) {sorted(missing)} to report enabled")
            # Re-publish: with auto_enable disabled the first enable is dropped if
            # it lands before the simulator subscribes.
            self._publish_enable()
            # Polls the handshake set, switch to a threading.Event if the
            # poll interval shows up in suite runtime.
            time.sleep(0.05)

    def plug_in(self, connector_id=1):
        self._mqtt_client.publish(
            f"{self._mqtt_external_prefix}everest_external/nodered/{connector_id}/carsim/cmd/execute_charging_session",
            "sleep 1;iec_wait_pwr_ready;sleep 1;draw_power_regulated 32,1;sleep 200;unplug")

    def plug_in_ac_iso(self, connector_id=1, payment_type=""):
        self._mqtt_client.publish(
            f"{self._mqtt_external_prefix}everest_external/nodered/{connector_id}/carsim/cmd/execute_charging_session",
            f"sleep 1;iso_wait_slac_matched;iso_start_v2g_session AC {payment_type} 86400 0;iso_wait_pwr_ready;iso_draw_power_regulated 16,3;sleep 60;iso_stop_charging;iso_wait_v2g_session_stopped;unplug")

    def plug_in_dc_iso(self, connector_id=1, payment_type=""):
        self._mqtt_client.publish(
            f"{self._mqtt_external_prefix}everest_external/nodered/{connector_id}/carsim/cmd/execute_charging_session",
            f"sleep 1;iso_wait_slac_matched;iso_start_v2g_session DC {payment_type} 86400 0;iso_wait_pwr_ready;iso_wait_for_stop 60;iso_wait_v2g_session_stopped;unplug"
        )

    def plug_out(self, connector_id=1):
        self._mqtt_client.publish(
            f"{self._mqtt_external_prefix}everest_external/nodered/{connector_id}/carsim/cmd/modify_charging_session",
            "unplug")

    def plug_out_iso(self, connector_id=1):
        self._mqtt_client.publish(
            f"{self._mqtt_external_prefix}everest_external/nodered/{connector_id}/carsim/cmd/modify_charging_session",
            "iso_stop_charging;iso_wait_v2g_session_stopped;unplug")

    def pause_session(self, connector_id=1):
        self._mqtt_client.publish(
            f"{self._mqtt_external_prefix}everest_external/nodered/{connector_id}/carsim/cmd/modify_charging_session",
            "pause;sleep 36000"
        )

    def resume_session(self, connector_id=1):
        self._mqtt_client.publish(
            f"{self._mqtt_external_prefix}everest_external/nodered/{connector_id}/carsim/cmd/modify_charging_session",
            "draw_power_regulated 16,3"
        )

    def pause_iso_session(self, connector_id=1):
        self._mqtt_client.publish(
            f"{self._mqtt_external_prefix}everest_external/nodered/{connector_id}/carsim/cmd/modify_charging_session",
            "iso_pause_charging;iso_wait_for_resume"
        )

    def resume_iso_session_ac(self, connector_id=1):
        self._mqtt_client.publish(
            f"{self._mqtt_external_prefix}everest_external/nodered/{connector_id}/carsim/cmd/modify_charging_session",
            "iso_start_bcb_toggle 3;iso_wait_pwm_is_running;iso_start_v2g_session AC 86400 0;iso_wait_pwr_ready;iso_draw_power_regulated 16,3;iso_wait_for_stop 60"
        )

    def resume_iso_session_dc(self, connector_id=1):
        self._mqtt_client.publish(
            f"{self._mqtt_external_prefix}everest_external/nodered/{connector_id}/carsim/cmd/modify_charging_session",
            "iso_start_bcb_toggle 3;iso_wait_pwm_is_running;iso_start_v2g_session DC 86400 0;iso_wait_pwr_ready;iso_dc_power_on;iso_wait_for_stop 60"
        )

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
        self._mqtt_client.publish(
            f"{self._mqtt_external_prefix}everest_external/nodered/{connector_id}/carsim/cmd/execute_charging_session",
            "sleep 1;iec_wait_pwr_ready;sleep 1;draw_power_regulated 32,3;sleep 5;diode_fail;sleep 36000;unplug")

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
            self._mqtt_client.loop_stop()
            self._mqtt_client.disconnect()
            self._mqtt_client = None
