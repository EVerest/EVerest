# SPDX-License-Identifier: Apache-2.0
# Copyright Pionix GmbH and Contributors to EVerest
"""over_voltage_monitor_API against its AsyncAPI specification and the
over_voltage_monitor interface, in the DC SIL with a fake over voltage monitor.

Each test starts its own EVerest from config-sil-dc.yaml with OVMSimulator
replaced by the API module; see over_voltage_monitor_api_lab.py. The device
side of the protection (the shutdown timings of IEC 61851-23:2023, 6.3.1.106)
cannot be exercised here; what is exercised is everything EvseManager does with
the API.
"""
import json
import time

import pytest
from everest.testing.core_utils.fixtures import *

from over_voltage_monitor_api_lab import (E2M, ERR, ERROR_TYPES, EVSE_INOP, M2E, MREC5, NON_FATAL_ERRORS,
                                          PLAUSIBILITY, VAR, Lab, OverVoltageMonitorApiStrategy,
                                          emergency_threshold)

pytestmark = [
    pytest.mark.everest_core_config('config-sil-dc.yaml'),
    pytest.mark.everest_config_adaptions(OverVoltageMonitorApiStrategy()),
    # every test runs the ISO 15118 SECC on the shared interface, like the DC smoke tests
    pytest.mark.xdist_group(name='ISO15118'),
    # the simulated EV occasionally drops CP to state B right after PowerDelivery
    pytest.mark.flaky(reruns=1),
]


@pytest.fixture
def lab(request, everest_core, test_controller):
    """EVerest started with the API module; the fake OVM runs next to it.

    @pytest.mark.over_voltage_monitor_api(attr=value, ...) sets attributes of
    the fake device before it starts.
    """
    bench = Lab(everest_core)
    marker = request.node.get_closest_marker('over_voltage_monitor_api')
    for name, value in (marker.kwargs if marker else {}).items():
        setattr(bench.ovm, name, value)
    test_controller.start()
    bench.start_ovm()
    bench.wait_booted()
    yield bench
    bench.stop()


def forwarded(lab: Lab, payload):
    t = lab.rec.now()
    lab.rec.publish(M2E + 'voltage_measurement_V', payload)
    r = lab.rec.wait_for(VAR + 'voltage_measurement_V', timeout=1.5, since=t)
    return r.value() if r else None


# --------------------------------------------------------------------------- boot
def test_entrypoint_and_heartbeat(lab: Lab):
    assert lab.rec.wait_for('everest_api/ready_beacon', timeout=5, since=0) is not None
    t = lab.rec.now()
    lab.rec.publish('everest_api/discover', {'headers': {'replyTo': 'test/reply/discover'}})
    reply = lab.rec.wait_for('test/reply/discover', timeout=3, since=t)
    assert reply is not None
    api = reply.json()['apis'][0]
    assert api['type'] == 'over_voltage_monitor' and api['module_id'] == 'ovm' and api['version'] == 1
    assert [k for t, k, p in lab.ovm.requests(0)] == ['stop'], 'EvseManager stops the monitor once at boot'
    time.sleep(3.1)
    ids = [r.json() for r in lab.rec.find(E2M + 'heartbeat')]
    assert ids[0] == 0 and all(b - a == 1 for a, b in zip(ids, ids[1:])), ids


@pytest.mark.over_voltage_monitor_api(comm_check_s=0)
def test_communication_check(lab: Lab):
    ready = lab.rec.find(E2M + 'heartbeat')[0].t
    fault = lab.wait_error(ERR + 'CommunicationFault', 'Active', since=0, timeout=8)
    assert fault is not None and 4.0 < fault.t - ready < 7.0
    assert fault.value()['sub_type'] == 'Initial communication check'
    assert lab.wait_error(EVSE_INOP, 'Active', since=0, timeout=2) is not None
    t = lab.rec.now()
    lab.ovm.communication_check(True)
    assert lab.wait_error(ERR + 'CommunicationFault', 'ClearedByModule', since=t, timeout=3) is not None
    assert lab.wait_error(EVSE_INOP, 'ClearedByModule', since=t, timeout=3) is not None


