# SPDX-License-Identifier: Apache-2.0
# Copyright Pionix GmbH and Contributors to EVerest
"""Test bench for evse_board_support_API: a fake board that plays the hardware and
the car over the AsyncAPI topics, the config adaption that puts the API module into
the AC SIL, and a helper to issue the commands EvseManager never sends.
"""
import json
import threading
import time
import uuid
from copy import deepcopy
from typing import Dict, Optional

from everest.testing.core_utils import EverestConfigAdjustmentStrategy
from everest.testing.core_utils.everest_core import EverestCore

from power_supply_dc_api_lab import Recorder

MODULE_ID = 'connector_1_powerpath'
API = f'everest_api/1/evse_board_support/{MODULE_ID}'
E2M = API + '/e2m/'
M2E = API + '/m2e/'
MOD = f'modules/{MODULE_ID}/'
VAR_MAIN = MOD + 'impl/main/var/'
VAR_RCD = MOD + 'impl/rcd/var/'
ERR_MAIN = MOD + 'impl/main/error/'
ERR_LOCK = MOD + 'impl/connector_lock/error/'
ERR_RCD = MOD + 'impl/rcd/error/'
EVSE = 'modules/connector_1/impl/evse/'
EVSE_INOP = EVSE + 'error/evse_manager/Inoperative'
SESSION_EVENT = EVSE + 'var/session_event'

DEFAULT_CAPABILITIES = {
    'max_current_A_import': 16.0, 'min_current_A_import': 6.0,
    'max_phase_count_import': 3, 'min_phase_count_import': 1,
    'max_current_A_export': 16.0, 'min_current_A_export': 0.0,
    'max_phase_count_export': 3, 'min_phase_count_export': 1,
    'supports_changing_phases_during_charging': True,
    'supports_cp_state_E': True,
    'connector_type': 'IEC62196Type2Cable',
}

BSP_ERRORS = ['DiodeFault', 'VentilationNotAvailable', 'BrownOut', 'EnergyManagement', 'PermanentFault',
              'MREC2GroundFailure', 'MREC3HighTemperature', 'MREC4OverCurrentFailure', 'MREC5OverVoltage',
              'MREC6UnderVoltage', 'MREC8EmergencyStop', 'MREC10InvalidVehicleMode', 'MREC14PilotFault',
              'MREC15PowerLoss', 'MREC17EVSEContactorFault', 'MREC18CableOverTempDerate', 'MREC19CableOverTempStop',
              'MREC20PartialInsertion', 'MREC23ProximityFault', 'MREC24ConnectorVoltageHigh', 'MREC25BrokenLatch',
              'MREC26CutCable', 'TiltDetected', 'WaterIngressDetected', 'EnclosureOpen', 'VendorError', 'VendorWarning']
LOCK_ERRORS = ['ConnectorLockCapNotCharged', 'ConnectorLockUnexpectedOpen', 'ConnectorLockUnexpectedClose',
               'ConnectorLockFailedLock', 'ConnectorLockFailedUnlock', 'MREC1ConnectorLockFailure']
RCD_ERRORS = ['Selftest', 'DC', 'AC']
# errors EvseManager keeps charging through (ErrorHandling.cpp IgnoreErrors)
NON_FATAL_ERRORS = {'MREC3HighTemperature', 'MREC18CableOverTempDerate', 'VendorWarning'}
STOP_REASONS = ['EmergencyStop', 'EVDisconnected', 'HardReset', 'Local', 'Other', 'PowerLoss', 'Reboot', 'Remote',
                'SoftReset', 'UnlockCommand']


