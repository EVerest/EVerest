# SPDX-License-Identifier: Apache-2.0
# Copyright Pionix GmbH and Contributors to EVerest
"""evse_board_support_API against its AsyncAPI specification and the
evse_board_support, connector_lock and ac_rcd interfaces, in the AC SIL with a
fake board that plays hardware and car (basic charging, no HLC).

Each test starts its own EVerest from config-sil.yaml with the simulated board
replaced by the API module; see evse_board_support_api_lab.py.
"""
import time

import pytest
from everest.testing.core_utils.fixtures import *

from evse_board_support_api_lab import (BSP_ERRORS, DEFAULT_CAPABILITIES, E2M, ERR_LOCK, ERR_MAIN, ERR_RCD, EVSE_INOP,
                                        LOCK_ERRORS, M2E, NON_FATAL_ERRORS, RCD_ERRORS, SESSION_EVENT, STOP_REASONS,
                                        VAR_MAIN, VAR_RCD, BoardSupportApiStrategy, Lab)

pytestmark = [
    pytest.mark.everest_core_config('config-sil.yaml'),
    pytest.mark.everest_config_adaptions(BoardSupportApiStrategy()),
]


@pytest.fixture
def lab(request, everest_core, test_controller):
    """EVerest started with the API board; the fake board runs next to it.

    @pytest.mark.board_support_api(attr=value, ...) sets attributes of the fake
    board before it starts; capabilities are only published when a test asks.
    """
    bench = Lab(everest_core)
    marker = request.node.get_closest_marker('board_support_api')
    for name, value in (marker.kwargs if marker else {}).items():
        setattr(bench.board, name, value)
    test_controller.start()
    bench.start_board()
    bench.wait_booted()
    yield bench
    bench.stop()


def forwarded(lab: Lab, var: str, channel: str, payload, var_topic=None):
    t = lab.rec.now()
    lab.rec.publish(M2E + channel, payload)
    r = lab.rec.wait_for(var_topic or VAR_MAIN + var, timeout=1.5, since=t)
    return r.value() if r else None


# --------------------------------------------------------------------------- boot
def test_entrypoint_and_heartbeat(lab: Lab):
    assert lab.rec.wait_for('everest_api/ready_beacon', timeout=5, since=0) is not None
    t = lab.rec.now()
    lab.rec.publish('everest_api/discover', {'headers': {'replyTo': 'test/reply/discover'}})
    reply = lab.rec.wait_for('test/reply/discover', timeout=3, since=t)
    assert reply is not None
    api = reply.json()['apis'][0]
    assert api['type'] == 'evse_board_support' and api['module_id'] == 'connector_1_powerpath' and api['version'] == 1
    assert api['communication_monitoring']['request_reply_timeout_s'] == 2
    boot = [k for t, k, d in lab.board.requests(0)]
    assert 'enable' in boot and 'ac_overcurrent_limit' in boot, boot
    time.sleep(3.1)
    ids = [r.json() for r in lab.rec.find(E2M + 'heartbeat')]
    assert ids[0] == 0 and all(b - a == 1 for a, b in zip(ids, ids[1:])), ids


@pytest.mark.board_support_api(comm_check_s=0)
def test_communication_check(lab: Lab):
    ready = lab.rec.find(E2M + 'heartbeat')[0].t
    fault = lab.wait_error(ERR_MAIN + 'evse_board_support/CommunicationFault', 'Active', since=0, timeout=8)
    assert fault is not None and 4.0 < fault.t - ready < 7.0
    assert fault.value()['sub_type'] == 'Initial communication check'
    assert lab.wait_error(EVSE_INOP, 'Active', since=0, timeout=2) is not None
    t = lab.rec.now()
    lab.board.communication_check(True)
    assert lab.wait_error(ERR_MAIN + 'evse_board_support/CommunicationFault', 'ClearedByModule', since=t, timeout=3)
    assert lab.wait_error(EVSE_INOP, 'ClearedByModule', since=t, timeout=3)