def test_payload_validation_and_commands(lab: Lab):
    assert forwarded(lab, '500') == 500.0
    assert forwarded(lab, '500.5') == 500.5
    assert forwarded(lab, '-5') == -5.0, 'negative values pass through'
    assert forwarded(lab, '1e3') == 1000.0
    assert abs(forwarded(lab, '123.456789') - 123.45679) < 1e-4, 'float32'
    for bad in ('"500"', '{"voltage_V": 500}', 'null', 'true', 'false'):
        assert forwarded(lab, bad) is None, bad
    expected = {
        ('set_limits', 1100.0, 900.0): {'emergency_limit_V': 1100.0, 'error_limit_V': 900.0},
        ('start',): {}, ('stop',): {}, ('reset_over_voltage_error',): {},
    }
    for key, payload in expected.items():
        command = key[0]
        args = {'emergency_over_voltage_limit_V': key[1], 'error_over_voltage_limit_V': key[2]} if len(key) > 1 else {}
        t = lab.rec.now()
        lab.call(command, args)
        r = lab.wait_request(command, since=t, timeout=2)
        assert r is not None and r.json() == payload, (command, r.payload if r else None)


def test_errors_raise_and_clear(lab: Lab):
    for error_type in ERROR_TYPES:
        fatal = error_type not in NON_FATAL_ERRORS
        t = lab.rec.now()
        lab.ovm.raise_error(error_type, 'sub', 'detail', severity='Medium')
        raised = lab.wait_error(ERR + error_type, 'Active', since=t, timeout=2)
        assert raised is not None and raised.value()['sub_type'] == 'sub' and raised.value()['message'] == 'detail'
        assert raised.value()['severity'] == 'Medium'
        assert (lab.wait_error(EVSE_INOP, 'Active', since=t, timeout=1.5) is not None) == fatal, error_type
        t = lab.rec.now()
        lab.ovm.clear_error(error_type, 'sub')
        assert lab.wait_error(ERR + error_type, 'ClearedByModule', since=t, timeout=2) is not None, error_type
        if fatal:
            assert lab.wait_error(EVSE_INOP, 'ClearedByModule', since=t, timeout=2) is not None, error_type
    for severity, expected in [(None, 'High'), ('Low', 'Low')]:
        t = lab.rec.now()
        lab.ovm.raise_error('MREC5OverVoltage', 's', 'm', severity=severity)
        raised = lab.wait_error(ERR + 'MREC5OverVoltage', 'Active', since=t, timeout=2)
        assert raised is not None and raised.value()['severity'] == expected, severity
        lab.ovm.clear_error('MREC5OverVoltage', 's')
        assert lab.wait_error(ERR + 'MREC5OverVoltage', 'ClearedByModule', since=t, timeout=2) is not None
    lab.ovm.raise_error('MREC5OverVoltage', 's', 'm', severity='Critical')
    assert lab.rec.absent(topic=ERR + 'MREC5OverVoltage', timeout=1), 'unknown severity rejects the message'
    lab.ovm.raise_error('OverVoltage')
    assert lab.rec.absent(prefix=ERR + 'OverVoltage', timeout=1)


