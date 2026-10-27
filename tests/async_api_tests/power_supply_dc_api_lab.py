# SPDX-License-Identifier: Apache-2.0
# Copyright Pionix GmbH and Contributors to EVerest
"""Test bench for power_supply_DC_API: an external DC power supply speaking the
AsyncAPI topics, a recorder of every MQTT message of the EVerest instance under
test, and the config adaption that puts the API module into the DC SIL.
"""
import json
import os
import struct
import threading
import time
from copy import deepcopy
from typing import Dict, Optional

import paho.mqtt.client as mqtt
from everest.testing.core_utils import EverestConfigAdjustmentStrategy
from everest.testing.core_utils.everest_core import EverestCore

MODULE_ID = 'powersupply_dc'
API = f'everest_api/1/power_supply_DC/{MODULE_ID}'
E2M = API + '/e2m/'
M2E = API + '/m2e/'
MOD_VAR = f'modules/{MODULE_ID}/impl/main/var/'
MOD_ERR = f'modules/{MODULE_ID}/impl/main/error/power_supply_DC/'
EVSE_INOP = 'modules/evse_manager/impl/evse/error/evse_manager/Inoperative'
EVSE_ERR = 'modules/evse_manager/impl/evse/error/evse_manager/'
SESSION_EVENT = 'modules/evse_manager/impl/evse/var/session_event'
BSP_CMD = 'modules/yeti_driver/impl/board_support/cmd/'
ISO_VAR = 'modules/iso15118_charger/impl/charger/var/'
ISO_CMD = 'modules/iso15118_charger/impl/charger/cmd/'

ERROR_TYPES = ['CommunicationFault', 'HardwareFault', 'OverTemperature', 'UnderTemperature', 'UnderVoltageAC',
               'OverVoltageAC', 'UnderVoltageDC', 'OverVoltageDC', 'OverCurrentAC', 'OverCurrentDC', 'VendorError',
               'VendorWarning']

DEFAULT_CAPABILITIES = {
    'bidirectional': True,
    'current_regulation_tolerance_A': 2.0,
    'peak_current_ripple_A': 2.0,
    'max_export_voltage_V': 900.0,
    'min_export_voltage_V': 200.0,
    'max_export_current_A': 200.0,
    'min_export_current_A': 1.0,
    'max_export_power_W': 150000.0,
    'max_import_voltage_V': 900.0,
    'min_import_voltage_V': 200.0,
    'max_import_current_A': 200.0,
    'min_import_current_A': 1.0,
    'max_import_power_W': 150000.0,
    'conversion_efficiency_import': 0.85,
    'conversion_efficiency_export': 0.9,
}

REQUIRED_CAPABILITY_KEYS = ('bidirectional', 'max_export_voltage_V', 'min_export_voltage_V', 'max_export_current_A',
                            'min_export_current_A', 'max_export_power_W', 'current_regulation_tolerance_A',
                            'peak_current_ripple_A')


def as_float32(value: float) -> float:
    """The API carries every number as a 32-bit float."""
    return struct.unpack('f', struct.pack('f', value))[0]


class PowerSupplyApiStrategy(EverestConfigAdjustmentStrategy):
    """config-sil-dc.yaml with the simulated supply replaced by power_supply_DC_API.

    The simulator's car-side power meter disappears with it. With din=False both
    sides are pinned to ISO 15118-2, so the EV performs welding detection, and
    the supply becomes the welding-detection voltage source.
    """

    def __init__(self, din: bool = True, communication_check_s: int = 5, heartbeat_ms: int = 1000):
        self.din = din
        self.communication_check_s = communication_check_s
        self.heartbeat_ms = heartbeat_ms

    def adjust_everest_configuration(self, everest_config: Dict):
        config = deepcopy(everest_config)
        modules = config['active_modules']
        modules[MODULE_ID] = {
            'module': 'power_supply_DC_API',
            'config_module': {'cfg_communication_check_to_s': self.communication_check_s,
                              'cfg_heartbeat_interval_ms': self.heartbeat_ms},
        }
        modules['evse_manager']['connections'].pop('powermeter_car_side', None)
        if not self.din:
            modules['iso15118_charger']['config_module']['supported_DIN70121'] = False
            modules['iso15118_car']['config_module']['supported_DIN70121'] = False
            modules['evse_manager']['config_module']['welding_detection_voltage_source'] = 'power_supply'
        return config


