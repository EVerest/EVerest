# SPDX-License-Identifier: Apache-2.0
# Copyright Pionix GmbH and Contributors to EVerest
"""isolation_monitor_API against its AsyncAPI specification and the
isolation_monitor interface, in the DC SIL with a fake IMD.

Each test starts its own EVerest from config-sil-dc.yaml with IMDSimulator
replaced by the API module; see isolation_monitor_api_lab.py.
"""
import time

import pytest
from everest.testing.core_utils.fixtures import *

from isolation_monitor_api_lab import (E2M, ERR, ERROR_TYPES, EVSE_INOP, ISO_VAR, M2E, MREC11, MREC22,
                                       NON_FATAL_ERRORS, VAR, IsolationMonitorApiStrategy, Lab)

pytestmark = [
    pytest.mark.everest_core_config('config-sil-dc.yaml'),
    pytest.mark.everest_config_adaptions(IsolationMonitorApiStrategy()),
    # every test runs the ISO 15118 SECC on the shared interface, like the DC smoke tests
    pytest.mark.xdist_group(name='ISO15118'),
    # the simulated EV occasionally drops CP to state B right after PowerDelivery
    pytest.mark.flaky(reruns=1),
]


@pytest.fixture
def lab(request, everest_core, test_controller):
    """EVerest started with the API module; the fake IMD runs next to it.

    @pytest.mark.isolation_monitor_api(attr=value, ...) sets attributes of the
    fake IMD before it starts.
    """
    bench = Lab(everest_core)
    marker = request.node.get_closest_marker('isolation_monitor_api')
    for name, value in (marker.kwargs if marker else {}).items():
        setattr(bench.imd, name, value)
    test_controller.start()
    bench.start_imd()
    bench.wait_booted()
    yield bench
    bench.stop()


def forwarded(lab: Lab, var: str, channel: str, payload):
    t = lab.rec.now()
    lab.rec.publish(M2E + channel, payload)
    r = lab.rec.wait_for(VAR + var, timeout=1.5, since=t)
    return r.value() if r else None


# --------------------------------------------------------------------------- boot
def test_entrypoint_and_heartbeat(lab: Lab):
    assert lab.rec.wait_for('everest_api/ready_beacon', timeout=5, since=0) is not None
    t = lab.rec.now()
    lab.rec.publish('everest_api/discover', {'headers': {'replyTo': 'test/reply/discover'}})
    reply = lab.rec.wait_for('test/reply/discover', timeout=3, since=t)
    assert reply is not None
    api = reply.json()['apis'][0]
    assert api['type'] == 'isolation_monitor' and api['module_id'] == 'imd' and api['version'] == 1
    assert [k for t, k, p in lab.imd.requests(0)] == ['stop'], 'EvseManager stops the monitor once at boot'
    time.sleep(3.1)
    ids = [r.json() for r in lab.rec.find(E2M + 'heartbeat')]
    assert ids[0] == 0 and all(b - a == 1 for a, b in zip(ids, ids[1:])), ids


@pytest.mark.isolation_monitor_api(comm_check_s=0)
def test_communication_check(lab: Lab):
    ready = lab.rec.find(E2M + 'heartbeat')[0].t
    fault = lab.wait_error(ERR + 'CommunicationFault', 'Active', since=0, timeout=8)
    assert fault is not None and 4.0 < fault.t - ready < 7.0
    assert fault.value()['sub_type'] == 'Initial communication check'
    assert lab.wait_error(EVSE_INOP, 'Active', since=0, timeout=2) is not None
    t = lab.rec.now()
    lab.imd.communication_check(True)
    assert lab.wait_error(ERR + 'CommunicationFault', 'ClearedByModule', since=t, timeout=3) is not None
    assert lab.wait_error(EVSE_INOP, 'ClearedByModule', since=t, timeout=3) is not None