# --------------------------------------------------------------------------- sessions
def test_dc_session_sequence(lab: Lab):
    t0, started = lab.charging_session()
    assert lab.wait_event('SessionFinished', since=t0, timeout=90) is not None
    requests = lab.ovm.requests(t0)
    limits = [(t, p) for t, k, p in requests if k == 'set_limits']
    starts = [t for t, k, p in requests if k == 'start']
    stops = [t for t, k, p in requests if k == 'stop']
    resets = [t for t, k, p in requests if k == 'reset_over_voltage_error']
    ev_max = lab.ev_maximum_voltage(t0)
    supply_max = lab.supply_maximum_voltage()
    assert ev_max is not None and supply_max is not None
    for t, payload in limits:
        assert json.loads(payload) == {
            'error_limit_V': ev_max, 'emergency_limit_V': emergency_threshold(min(ev_max, supply_max))}, payload
    assert len(starts) == 1 and limits and starts[0] > limits[0][0], 'start once, after set_limits'
    assert stops and stops[-1] > starts[0] and resets, requests
    assert resets[0] < stops[-1], 'reset on unplug, followed by the unplug stop'
    assert lab.rec.find(topic=VAR + 'voltage_measurement_V', since=t0), 'measurements forwarded'
    assert not lab.rec.find(topic=PLAUSIBILITY, since=t0)


def test_internal_watchdog_error_limit(lab: Lab):
    """A value above error_limit_V for internal_over_voltage_duration_ms (400 ms):
    evse_manager/MREC5OverVoltage Medium and an error shutdown with the CP
    oscillator off within 2.5 s (IEC 61851-23:2023 Table CC.11 t203)."""
    t0, started = lab.charging_session(hold_s=40)
    time.sleep(2)
    t = lab.rec.now()
    lab.ovm.fixed_V = lab.ovm.limits['error_limit_V'] + 10
    fault = lab.wait_error(MREC5, 'Active', since=t, timeout=5)
    assert fault is not None and fault.value()['severity'] == 'Medium', fault
    first_high = lab.rec.wait_for(VAR + 'voltage_measurement_V', since=t, timeout=0.1,
                                  pred=lambda r: r.value() > lab.ovm.limits['error_limit_V'])
    assert first_high is not None and 0.3 < fault.t - first_high.t < 1.5
    cp_off = lab.wait_cp_off(since=fault.t)
    assert cp_off is not None and cp_off.t - fault.t < 2.5
    assert lab.wait_contactor_open(since=fault.t) is not None
    assert lab.wait_event('StoppingCharging', since=t, timeout=3) is not None
    lab.ovm.fixed_V = None
    assert lab.wait_event('SessionFinished', since=t, timeout=60) is not None
    assert lab.ovm.requests(t, 'reset_over_voltage_error'), 'reset on unplug'
    assert lab.wait_error(MREC5, 'ClearedByModule', since=t, timeout=5) is not None
    assert lab.wait_hlc_error('Error_ErrorShutdown', since=t) is not None
    time.sleep(2)
    t1, started = lab.charging_session()
    assert lab.wait_event('SessionFinished', since=t1, timeout=90) is not None


def test_internal_watchdog_emergency_limit(lab: Lab):
    t0, started = lab.charging_session(hold_s=40)
    time.sleep(2)
    t = lab.rec.now()
    lab.ovm.fixed_V = lab.ovm.limits['emergency_limit_V'] + 10
    fault = lab.wait_error(MREC5, 'Active', since=t, timeout=5)
    assert fault is not None and fault.value()['severity'] == 'High'
    sample = lab.rec.wait_for(VAR + 'voltage_measurement_V', since=t, timeout=0.1,
                              pred=lambda r: r.value() >= lab.ovm.limits['emergency_limit_V'])
    assert sample is not None and fault.t - sample.t < 0.2, 'immediate'
    cp_off = lab.wait_cp_off(since=fault.t)
    assert cp_off is not None and cp_off.t - fault.t < 0.5
    assert lab.wait_hlc_error('Error_EmergencyShutdown', since=t) is not None
    lab.ovm.fixed_V = None
    assert lab.wait_event('SessionFinished', since=t, timeout=60) is not None