def test_payload_validation(lab: Lab):
    for name in ['A', 'B', 'C', 'D', 'E', 'F', 'PowerOn', 'PowerOff', 'Disconnected']:
        assert forwarded(lab, 'event', 'event', {'event': name}) == {'event': name}
    assert forwarded(lab, 'event', 'event', {'event': 'A', 'raw_voltage': 9.0}) == {'event': 'A'}
    for bad in [{'event': 'G'}, {'event': 'a'}, {'ev': 'A'}]:
        assert forwarded(lab, 'event', 'event', bad) is None, bad
    lab.board.event('A')

    caps = dict(DEFAULT_CAPABILITIES)
    assert forwarded(lab, 'capabilities', 'capabilities', caps)['max_current_A_import'] == 16.0
    without_state_e = {k: v for k, v in caps.items() if k != 'supports_cp_state_E'}
    assert forwarded(lab, 'capabilities', 'capabilities', without_state_e)['supports_cp_state_E'] is False
    for connector in ['IEC62196Type2Socket', 'cCCS2', 'cMCS', 'cNACS']:
        assert forwarded(lab, 'capabilities', 'capabilities', dict(caps, connector_type=connector))['connector_type'] == connector
    for bad in [{k: v for k, v in caps.items() if k != 'max_current_A_import'}, dict(caps, connector_type='Schuko'),
                dict(caps, max_current_A_import='16')]:
        assert forwarded(lab, 'capabilities', 'capabilities', bad) is None
    lab.board.publish_capabilities()

    for ampacity in ['None', 'A_13', 'A_20', 'A_32', 'A_63_3ph_70_1ph']:
        assert forwarded(lab, 'ac_pp_ampacity', 'ac_pp_ampacity', {'ampacity': ampacity}) == {'ampacity': ampacity}
    assert forwarded(lab, 'ac_pp_ampacity', 'ac_pp_ampacity', {'ampacity': 'A_16'}) is None
    for n in (1, 2, 3):
        assert forwarded(lab, 'ac_nr_of_phases_available', 'ac_nr_of_phases', n) == n
    assert forwarded(lab, 'ac_nr_of_phases_available', 'ac_nr_of_phases', '"3"') is None
    for reason in STOP_REASONS:
        assert forwarded(lab, 'request_stop_transaction', 'request_stop_transaction', {'reason': reason}) == {'reason': reason}
    assert forwarded(lab, 'request_stop_transaction', 'request_stop_transaction', {'reason': 'Nonsense'}) is None
    assert forwarded(lab, 'request_stop_transaction', 'request_stop_transaction', {}) is None
    for value in (1.5, 0, -2):
        assert forwarded(lab, '', 'rcd_current', value, var_topic=VAR_RCD + 'rcd_current_mA') == float(value)
    assert forwarded(lab, '', 'rcd_current', '"1"', var_topic=VAR_RCD + 'rcd_current_mA') is None


def test_errors_raise_and_clear(lab: Lab):
    for errors, base in [(BSP_ERRORS, ERR_MAIN + 'evse_board_support/'), (LOCK_ERRORS, ERR_LOCK + 'connector_lock/'),
                         (RCD_ERRORS, ERR_RCD + 'ac_rcd/')]:
        for error_type in errors:
            fatal = error_type not in NON_FATAL_ERRORS
            t = lab.rec.now()
            lab.board.raise_error(error_type, 'sub', 'detail')
            raised = lab.wait_error(base + error_type, 'Active', since=t, timeout=2)
            assert raised is not None, error_type
            assert raised.value()['sub_type'] == 'sub' and raised.value()['message'] == 'detail'
            assert (lab.wait_error(EVSE_INOP, 'Active', since=t, timeout=1.2) is not None) == fatal, error_type
            t = lab.rec.now()
            lab.board.clear_error(error_type, 'sub')
            assert lab.wait_error(base + error_type, 'ClearedByModule', since=t, timeout=2) is not None, error_type
            if fatal:
                assert lab.wait_error(EVSE_INOP, 'ClearedByModule', since=t, timeout=2) is not None, error_type
    # CommunicationFault is accepted although the spec enum does not list it
    t = lab.rec.now()
    lab.board.raise_error('CommunicationFault', 'x')
    assert lab.wait_error(ERR_MAIN + 'evse_board_support/CommunicationFault', 'Active', since=t, timeout=2)
    lab.board.clear_error('CommunicationFault', 'x')
    lab.board.raise_error('NoSuchError')
    assert lab.rec.absent(prefix=ERR_MAIN + 'evse_board_support/NoSuchError', timeout=1)