class BoardSupportApiStrategy(EverestConfigAdjustmentStrategy):
    """config-sil.yaml with the simulated board replaced by evse_board_support_API.

    EvseManager (connector_1) gets bsp, connector_lock and ac_rcd from the API
    module; the simulator stays as sim_powerpath for the grid power meter. The
    fake board plays the car, so the ISO 15118, SLAC and EV simulator modules are
    dropped and HLC is disabled: the bench needs no network interface.
    """

    DROP = ('iso15118_charger', 'iso15118_car', 'slac', 'ev_manager', 'ev_api')

    def __init__(self, connector_type: Optional[str] = None, request_reply_timeout_s: int = 2):
        self.connector_type = connector_type
        self.request_reply_timeout_s = request_reply_timeout_s

    def adjust_everest_configuration(self, everest_config: Dict):
        config = deepcopy(everest_config)
        modules = config['active_modules']
        modules['sim_powerpath'] = modules[MODULE_ID]
        modules[MODULE_ID] = {
            'module': 'evse_board_support_API',
            'config_module': {'cfg_communication_check_to_s': 5, 'cfg_heartbeat_interval_ms': 1000,
                              'cfg_request_reply_to_s': self.request_reply_timeout_s},
        }
        for name in self.DROP:
            modules.pop(name, None)
        for module_id, module in modules.items():
            for requirement, connections in (module.get('connections') or {}).items():
                for connection in connections:
                    if connection['module_id'] == MODULE_ID and not (
                            module_id == 'connector_1' and requirement in ('bsp', 'ac_rcd', 'connector_lock')):
                        connection['module_id'] = 'sim_powerpath'
        evse = modules['connector_1']
        evse['connections']['bsp'][0]['implementation_id'] = 'main'
        for requirement in ('hlc', 'slac'):
            evse['connections'].pop(requirement, None)
        evse['config_module']['ac_hlc_enabled'] = False
        if self.connector_type:
            evse['config_module']['connector_type'] = self.connector_type
        return config


class FakeBoard:
    """Hardware and car behind evse_board_support_API.

    Switches:
      auto_power     publish PowerOn/PowerOff after allow_power_on true/false
      reset_reply    True/False to answer reset requests, None leaves them unanswered
      comm_check_s   automatic communication_check period, 0 disables
    """

    def __init__(self, recorder: Recorder, capabilities: Optional[Dict] = None):
        self.rec = recorder
        self.capabilities = dict(capabilities or DEFAULT_CAPABILITIES)
        self.auto_power = True
        self.relay_delay_s = 0.1
        self.reset_reply = True
        self.comm_check_s = 2.0
        self.cp = 'A'
        self.power_allowed = False
        self.relay = False
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
        if not record.topic.startswith(E2M):
            return
        kind = record.topic[len(E2M):]
        data = record.json() if record.payload else ''
        with self._lock:
            self.received.append((record.t, kind, data))
            if kind == 'allow_power_on' and isinstance(data, dict):
                self.power_allowed = bool(data.get('allow_power_on'))
                if self.auto_power:
                    threading.Timer(self.relay_delay_s, self._apply_relay).start()
            elif kind == 'reset' and isinstance(data, dict) and self.reset_reply is not None:
                reply_to = (data.get('headers') or {}).get('replyTo')
                if reply_to:
                    self.rec.publish(reply_to, json.dumps(self.reset_reply))

    def _apply_relay(self):
        with self._lock:
            if self.power_allowed and self.cp in ('C', 'D') and not self.relay:
                self.relay = True
                self.event('PowerOn')
            elif not self.power_allowed and self.relay:
                self.relay = False
                self.event('PowerOff')

    def event(self, name: str):
        if name in 'ABCDEF':
            self.cp = name
        self.rec.publish(M2E + 'event', {'event': name})

    def publish_capabilities(self, capabilities: Optional[Dict] = None):
        self.rec.publish(M2E + 'capabilities', self.capabilities if capabilities is None else capabilities)

    def pp_ampacity(self, value: str):
        self.rec.publish(M2E + 'ac_pp_ampacity', {'ampacity': value})

    def nr_of_phases(self, n: int):
        self.rec.publish(M2E + 'ac_nr_of_phases', n)

    def rcd_current(self, milliamps: float):
        self.rec.publish(M2E + 'rcd_current', milliamps)

    def request_stop(self, reason: str = 'Local'):
        self.rec.publish(M2E + 'request_stop_transaction', {'reason': reason})

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
        last = 0.0
        while not self._stop.is_set():
            now = time.monotonic()
            if self.comm_check_s > 0 and now - last >= self.comm_check_s:
                last = now
                self.communication_check(True)
            time.sleep(0.05)

    def requests(self, since: float, kind: Optional[str] = None):
        with self._lock:
            return [(t - since, k, d) for t, k, d in self.received
                    if t >= since and k != 'heartbeat' and (kind is None or k == kind)]


