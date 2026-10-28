# SPDX-License-Identifier: Apache-2.0
# Copyright Pionix GmbH and Contributors to EVerest
"""Test bench for isolation_monitor_API: a fake isolation monitoring device over
the AsyncAPI topics, the config adaption that puts the API module into the DC
SIL, and a helper to issue its commands directly.
"""
import json
import threading
import time
import uuid
from copy import deepcopy
from typing import Dict, Optional

from everest.testing.core_utils import EverestConfigAdjustmentStrategy
from everest.testing.core_utils.everest_core import EverestCore

from api_recorder import Recorder

MODULE_ID = 'imd'
API = f'everest_api/1/isolation_monitor/{MODULE_ID}'
E2M = API + '/e2m/'
M2E = API + '/m2e/'
MOD = f'modules/{MODULE_ID}/'
VAR = MOD + 'impl/main/var/'
ERR = MOD + 'impl/main/error/isolation_monitor/'
EVSE = 'modules/evse_manager/impl/evse/'
EVSE_INOP = EVSE + 'error/evse_manager/Inoperative'
EVSE_ERR = EVSE + 'error/evse_manager/'
MREC22 = EVSE_ERR + 'MREC22ResistanceFault'
MREC11 = EVSE_ERR + 'MREC11CableCheckFault'
SESSION_EVENT = EVSE + 'var/session_event'
ISO_VAR = 'modules/iso15118_charger/impl/charger/var/'
ISO_CMD = 'modules/iso15118_charger/impl/charger/cmd/'
PSU_CMD = 'modules/powersupply_dc/impl/main/cmd/'

ERROR_TYPES = ['DeviceFault', 'CommunicationFault', 'VendorError', 'VendorWarning']
NON_FATAL_ERRORS = {'VendorWarning'}


class IsolationMonitorApiStrategy(EverestConfigAdjustmentStrategy):
    """config-sil-dc.yaml with IMDSimulator replaced by isolation_monitor_API.

    With din=False both sides are pinned to ISO 15118-2 so the EV runs welding
    detection, and the IMD becomes the welding-detection voltage source.
    relays_open runs the IMD self test before the contactors close, at a test
    voltage the simulated supply can deliver.
    """

    def __init__(self, din: bool = True, relays_open: bool = False):
        self.din = din
        self.relays_open = relays_open

    def adjust_everest_configuration(self, everest_config: Dict):
        config = deepcopy(everest_config)
        modules = config['active_modules']
        modules[MODULE_ID] = {
            'module': 'isolation_monitor_API',
            'config_module': {'cfg_communication_check_to_s': 5, 'cfg_heartbeat_interval_ms': 1000},
        }
        evse = modules['evse_manager']['config_module']
        if not self.din:
            modules['iso15118_charger']['config_module']['supported_DIN70121'] = False
            modules['iso15118_car']['config_module']['supported_DIN70121'] = False
            evse['welding_detection_voltage_source'] = 'isolation_monitor'
        if self.relays_open:
            evse['cable_check_enable_imd_self_test_relays_open'] = True
            evse['cable_check_relays_open_voltage_V'] = 250
        return config