def test_commands_evsemanager_never_sends(lab: Lab):
    t = lab.rec.now()
    lab.call('rcd', 'self_test')
    assert lab.wait_request('self_test', since=t, timeout=2) is not None
    for reply in (True, False):
        lab.board.reset_reply = reply
        t = lab.rec.now()
        result, took = lab.call('rcd', 'reset')
        request = lab.wait_request('reset', since=t, timeout=2)
        assert result is reply and took < 1.0
        assert request.json()['headers']['replyTo'].startswith('everest_api/1/evse_board_support/connector_1_powerpath/m2e/reply/')
    lab.board.reset_reply = None
    result, took = lab.call('rcd', 'reset', timeout=6)
    assert result is False and 1.8 < took < 3.5, 'default response after cfg_request_reply_to_s'
    lab.board.reset_reply = True
    for impl, command, args, channel, expected in [
        ('connector_lock', 'lock', {}, 'lock', ''), ('connector_lock', 'unlock', {}, 'unlock', ''),
        ('main', 'cp_state_E', {}, 'cp_state_E', ''), ('main', 'cp_state_F', {}, 'cp_state_F', ''),
        ('main', 'cp_state_X1', {}, 'cp_state_X1', ''),
        ('main', 'ac_switch_three_phases_while_charging', {'value': True}, 'ac_switch_three_phases_while_charging', 'ThreePhases'),
        ('main', 'ac_switch_three_phases_while_charging', {'value': False}, 'ac_switch_three_phases_while_charging', 'SinglePhase'),
        ('main', 'ac_set_overcurrent_limit_A', {'value': 15.5}, 'ac_overcurrent_limit', 15.5),
        ('main', 'pwm_on', {'value': 5.0}, 'pwm_on', 5.0),
        ('main', 'enable', {'value': False}, 'enable', False),
        ('main', 'enable', {'value': True}, 'enable', True),
    ]:
        t = lab.rec.now()
        lab.call(impl, command, args)
        r = lab.wait_request(channel, since=t, timeout=2)
        assert r is not None, channel
        assert (r.json() if r.payload else '') == expected, (channel, r.payload)


# --------------------------------------------------------------------------- sessions
def test_basic_ac_session(lab: Lab):
    t0, pwm, t_c, power_on, started = lab.plug_in_and_charge()
    requests = lab.board.requests(t0)
    lock = next(t for t, k, d in requests if k == 'lock')
    assert lock < 0.5, 'plug-in locks the connector'
    assert any(k == 'allow_power_on' and not d['allow_power_on'] for t, k, d in requests), 'power forced off on B'
    assert pwm is not None and abs(pwm.json() - 26.667) < 0.05, 'PWM for 16 A after authorization'
    assert power_on is not None and power_on.t - t_c < 3.0, 'power on within 3 s after C (IEC 61851-1 A.6 seq 4)'
    assert power_on.json()['reason'] == 'FullPowerCharging'
    assert started is not None
    time.sleep(1)
    t_b = lab.rec.now()
    lab.board.event('B')
    off = lab.wait_power(False, since=t_b, timeout=2)
    assert off is not None and off.t - t_b < 0.1, 'power off within 100 ms after C->B (A.6 seq 8.1)'
    assert lab.wait_request('cp_state_X1', since=t_b, timeout=2) is not None
    assert lab.wait_event('ChargingPausedEV', since=t_b, timeout=3) is not None
    time.sleep(0.5)
    t_a = lab.rec.now()
    lab.board.event('A')
    assert lab.wait_request('unlock', since=t_a, timeout=3) is not None
    assert lab.wait_event('SessionFinished', since=t_a, timeout=5) is not None
    events = [(r.value() or {}).get('event') for r in lab.rec.find(topic=SESSION_EVENT, since=t0)]
    assert events[:6] == ['SessionStarted', 'AuthRequired', 'Authorized', 'TransactionStarted', 'PrepareCharging',
                          'ChargingStarted'], events
    assert events[-1] == 'SessionFinished'