@pytest.mark.over_voltage_monitor_api(interval_s=0)
def test_internal_watchdog_timer_semantics(lab: Lab):
    """The 400 ms are a timer armed by one sample above the error limit and
    cancelled by a later sample below it."""
    t0, started = lab.charging_session(hold_s=40)
    time.sleep(2)
    limit = lab.ovm.limits['error_limit_V']
    t = lab.rec.now()
    lab.ovm.measurement(limit + 10)
    time.sleep(0.2)
    lab.ovm.measurement(400.0)
    assert lab.wait_error(MREC5, 'Active', since=t, timeout=2) is None, 'excursion shorter than 400 ms'
    t = lab.rec.now()
    lab.ovm.measurement(limit + 10)
    fault = lab.wait_error(MREC5, 'Active', since=t, timeout=2)
    assert fault is not None and 0.3 < fault.t - t < 1.0, 'a single sample with no later sample trips the timer'
    assert lab.wait_event('SessionFinished', since=t, timeout=60) is not None


@pytest.mark.over_voltage_monitor_api(publish_when_stopped=True, fixed_V=2000.0)
def test_high_voltage_while_stopped_is_ignored(lab: Lab):
    t = lab.rec.now()
    time.sleep(5)
    assert lab.rec.find(topic=VAR + 'voltage_measurement_V', since=t)
    assert not lab.rec.find(topic=MREC5, since=t) and not lab.rec.find(topic=EVSE_INOP, since=t)
    lab.ovm.fixed_V = None
    t0, started = lab.charging_session()
    assert lab.wait_event('SessionFinished', since=t0, timeout=60) is not None
    t = lab.rec.now()
    lab.ovm.fixed_V = 2000.0
    time.sleep(4)
    assert not lab.rec.find(topic=MREC5, since=t)


def test_device_error_shutdown(lab: Lab):
    """MREC5OverVoltage with severity Medium from the device: error shutdown,
    CP oscillator off within 2.5 s, reset on unplug, device clears, next session charges."""
    t0, started = lab.charging_session(hold_s=40)
    time.sleep(2)
    t = lab.rec.now()
    lab.ovm.raise_error('MREC5OverVoltage', '', 'device detected over voltage', severity='Medium')
    assert lab.wait_error(ERR + 'MREC5OverVoltage', 'Active', since=t, timeout=2) is not None
    assert lab.wait_event('StoppingCharging', since=t, timeout=3) is not None
    assert lab.wait_contactor_open(since=t) is not None
    cp_off = lab.wait_cp_off(since=t)
    assert cp_off is not None and cp_off.t - t < 2.5
    assert lab.wait_hlc_error('Error_ErrorShutdown', since=t) is not None
    assert lab.wait_event('SessionFinished', since=t, timeout=60) is not None
    assert lab.ovm.requests(t, 'reset_over_voltage_error')
    assert lab.wait_error(ERR + 'MREC5OverVoltage', 'ClearedByModule', since=t, timeout=5) is not None
    assert lab.wait_error(EVSE_INOP, 'ClearedByModule', since=t, timeout=3) is not None
    time.sleep(2)
    t1, started = lab.charging_session()
    assert lab.wait_event('SessionFinished', since=t1, timeout=90) is not None


def test_device_emergency_shutdown_report(lab: Lab):
    """MREC5OverVoltage with severity High reports an emergency shutdown the device
    already performed: EvseManager drops the CP to X1 at once."""
    t0, started = lab.charging_session(hold_s=40)
    time.sleep(2)
    t = lab.rec.now()
    lab.ovm.raise_error('MREC5OverVoltage', '', 'emergency shutdown performed', severity='High')
    cp_off = lab.wait_cp_off(since=t)
    assert cp_off is not None and cp_off.t - t < 0.5 and cp_off.topic.endswith('cp_state_X1')
    assert lab.wait_contactor_open(since=t) is not None
    assert lab.wait_hlc_error('Error_EmergencyShutdown', since=t) is not None
    assert lab.wait_event('SessionFinished', since=t, timeout=60) is not None
    assert lab.wait_error(ERR + 'MREC5OverVoltage', 'ClearedByModule', since=t, timeout=5) is not None