class Record:
    __slots__ = ('t', 'topic', 'payload')

    def __init__(self, t: float, topic: str, payload: str):
        self.t, self.topic, self.payload = t, topic, payload

    def json(self):
        try:
            return json.loads(self.payload)
        except ValueError:
            return None

    def value(self):
        """Strip the framework envelope of internal var, cmd and error messages."""
        data = self.json()
        if isinstance(data, dict) and isinstance(data.get('data'), dict):
            inner = data['data']
            if 'data' in inner:
                return inner['data']
            if 'args' in inner:
                return inner['args']
            return inner
        return data

    def __repr__(self):
        return f'{self.t:8.3f} {self.topic} {self.payload[:120]}'


class Recorder:
    """Records every message of one EVerest instance, with the instance prefixes removed.

    External topics come back exactly as the API spec names them (everest_api/...,
    everest_external/...), internal ones as modules/<id>/impl/<impl>/{var,cmd,error}/...
    """

    def __init__(self, everest_core: EverestCore):
        self.internal_prefix = f'everest_{everest_core.everest_uuid}/'
        self.external_prefix = everest_core.mqtt_external_prefix
        self.records = []
        self.listeners = []
        self.cv = threading.Condition()
        self.t0 = time.monotonic()
        self.client = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2, client_id=f'lab-{everest_core.everest_uuid}')
        self.client.on_message = self._on_message
        self.client.on_connect = self._on_connect
        self._subscribed = threading.Event()

    def start(self):
        self.client.connect(os.environ.get('MQTT_SERVER_ADDRESS', '127.0.0.1'),
                            int(os.environ.get('MQTT_SERVER_PORT', '1883')))
        self.client.loop_start()
        assert self._subscribed.wait(5.0), 'recorder did not subscribe within 5 s'
        return self

    def stop(self):
        self.client.loop_stop()
        self.client.disconnect()

    def now(self) -> float:
        return time.monotonic() - self.t0

    def _on_connect(self, client, userdata, flags, reason_code, properties):
        # the external prefix has no trailing slash, so '#' alone is not a valid filter after it
        client.subscribe([(self.internal_prefix + '#', 0)] +
                         [(self.external_prefix + root + '/#', 0) for root in ('everest_api', 'everest_external', 'test')])
        self._subscribed.set()

    def _on_message(self, client, userdata, msg):
        topic = msg.topic
        if topic.startswith(self.internal_prefix):
            topic = topic[len(self.internal_prefix):]
        elif topic.startswith(self.external_prefix):
            topic = topic[len(self.external_prefix):]
        record = Record(self.now(), topic, msg.payload.decode('utf-8', 'replace'))
        with self.cv:
            self.records.append(record)
            self.cv.notify_all()
        for listener in list(self.listeners):
            listener(record)

    def publish(self, topic: str, payload):
        """Publish on an external topic; non-string payloads are JSON encoded."""
        if not isinstance(payload, str):
            payload = json.dumps(payload)
        self.client.publish(self.external_prefix + topic, payload)

    def _matches(self, r: Record, topic, prefix, pred, since):
        return (r.t >= since and (topic is None or r.topic == topic)
                and (prefix is None or r.topic.startswith(prefix)) and (pred is None or pred(r)))

    def find(self, topic=None, since=0.0, pred=None, prefix=None):
        with self.cv:
            return [r for r in self.records if self._matches(r, topic, prefix, pred, since)]

    def wait_for(self, topic=None, timeout=10.0, since=None, pred=None, prefix=None) -> Optional[Record]:
        """First matching record at or after `since` (default: now), or None after `timeout`."""
        if since is None:
            since = self.now()
        deadline = time.monotonic() + timeout
        with self.cv:
            while True:
                for r in self.records:
                    if self._matches(r, topic, prefix, pred, since):
                        return r
                remaining = deadline - time.monotonic()
                if remaining <= 0:
                    return None
                self.cv.wait(remaining)

    def absent(self, topic=None, timeout=3.0, since=None, pred=None, prefix=None) -> bool:
        return self.wait_for(topic, timeout, since, pred, prefix) is None