def test_stop_request_powers_off_after_six_seconds(lab: Lab):
    """request_stop_transaction stops the transaction; a car that stays in C is switched off after 6 s (A.6 seq 10.2)."""
    t0, pwm, t_c, power_on, started = lab.plug_in_and_charge()
    assert started is not None
    time.sleep(1)
    t = lab.rec.now()
    lab.board.request_stop('Local')
    assert lab.wait_event('StoppingCharging', since=t, timeout=5) is not None
    x1 = lab.wait_request('cp_state_X1', since=t, timeout=5)
    off = lab.wait_power(False, since=t, timeout=12)
    assert x1 is not None and off is not None
    assert 5.5 < off.t - x1.t < 7.0, off.t - x1.t
    lab.board.event('B')
    lab.board.event('A')
    assert lab.wait_event('SessionFinished', since=t, timeout=8) is not None


@pytest.mark.board_support_api(auto_power=False)
def test_charging_starts_without_power_on_confirmation(lab: Lab):
    """ChargingStarted follows allow_power_on(true); PowerOn only tracks the contactor state."""
    t0, pwm, t_c, power_on, started = lab.plug_in_and_charge()
    assert power_on is not None and started is not None
    assert not lab.rec.find(M2E + 'event', since=t0, pred=lambda r: r.json()['event'] == 'PowerOn')
    t = lab.rec.now()
    lab.board.event('B')
    lab.board.event('A')
    assert lab.wait_event('SessionFinished', since=t, timeout=8) is not None


def test_capabilities_gate_the_pwm(lab: Lab):
    """Without capabilities EVerest offers no current (100 % duty cycle, state X1); a runtime update changes the PWM."""
    lab.board.nr_of_phases(3)
    lab.board.event('A')
    time.sleep(0.5)
    t0 = lab.rec.now()
    lab.board.event('B')
    first = lab.wait_request('pwm_on', since=t0, timeout=8)
    assert first is not None and first.json() == 100.0
    assert lab.rec.absent(E2M + 'pwm_on', timeout=5, since=t0, pred=lambda r: r.json() < 100)
    t = lab.rec.now()
    lab.board.publish_capabilities()
    pwm = lab.wait_request('pwm_on', since=t, timeout=8, pred=lambda r: r.json() < 100)
    assert pwm is not None and abs(pwm.json() - 26.667) < 0.05
    lab.board.event('C')
    assert lab.wait_event('ChargingStarted', since=t0, timeout=10) is not None
    t = lab.rec.now()
    lab.board.publish_capabilities(dict(DEFAULT_CAPABILITIES, max_current_A_import=6.0))
    pwm = lab.wait_request('pwm_on', since=t, timeout=8, pred=lambda r: r.json() < 12)
    assert pwm is not None and abs(pwm.json() - 10.0) < 0.1, '6 A -> 10 %'
    lab.board.event('B')
    lab.board.event('A')