def test_payload_validation_and_commands(lab: Lab):
    full = {'resistance_F_Ohm': 123.5, 'voltage_V': 400, 'voltage_to_earth_l1e_V': 200, 'voltage_to_earth_l2e_V': -200}
    assert forwarded(lab, 'isolation_measurement', 'isolation_measurement', {'resistance_F_Ohm': 900000}) == {'resistance_F_Ohm': 900000.0}
    assert forwarded(lab, 'isolation_measurement', 'isolation_measurement', full) == {k: float(v) for k, v in full.items()}
    assert forwarded(lab, 'isolation_measurement', 'isolation_measurement', {'resistance_F_Ohm': 1, 'extra': 1}) == {'resistance_F_Ohm': 1.0}
    for bad in [{'voltage_V': 400}, {'resistance_F_Ohm': '900000'}, '900000']:
        assert forwarded(lab, 'isolation_measurement', 'isolation_measurement', bad) is None, bad
    assert forwarded(lab, 'self_test_result', 'self_test_result', 'true') is True
    assert forwarded(lab, 'self_test_result', 'self_test_result', 'false') is False
    for bad in ('"true"', '1'):
        assert forwarded(lab, 'self_test_result', 'self_test_result', bad) is None, bad
    for command, args, expected in [('start', {}, '{}'), ('stop', {}, '{}'),
                                    ('start_self_test', {'test_voltage_V': 500.0}, '500.0')]:
        t = lab.rec.now()
        lab.call(command, args)
        r = lab.wait_request(command, since=t, timeout=2)
        assert r is not None and r.payload == expected, (command, r.payload if r else None)


def test_errors_raise_and_clear(lab: Lab):
    for error_type in ERROR_TYPES:
        fatal = error_type not in NON_FATAL_ERRORS
        t = lab.rec.now()
        lab.imd.raise_error(error_type, 'sub', 'detail')
        raised = lab.wait_error(ERR + error_type, 'Active', since=t, timeout=2)
        assert raised is not None and raised.value()['sub_type'] == 'sub' and raised.value()['message'] == 'detail'
        assert (lab.wait_error(EVSE_INOP, 'Active', since=t, timeout=1.5) is not None) == fatal, error_type
        t = lab.rec.now()
        lab.imd.clear_error(error_type, 'sub')
        assert lab.wait_error(ERR + error_type, 'ClearedByModule', since=t, timeout=2) is not None, error_type
        if fatal:
            assert lab.wait_error(EVSE_INOP, 'ClearedByModule', since=t, timeout=2) is not None, error_type
    lab.imd.raise_error('IsolationFault')
    assert lab.rec.absent(prefix=ERR + 'IsolationFault', timeout=1)


# --------------------------------------------------------------------------- sessions
def test_dc_session_sequence(lab: Lab):
    t0, started = lab.charging_session()
    finished = lab.wait_event('SessionFinished', since=t0, timeout=90)
    assert finished is not None
    requests = lab.imd.requests(t0)
    kinds = [k for t, k, p in requests]
    self_test = next(((t, p) for t, k, p in requests if k == 'start_self_test'), None)
    assert self_test is not None and float(self_test[1]) >= 200.0, 'self test at the cable-check voltage'
    start = next(t for t, k, p in requests if k == 'start')
    assert start > self_test[0], 'start after the self test'
    assert kinds[-1] == 'stop', kinds
    statuses = lab.isolation_status(t0)
    assert statuses and set(statuses) == {'Valid'}, statuses


@pytest.mark.isolation_monitor_api(self_test_result=False)
def test_failed_self_test_fails_the_cable_check(lab: Lab):
    t0 = lab.dc_session()
    assert lab.wait_error(MREC11, 'Active', since=t0, timeout=40) is not None
    assert lab.wait_event('ChargingStarted', since=t0, timeout=0.1) is None
    assert lab.wait_event('SessionFinished', since=t0, timeout=90) is not None


@pytest.mark.isolation_monitor_api(interval_s=0)
def test_missing_measurement_fails_the_cable_check(lab: Lab):
    """After start EvseManager waits 5 s for the first isolation measurement."""
    t0 = lab.dc_session()
    start = lab.wait_request('start', since=t0, timeout=40)
    assert start is not None
    fault = lab.wait_error(MREC11, 'Active', since=start.t, timeout=15)
    assert fault is not None and 4.0 < fault.t - start.t < 10.0, fault
    assert lab.wait_event('SessionFinished', since=t0, timeout=90) is not None


@pytest.mark.isolation_monitor_api(resistance_ohm=50000.0)
def test_low_resistance_during_cable_check(lab: Lab):
    """Below 100 kOhm: MREC22ResistanceFault/Resistance, Fault reported to the EV, no charging."""
    t0 = lab.dc_session()
    fault = lab.wait_error(MREC22, 'Active', since=t0, timeout=40)
    assert fault is not None and fault.value()['sub_type'] == 'Resistance'
    assert lab.wait_event('ChargingStarted', since=t0, timeout=0.1) is None
    assert lab.wait_event('SessionFinished', since=t0, timeout=90) is not None
    assert 'Fault' in lab.isolation_status(t0)