class FakePowerSupply:
    """A DC power supply behind power_supply_DC_API.

    It follows the mode and setpoint requests it receives, publishes its output
    at `rate_hz` and answers the communication check every `comm_check_s`
    seconds. Each of these can be switched per test:
      follow          output tracks the setpoint while the mode is Export or Import
      rate_hz         measurement rate; 0 stops the periodic publisher
      on_change_only  publish a measurement only when the output changed
      comm_check_s    period of the automatic communication_check=true; 0 stops it
    """

    def __init__(self, recorder: Recorder, capabilities: Optional[Dict] = None):
        self.rec = recorder
        self.capabilities = dict(capabilities or DEFAULT_CAPABILITIES)
        self.follow = True
        self.rate_hz = 4.0
        self.on_change_only = False
        self.comm_check_s = 2.0
        self.mode = 'Off'
        self.phase = 'Other'
        self.export_setpoint = (0.0, 0.0)
        self.import_setpoint = (0.0, 0.0)
        self.output = (0.0, 0.0)
        self._lock = threading.Lock()
        self._stop = threading.Event()
        self._last_published = None
        self._thread = threading.Thread(target=self._loop, daemon=True)
        recorder.listeners.append(self._on_record)

    def start(self):
        self._thread.start()
        return self

    def stop(self):
        self._stop.set()
        if self._thread.is_alive():
            self._thread.join(2.0)

    def _on_record(self, record: Record):
        if not record.topic.startswith(E2M):
            return
        kind = record.topic[len(E2M):]
        data = record.json()
        if not isinstance(data, dict):
            return
        with self._lock:
            if kind == 'mode':
                self.mode = data.get('mode')
                self.phase = data.get('charging_phase')
                if self.mode in ('Off', 'Export', 'Import', 'Fault'):
                    self.rec.publish(M2E + 'mode', json.dumps(self.mode))
            elif kind == 'export_voltage_current':
                self.export_setpoint = (float(data['voltage_V']), float(data['current_A']))
            elif kind == 'import_voltage_current':
                self.import_setpoint = (float(data['voltage_V']), float(data['current_A']))

    def publish_capabilities(self, capabilities: Optional[Dict] = None):
        self.rec.publish(M2E + 'capabilities', capabilities if capabilities is not None else self.capabilities)

    def publish_measurement(self, voltage: float, current: float):
        self.rec.publish(M2E + 'voltage_current', {'voltage_V': voltage, 'current_A': current})

    def communication_check(self, value: bool = True):
        self.rec.publish(M2E + 'communication_check', 'true' if value else 'false')

    def raise_error(self, type_: str, sub_type: Optional[str] = None, message: Optional[str] = None):
        error = {'type': type_}
        if sub_type is not None:
            error['sub_type'] = sub_type
        if message is not None:
            error['message'] = message
        self.rec.publish(M2E + 'raise_error', error)

    def clear_error(self, type_: str, sub_type: Optional[str] = None):
        error = {'type': type_}
        if sub_type is not None:
            error['sub_type'] = sub_type
        self.rec.publish(M2E + 'clear_error', error)

    def _target(self):
        if not self.follow:
            return self.output
        if self.mode == 'Export':
            voltage, current = self.export_setpoint
            if current < self.capabilities['min_export_current_A']:
                current = 0.0
            return (voltage, current)
        if self.mode == 'Import':
            voltage, current = self.import_setpoint
            if current < self.capabilities.get('min_import_current_A', 0.0):
                current = 0.0
            return (voltage, -current)
        return (0.0, 0.0)

    def _loop(self):
        last_check = 0.0
        last_measurement = 0.0
        while not self._stop.is_set():
            now = time.monotonic()
            with self._lock:
                self.output = self._target()
                output = self.output
            if self.rate_hz > 0 and now - last_measurement >= 1.0 / self.rate_hz:
                last_measurement = now
                if not (self.on_change_only and output == self._last_published):
                    self.publish_measurement(*output)
                    self._last_published = output
            if self.comm_check_s > 0 and now - last_check >= self.comm_check_s:
                last_check = now
                self.communication_check(True)
            time.sleep(0.05)