def test_errors_during_charging(lab: Lab):
    t0, pwm, t_c, power_on, started = lab.plug_in_and_charge()
    assert started is not None
    time.sleep(1)
    t = lab.rec.now()
    lab.board.raise_error('VendorWarning', 'w', 'warning')
    assert lab.rec.absent(E2M + 'allow_power_on', timeout=2, since=t, pred=lambda r: not r.json()['allow_power_on'])
    lab.board.clear_error('VendorWarning', 'w')
    time.sleep(0.5)
    t = lab.rec.now()
    lab.board.raise_error('MREC8EmergencyStop', 'e', 'button')
    assert lab.wait_power(False, since=t, timeout=3) is not None
    assert lab.rec.wait_for(prefix=E2M + 'cp_state_', timeout=3, since=t) is not None
    assert lab.wait_event('StoppingCharging', since=t, timeout=3) is not None
    lab.board.event('B')
    time.sleep(0.5)
    t = lab.rec.now()
    lab.board.clear_error('MREC8EmergencyStop', 'e')
    assert lab.wait_error(EVSE_INOP, 'ClearedByModule', since=t, timeout=3) is not None
    lab.board.event('A')
    assert lab.wait_event('SessionFinished', since=t, timeout=8) is not None


def test_communication_loss_during_charging(lab: Lab):
    t0, pwm, t_c, power_on, started = lab.plug_in_and_charge()
    assert started is not None
    time.sleep(1)
    t = lab.rec.now()
    lab.board.comm_check_s = 0
    assert lab.wait_error(ERR_MAIN + 'evse_board_support/CommunicationFault', 'Active', since=t, timeout=8) is not None
    assert lab.wait_power(False, since=t, timeout=8) is not None
    lab.board.comm_check_s = 2
    assert lab.wait_error(ERR_MAIN + 'evse_board_support/CommunicationFault', 'ClearedByModule', since=lab.rec.now(),
                          timeout=5) is not None
    lab.board.event('B')
    lab.board.event('A')


@pytest.mark.everest_config_adaptions(BoardSupportApiStrategy(connector_type='sType2'))
def test_socket_proximity_pilot_limits_the_pwm(lab: Lab):
    lab.board.capabilities.update(max_current_A_import=32.0, connector_type='IEC62196Type2Socket')
    lab.board.publish_capabilities()
    lab.board.nr_of_phases(3)
    lab.board.event('A')
    time.sleep(0.5)
    lab.board.pp_ampacity('A_13')
    t0 = lab.rec.now()
    lab.board.event('B')
    pwm = lab.wait_request('pwm_on', since=t0, timeout=12, pred=lambda r: r.json() < 100)
    assert pwm is not None and abs(pwm.json() - 21.667) < 0.05, '13 A cable -> 21.67 %'
    lab.board.event('A')
    assert lab.wait_event('SessionFinished', since=t0, timeout=8) is not None
    lab.board.pp_ampacity('None')
    time.sleep(0.5)
    t1 = lab.rec.now()
    lab.board.event('B')
    assert lab.rec.absent(E2M + 'pwm_on', timeout=5, since=t1, pred=lambda r: r.json() < 100), 'no cable, no PWM'
    lab.board.event('A')


def test_state_d_and_state_e(lab: Lab):
    lab.board.publish_capabilities()
    lab.board.nr_of_phases(3)
    lab.board.event('A')
    time.sleep(0.5)
    t0 = lab.rec.now()
    lab.board.event('B')
    assert lab.wait_request('pwm_on', since=t0, timeout=10, pred=lambda r: r.json() < 100) is not None
    t_d = lab.rec.now()
    lab.board.event('D')
    assert lab.wait_power(True, since=t_d, timeout=3) is not None, 'state D with ventilation charges like C'
    time.sleep(1)
    t_e = lab.rec.now()
    lab.board.event('E')
    assert lab.wait_power(False, since=t_e, timeout=2) is not None
    assert lab.wait_request('unlock', since=t_e, timeout=2) is not None
    lab.board.event('A')
    assert lab.wait_event('SessionFinished', since=t_e, timeout=8) is not None