def test_low_resistance_during_charging(lab: Lab):
    t0, started = lab.charging_session(hold_s=40)
    time.sleep(2)
    t = lab.rec.now()
    lab.imd.resistance_ohm = 50000.0
    fault = lab.wait_error(MREC22, 'Active', since=t, timeout=5)
    assert fault is not None and fault.value()['sub_type'] == 'Resistance'
    assert lab.wait_event('StoppingCharging', since=t, timeout=5) is not None
    lab.imd.resistance_ohm = 900000.0
    assert lab.wait_event('SessionFinished', since=t, timeout=60) is not None


def test_voltage_to_earth_fault_during_charging(lab: Lab):
    """IEC 61851-23:2023 6.3.1.112.2: two samples >= 2 s apart above 110 % of voltage_V."""
    t0, started = lab.charging_session(hold_s=40)
    time.sleep(2)
    t = lab.rec.now()
    lab.imd.l1e_V = 600.0
    lab.imd.l2e_V = 50.0
    fault = lab.wait_error(MREC22, 'Active', since=t, timeout=8)
    assert fault is not None and fault.value()['sub_type'] == 'VoltageToEarth'
    assert fault.t - t >= 2.0, 'not before the second sample 2 s later'
    lab.imd.l1e_V = None
    lab.imd.l2e_V = None
    assert lab.wait_event('SessionFinished', since=t, timeout=60) is not None


@pytest.mark.everest_config_adaptions(IsolationMonitorApiStrategy(relays_open=True))
def test_early_self_test_with_relays_open(lab: Lab):
    t0, started = lab.charging_session()
    self_tests = [(t, p) for t, k, p in lab.imd.requests(t0) if k == 'start_self_test']
    assert self_tests and self_tests[0][1] == '250.0', self_tests
    assert lab.wait_event('SessionFinished', since=t0, timeout=90) is not None


def test_errors_during_charging(lab: Lab):
    t0, started = lab.charging_session(hold_s=40)
    time.sleep(1)
    t = lab.rec.now()
    lab.imd.raise_error('VendorWarning', 'w', 'warning')
    assert lab.wait_event('StoppingCharging', since=t, timeout=3) is None
    lab.imd.clear_error('VendorWarning', 'w')
    time.sleep(0.5)
    t = lab.rec.now()
    lab.imd.raise_error('DeviceFault', 'd', 'broken')
    assert lab.wait_event('StoppingCharging', since=t, timeout=3) is not None
    assert lab.wait_error(EVSE_INOP, 'Active', since=t, timeout=2) is not None
    assert lab.wait_event('SessionFinished', since=t, timeout=60) is not None
    t = lab.rec.now()
    lab.imd.clear_error('DeviceFault', 'd')
    assert lab.wait_error(EVSE_INOP, 'ClearedByModule', since=t, timeout=3) is not None


def test_communication_loss_during_charging(lab: Lab):
    t0, started = lab.charging_session(hold_s=40)
    time.sleep(1)
    t = lab.rec.now()
    lab.imd.comm_check_s = 0
    assert lab.wait_error(ERR + 'CommunicationFault', 'Active', since=t, timeout=8) is not None
    assert lab.wait_event('StoppingCharging', since=t, timeout=8) is not None
    lab.imd.comm_check_s = 2
    assert lab.wait_error(ERR + 'CommunicationFault', 'ClearedByModule', since=lab.rec.now(), timeout=5) is not None
    assert lab.wait_event('SessionFinished', since=t, timeout=60) is not None


@pytest.mark.everest_config_adaptions(IsolationMonitorApiStrategy(din=False))
def test_imd_as_welding_detection_voltage_source(lab: Lab):
    """With welding_detection_voltage_source: isolation_monitor the IMD keeps running
    until the welding detection phase ended and its voltage answers the EV."""
    t0, started = lab.charging_session(hold_s=8)
    assert lab.wait_event('SessionFinished', since=t0, timeout=120) is not None
    markers = [(r.t - t0, r.value()) for r in lab.rec.find(ISO_VAR + 'welding_detection', since=t0)]
    assert [m for t, m in markers] == ['Started', 'Finished'], markers
    finished_at = markers[1][0]
    stops = [t for t, k, p in lab.imd.requests(t0) if k == 'stop']
    assert stops and all(t >= finished_at - 0.5 for t in stops if t > markers[0][0]), (stops, markers)
    messages = [(r.t - t0, (r.value() or {}).get('id')) for r in lab.rec.find(ISO_VAR + 'v2g_messages', since=t0)]
    requests = [t for t, m in messages if m == 'WeldingDetectionReq']
    responses = [t for t, m in messages if m == 'WeldingDetectionRes']
    assert requests, 'the EV performed no welding detection'
    latencies = [min(x for x in responses if x > q) - q for q in requests if any(x > q for x in responses)]
    assert latencies and max(latencies) < 2.0, latencies