class Carsim:
    def __init__(self, recorder: Recorder, connector_id: int = 1):
        self.rec = recorder
        self.base = f'everest_external/nodered/{connector_id}/carsim/cmd/'

    def dc_iso_session(self, hold_s: int = 12):
        self.rec.publish(self.base + 'execute_charging_session',
                         f'sleep 1;iso_wait_slac_matched;iso_start_v2g_session DC  86400 0;iso_wait_pwr_ready;'
                         f'iso_wait_for_stop {hold_s};iso_wait_v2g_session_stopped;unplug')


class Lab:
    """Recorder, fake supply and carsim around a started EVerest."""

    def __init__(self, everest_core: EverestCore, capabilities: Optional[Dict] = None):
        self.rec = Recorder(everest_core).start()
        self.supply = FakePowerSupply(self.rec, capabilities)
        self.carsim = Carsim(self.rec)

    def start_supply(self):
        self.supply.start()
        return self

    def stop(self):
        self.supply.stop()
        self.rec.stop()

    # --- waits ------------------------------------------------------------
    def wait_heartbeat(self, timeout=30.0) -> Record:
        heartbeat = self.rec.wait_for(E2M + 'heartbeat', timeout=timeout, since=0)
        assert heartbeat is not None, 'power_supply_DC_API published no heartbeat'
        return heartbeat

    def wait_event(self, name: str, since: float, timeout: float = 60.0) -> Optional[Record]:
        return self.rec.wait_for(SESSION_EVENT, timeout=timeout, since=since,
                                 pred=lambda r: (r.value() or {}).get('event') == name)

    def wait_error(self, type_: str, state: str, since: float, timeout: float = 8.0,
                   topic: Optional[str] = None) -> Optional[Record]:
        return self.rec.wait_for(topic or MOD_ERR + type_, timeout=timeout, since=since,
                                 pred=lambda r: (r.value() or {}).get('state') == state)

    def wait_inoperative(self, state: str, since: float, timeout: float = 3.0) -> Optional[Record]:
        return self.wait_error('', state, since, timeout, topic=EVSE_INOP)

    def wait_e2m(self, kind: str, since: float, timeout: float = 30.0, pred=None) -> Optional[Record]:
        return self.rec.wait_for(E2M + kind, timeout=timeout, since=since, pred=pred)

    def wait_mode(self, mode: str, since: float, timeout: float = 30.0, phase: Optional[str] = None):
        return self.wait_e2m('mode', since, timeout,
                             pred=lambda r: (r.json() or {}).get('mode') == mode
                             and (phase is None or (r.json() or {}).get('charging_phase') == phase))

    def requests(self, since: float):
        """(t, channel, payload) of every e2m message except the heartbeat since `since`."""
        return [(r.t - since, r.topic[len(E2M):], r.json())
                for r in self.rec.find(prefix=E2M, since=since) if not r.topic.endswith('heartbeat')]

    def modes(self, since: float):
        return [(t, p) for t, kind, p in self.requests(since) if kind == 'mode']

    def export_setpoints(self, since: float):
        return [(t, p) for t, kind, p in self.requests(since) if kind == 'export_voltage_current']

    def v2g_message_times(self, name: str, since: float):
        return [r.t for r in self.rec.find(topic=ISO_VAR + 'v2g_messages', since=since)
                if (r.value() or {}).get('id') == name]