@pytest.mark.over_voltage_monitor_api(clear_on_reset=False)
def test_device_keeping_the_error_after_reset_blocks_charging(lab: Lab):
    t0, started = lab.charging_session(hold_s=40)
    time.sleep(2)
    t = lab.rec.now()
    lab.ovm.raise_error('MREC5OverVoltage', '', 'latched', severity='Medium')
    assert lab.wait_event('SessionFinished', since=t, timeout=60) is not None
    time.sleep(2)
    t1 = lab.dc_session()
    assert lab.wait_event('ChargingStarted', since=t1, timeout=40) is None
    assert lab.rec.absent(topic=EVSE_INOP, since=t1, timeout=0.1,
                          pred=lambda r: (r.value() or {}).get('state') != 'Active')
    lab.ovm.clear_error('MREC5OverVoltage')
    assert lab.wait_event('SessionFinished', since=t1, timeout=60) is not None


def test_voltage_plausibility_fault(lab: Lab):
    """An OVM voltage 80 V above the supply for voltage_plausibility_fault_duration_ms
    (30 s): evse_manager/VoltagePlausibilityFault and stop."""
    t0, started = lab.charging_session(hold_s=70)
    time.sleep(2)
    t = lab.rec.now()
    lab.ovm.offset_V = 80.0
    fault = lab.wait_error(PLAUSIBILITY, 'Active', since=t, timeout=45)
    assert fault is not None and 29.0 < fault.t - t < 40.0, fault
    assert lab.wait_event('StoppingCharging', since=t, timeout=5) is not None
    lab.ovm.offset_V = 0.0
    assert lab.wait_event('SessionFinished', since=t, timeout=90) is not None


@pytest.mark.over_voltage_monitor_api(interval_s=0)
def test_device_without_measurement(lab: Lab):
    t0, started = lab.charging_session()
    assert lab.wait_event('SessionFinished', since=t0, timeout=90) is not None
    assert not lab.rec.find(prefix=EVSE_INOP, since=t0)


def test_errors_during_charging(lab: Lab):
    t0, started = lab.charging_session(hold_s=40)
    time.sleep(1)
    t = lab.rec.now()
    lab.ovm.raise_error('VendorWarning', 'w', 'warning')
    assert lab.wait_event('StoppingCharging', since=t, timeout=3) is None
    lab.ovm.clear_error('VendorWarning', 'w')
    time.sleep(0.5)
    t = lab.rec.now()
    lab.ovm.raise_error('DeviceFault', 'd', 'broken')
    assert lab.wait_event('StoppingCharging', since=t, timeout=3) is not None
    assert lab.wait_error(EVSE_INOP, 'Active', since=t, timeout=2) is not None
    assert lab.wait_event('SessionFinished', since=t, timeout=60) is not None
    t = lab.rec.now()
    lab.ovm.clear_error('DeviceFault', 'd')
    assert lab.wait_error(EVSE_INOP, 'ClearedByModule', since=t, timeout=3) is not None


def test_communication_loss_during_charging(lab: Lab):
    t0, started = lab.charging_session(hold_s=40)
    time.sleep(1)
    t = lab.rec.now()
    lab.ovm.comm_check_s = 0
    fault = lab.wait_error(ERR + 'CommunicationFault', 'Active', since=t, timeout=8)
    assert fault is not None and fault.value()['sub_type'] == 'Bridge to implementation connection lost'
    assert lab.wait_event('StoppingCharging', since=t, timeout=8) is not None
    lab.ovm.comm_check_s = 2
    assert lab.wait_error(ERR + 'CommunicationFault', 'ClearedByModule', since=lab.rec.now(), timeout=5) is not None
    assert lab.wait_event('SessionFinished', since=t, timeout=60) is not None