class Lab:
    def __init__(self, everest_core: EverestCore, capabilities: Optional[Dict] = None):
        self.rec = Recorder(everest_core).start()
        self.board = FakeBoard(self.rec, capabilities)
        self.internal_prefix = self.rec.internal_prefix

    def start_board(self):
        self.board.start()
        return self

    def stop(self):
        self.board.stop()
        self.rec.stop()

    def wait_booted(self, timeout=30.0):
        assert self.rec.wait_for(E2M + 'heartbeat', timeout=timeout, since=0) is not None, 'no heartbeat'
        assert self.rec.wait_for(E2M + 'enable', timeout=timeout, since=0) is not None, 'no enable at boot'

    def wait_event(self, name: str, since: float, timeout: float = 20.0):
        return self.rec.wait_for(SESSION_EVENT, timeout=timeout, since=since,
                                 pred=lambda r: (r.value() or {}).get('event') == name)

    def wait_request(self, kind: str, since: float, timeout: float = 10.0, pred=None):
        return self.rec.wait_for(E2M + kind, timeout=timeout, since=since, pred=pred)

    def wait_power(self, on: bool, since: float, timeout: float = 5.0):
        return self.wait_request('allow_power_on', since, timeout, pred=lambda r: r.json()['allow_power_on'] is on)

    def wait_error(self, topic: str, state: str, since: float, timeout: float = 5.0):
        return self.rec.wait_for(topic, timeout=timeout, since=since,
                                 pred=lambda r: (r.value() or {}).get('state') == state)

    def call(self, impl: str, command: str, args: Optional[Dict] = None, timeout: float = 5.0):
        """Issue an internal command to the API module the way a requiring module does.

        The framework accepts the command envelope from any origin; the result comes
        back on the origin's response topic.
        """
        call_id = str(uuid.uuid4())
        topic = f'{MOD}impl/{impl}/cmd/{command}'
        t = self.rec.now()
        self.rec.client.publish(self.internal_prefix + topic,
                                json.dumps({'data': {'args': args or {}, 'id': call_id, 'origin': 'test'},
                                            'msg_type': 'Cmd'}))
        r = self.rec.wait_for(topic + '/response/test', timeout=timeout, since=t,
                              pred=lambda r: (((r.json() or {}).get('data') or {}).get('data') or {}).get('id') == call_id)
        return (r.json()['data']['data'].get('retval'), r.t - t) if r else (None, None)

    def plug_in_and_charge(self, publish_capabilities: bool = True):
        """A -> B -> authorized PWM -> C -> power on -> ChargingStarted. Returns (t0, pwm, t_c, power_on, started)."""
        if publish_capabilities:
            self.board.publish_capabilities()
        self.board.nr_of_phases(3)
        time.sleep(0.5)
        self.board.event('A')
        time.sleep(0.5)
        t0 = self.rec.now()
        self.board.event('B')
        pwm = self.wait_request('pwm_on', since=t0, timeout=15, pred=lambda r: r.json() < 100)
        self.board.event('C')
        t_c = self.rec.now()
        power_on = self.wait_power(True, since=t_c, timeout=5)
        started = self.wait_event('ChargingStarted', since=t0, timeout=10)
        return t0, pwm, t_c, power_on, started
