# SPDX-License-Identifier: Apache-2.0
# Copyright Pionix GmbH and Contributors to EVerest
"""Test bench for over_voltage_monitor_API: a fake over voltage monitoring
device over the AsyncAPI topics, the config adaption that puts the API module
into the DC SIL, and a helper to issue its commands directly.
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

MODULE_ID = 'ovm'
API = f'everest_api/1/over_voltage_monitor/{MODULE_ID}'
E2M = API + '/e2m/'
M2E = API + '/m2e/'
MOD = f'modules/{MODULE_ID}/'
VAR = MOD + 'impl/main/var/'
ERR = MOD + 'impl/main/error/over_voltage_monitor/'
EVSE = 'modules/evse_manager/impl/evse/'
EVSE_INOP = EVSE + 'error/evse_manager/Inoperative'
EVSE_ERR = EVSE + 'error/evse_manager/'
MREC5 = EVSE_ERR + 'MREC5OverVoltage'
PLAUSIBILITY = EVSE_ERR + 'VoltagePlausibilityFault'
SESSION_EVENT = EVSE + 'var/session_event'
EV_INFO = EVSE + 'var/ev_info'
ISO_CMD = 'modules/iso15118_charger/impl/charger/cmd/'
PSU_VAR = 'modules/powersupply_dc/impl/main/var/'
BSP_CMD = 'modules/yeti_driver/impl/board_support/cmd/'
CP_OFF_COMMANDS = ('cp_state_X1', 'cp_state_F', 'pwm_off')

ERROR_TYPES = ['MREC5OverVoltage', 'DeviceFault', 'CommunicationFault', 'VendorError', 'VendorWarning']
NON_FATAL_ERRORS = {'VendorWarning'}


class OverVoltageMonitorApiStrategy(EverestConfigAdjustmentStrategy):
    """config-sil-dc.yaml with OVMSimulator replaced by over_voltage_monitor_API."""

    def adjust_everest_configuration(self, everest_config: Dict):
        config = deepcopy(everest_config)
        config['active_modules'][MODULE_ID] = {
            'module': 'over_voltage_monitor_API',
            'config_module': {'cfg_communication_check_to_s': 5, 'cfg_heartbeat_interval_ms': 1000},
        }
        return config


def emergency_threshold(negotiated_max_voltage_V: float) -> float:
    """IEC 61851-23:2023 Table 103 as EvseManager applies it (plus the draft 61851-23-3 row)."""
    if negotiated_max_voltage_V > 1000:
        return 1375.0
    if negotiated_max_voltage_V > 850:
        return 1100.0
    if negotiated_max_voltage_V > 750:
        return 935.0
    if negotiated_max_voltage_V > 500:
        return 825.0
    return 550.0


class FakeOverVoltageMonitor:
    """Device behind over_voltage_monitor_API.

    Switches:
      interval_s          measurement period while started, 0 publishes nothing
      publish_when_stopped  keep publishing although stopped
      offset_V            added to the supply's measured voltage
      fixed_V             publish this value instead of the supply voltage
      comm_check_s        automatic communication_check period, 0 disables
      clear_on_reset      clear MREC5OverVoltage when reset_over_voltage_error arrives
    """

    def __init__(self, recorder: Recorder):
        self.rec = recorder
        self.interval_s = 1.0
        self.publish_when_stopped = False
        self.offset_V = 0.0
        self.fixed_V: Optional[float] = None
        self.comm_check_s = 2.0
        self.clear_on_reset = True
        self.started = False
        self.limits: Optional[Dict] = None
        self.supply_voltage = 0.0
        self.received = []
        self._over_voltage_error_active = False
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
                elif kind == 'set_limits':
                    self.limits = record.json()
                elif kind == 'reset_over_voltage_error':
                    self.started = False
                    if self.clear_on_reset and self._over_voltage_error_active:
                        self._over_voltage_error_active = False
                        self.rec.publish(M2E + 'clear_error', {'type': 'MREC5OverVoltage'})
        elif record.topic == PSU_VAR + 'voltage_current':
            with self._lock:
                self.supply_voltage = float((record.value() or {}).get('voltage_V', 0.0))

    def voltage(self) -> float:
        with self._lock:
            return self.fixed_V if self.fixed_V is not None else self.supply_voltage + self.offset_V

    def measurement(self, value: Optional[float] = None):
        self.rec.publish(M2E + 'voltage_measurement_V', self.voltage() if value is None else value)

    def communication_check(self, value: bool = True):
        self.rec.publish(M2E + 'communication_check', 'true' if value else 'false')

    def raise_error(self, type_: str, sub_type: Optional[str] = None, message: Optional[str] = None,
                    severity: Optional[str] = None):
        error = {'type': type_}
        if sub_type is not None:
            error['sub_type'] = sub_type
        if message is not None:
            error['message'] = message
        if severity is not None:
            error['severity'] = severity
        if type_ == 'MREC5OverVoltage':
            self._over_voltage_error_active = True
        self.rec.publish(M2E + 'raise_error', error)

    def clear_error(self, type_: str, sub_type: Optional[str] = None):
        error = {'type': type_}
        if sub_type is not None:
            error['sub_type'] = sub_type
        if type_ == 'MREC5OverVoltage':
            self._over_voltage_error_active = False
        self.rec.publish(M2E + 'clear_error', error)

    def _loop(self):
        last_check = 0.0
        last_measurement = 0.0
        while not self._stop.is_set():
            now = time.monotonic()
            if (self.interval_s > 0 and (self.started or self.publish_when_stopped)
                    and now - last_measurement >= self.interval_s):
                last_measurement = now
                self.measurement()
            if self.comm_check_s > 0 and now - last_check >= self.comm_check_s:
                last_check = now
                self.communication_check(True)
            time.sleep(0.02)

    def requests(self, since: float, kind: Optional[str] = None):
        with self._lock:
            return [(t - since, k, p) for t, k, p in self.received
                    if t >= since and k != 'heartbeat' and (kind is None or k == kind)]


class Lab:
    def __init__(self, everest_core: EverestCore):
        self.rec = Recorder(everest_core).start()
        self.ovm = FakeOverVoltageMonitor(self.rec)
        self.internal_prefix = self.rec.internal_prefix

    def start_ovm(self):
        self.ovm.start()
        return self

    def stop(self):
        self.ovm.stop()
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

    def wait_cp_off(self, since: float, timeout: float = 4.0):
        """The BSP command that ends the charging PWM: cp_state_X1, cp_state_F or pwm_off."""
        return self.rec.wait_for(prefix=BSP_CMD, timeout=timeout, since=since,
                                 pred=lambda r: r.topic[len(BSP_CMD):] in CP_OFF_COMMANDS)

    def wait_contactor_open(self, since: float, timeout: float = 2.0):
        def opened(r):
            args = ((r.json() or {}).get('data') or {}).get('args') or {}
            return (args.get('value') or {}).get('allow_power_on') is False
        return self.rec.wait_for(BSP_CMD + 'allow_power_on', timeout=timeout, since=since, pred=opened)

    def wait_hlc_error(self, error: str, since: float, timeout: float = 2.0):
        """The send_error command to the charger, which follows the CP and contactor commands."""
        return self.rec.wait_for(ISO_CMD + 'send_error', timeout=timeout, since=since,
                                 pred=lambda r: ((r.json() or {}).get('data') or {}).get('args', {}).get('error') == error)

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
        assert started is not None, f'no ChargingStarted; requests: {self.ovm.requests(t0)}'
        return t0, started

    def ev_maximum_voltage(self, since: float = 0.0) -> Optional[float]:
        for r in reversed(self.rec.find(topic=EV_INFO, since=since)):
            v = (r.value() or {}).get('maximum_voltage_limit')
            if v is not None:
                return v
        return None

    def supply_maximum_voltage(self) -> Optional[float]:
        caps = self.rec.find(topic=PSU_VAR + 'capabilities')
        return (caps[-1].value() or {}).get('max_export_voltage_V') if caps else None