class FakeIsolationMonitor:
    """IMD behind isolation_monitor_API.

    Switches:
      resistance_ohm      reported resistance
      interval_s          measurement period while started, 0 publishes nothing
      self_test_result    True/False, None never answers
      self_test_delay_s
      l1e_V / l2e_V       voltages to earth; None leaves them out
      comm_check_s        automatic communication_check period, 0 disables
    """

    def __init__(self, recorder: Recorder):
        self.rec = recorder
        self.resistance_ohm = 900000.0
        self.interval_s = 1.0
        self.self_test_result = True
        self.self_test_delay_s = 1.0
        self.l1e_V = None
        self.l2e_V = None
        self.comm_check_s = 2.0
        self.started = False
        self.supply_voltage = 0.0
        self.received = []
        self._lock = threading.Lock()
        self._stop = threading.Event()
        self._thread = threading.Thread(target=self._loop, daemon=True)
        recorder.listeners.append(self._on_record)

    def start(self):
        self._thread.start()
        return self

    def stop(self):
        self._stop.set()
        if self._thread.is_alive():
            self._thread.join(2.0)

    def _on_record(self, record):
        if record.topic.startswith(E2M):
            kind = record.topic[len(E2M):]
            with self._lock:
                self.received.append((record.t, kind, record.payload))
                if kind == 'start':
                    self.started = True
                elif kind == 'stop':
                    self.started = False
                elif kind == 'start_self_test' and self.self_test_result is not None:
                    threading.Timer(self.self_test_delay_s, self.publish_self_test_result,
                                    [self.self_test_result]).start()
        elif record.topic == PSU_CMD + 'setExportVoltageCurrent':
            with self._lock:
                self.supply_voltage = float((record.value() or {}).get('voltage', 0.0))

    def measurement(self, resistance: Optional[float] = None):
        m = {'resistance_F_Ohm': self.resistance_ohm if resistance is None else resistance,
             'voltage_V': self.supply_voltage}
        if self.l1e_V is not None:
            m['voltage_to_earth_l1e_V'] = self.l1e_V
        if self.l2e_V is not None:
            m['voltage_to_earth_l2e_V'] = self.l2e_V
        self.rec.publish(M2E + 'isolation_measurement', m)

    def publish_self_test_result(self, result: bool):
        self.rec.publish(M2E + 'self_test_result', 'true' if result else 'false')

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

    def _loop(self):
        last_check = 0.0
        last_measurement = 0.0
        while not self._stop.is_set():
            now = time.monotonic()
            if self.interval_s > 0 and self.started and now - last_measurement >= self.interval_s:
                last_measurement = now
                self.measurement()
            if self.comm_check_s > 0 and now - last_check >= self.comm_check_s:
                last_check = now
                self.communication_check(True)
            time.sleep(0.05)

    def requests(self, since: float, kind: Optional[str] = None):
        with self._lock:
            return [(t - since, k, p) for t, k, p in self.received
                    if t >= since and k != 'heartbeat' and (kind is None or k == kind)]


class Lab:
    def __init__(self, everest_core: EverestCore):
        self.rec = Recorder(everest_core).start()
        self.imd = FakeIsolationMonitor(self.rec)
        self.external_prefix = everest_core.mqtt_external_prefix
        self.internal_prefix = self.rec.internal_prefix

    def start_imd(self):
        self.imd.start()
        return self

    def stop(self):
        self.imd.stop()
        self.rec.stop()

    def wait_booted(self, timeout=60.0):
        assert self.rec.wait_for(E2M + 'heartbeat', timeout=timeout, since=0) is not None, 'no heartbeat'
        assert self.rec.wait_for(prefix='everest_external/nodered/1/state/', timeout=timeout, since=0) is not None

    def wait_event(self, name: str, since: float, timeout: float = 60.0):
        return self.rec.wait_for(SESSION_EVENT, timeout=timeout, since=since,
                                 pred=lambda r: (r.value() or {}).get('event') == name)

    def wait_request(self, kind: str, since: float, timeout: float = 30.0, pred=None):
        return self.rec.wait_for(E2M + kind, timeout=timeout, since=since, pred=pred)

    def wait_error(self, topic: str, state: str, since: float, timeout: float = 5.0):
        return self.rec.wait_for(topic, timeout=timeout, since=since,
                                 pred=lambda r: (r.value() or {}).get('state') == state)

    def call(self, command: str, args: Optional[Dict] = None, timeout: float = 5.0):
        """Issue an internal command to the module the way EvseManager does."""
        call_id = str(uuid.uuid4())
        topic = f'{MOD}impl/main/cmd/{command}'
        t = self.rec.now()
        self.rec.client.publish(self.internal_prefix + topic,
                                json.dumps({'data': {'args': args or {}, 'id': call_id, 'origin': 'test'},
                                            'msg_type': 'Cmd'}))
        r = self.rec.wait_for(topic + '/response/test', timeout=timeout, since=t,
                              pred=lambda r: (((r.json() or {}).get('data') or {}).get('data') or {}).get('id') == call_id)
        return (r.json()['data']['data'].get('retval'), r.t - t) if r else (None, None)

    def dc_session(self, hold_s: int = 12):
        t0 = self.rec.now()
        self.rec.publish('everest_external/nodered/1/carsim/cmd/execute_charging_session',
                         f'sleep 1;iso_wait_slac_matched;iso_start_v2g_session DC  86400 0;iso_wait_pwr_ready;'
                         f'iso_wait_for_stop {hold_s};iso_wait_v2g_session_stopped;unplug')
        return t0

    def charging_session(self, hold_s: int = 12):
        t0 = self.dc_session(hold_s)
        started = self.wait_event('ChargingStarted', since=t0, timeout=60)
        assert started is not None, f'no ChargingStarted; requests: {self.imd.requests(t0)}'
        return t0, started

    def isolation_status(self, since: float):
        return [(r.value() or {}).get('isolation_status')
                for r in self.rec.find(topic=ISO_CMD + 'update_isolation_status', since=since)]
