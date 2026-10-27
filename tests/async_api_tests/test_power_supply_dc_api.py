# SPDX-License-Identifier: Apache-2.0
# Copyright Pionix GmbH and Contributors to EVerest
"""power_supply_DC_API against its AsyncAPI specification and the power_supply_DC
interface guidelines, in the DC SIL with an external fake supply.

Each test starts its own EVerest from config-sil-dc.yaml with the simulated
supply replaced by the API module. The supply, the recorder and the carsim
come from power_supply_dc_api_lab.py.
"""
import time

import pytest
from everest.testing.core_utils.fixtures import *

from power_supply_dc_api_lab import (BSP_CMD, DEFAULT_CAPABILITIES, E2M, ERROR_TYPES, EVSE_ERR, ISO_CMD, ISO_VAR,
                                     M2E, MOD_ERR, MOD_VAR, REQUIRED_CAPABILITY_KEYS, Lab, PowerSupplyApiStrategy,
                                     as_float32)

pytestmark = [
    pytest.mark.everest_core_config('config-sil-dc.yaml'),
    pytest.mark.everest_config_adaptions(PowerSupplyApiStrategy()),
    # every test boots the ISO 15118 SECC on the shared interface, like the DC smoke tests
    pytest.mark.xdist_group(name='ISO15118'),
    # the simulated EV occasionally drops CP to state B right after PowerDelivery
    pytest.mark.flaky(reruns=1),
]


@pytest.fixture
def lab(request, everest_core, test_controller):
    """EVerest started with the API module, the fake supply running next to it.

    @pytest.mark.power_supply_api(attr=value, ...) sets attributes of the fake
    supply before it starts; capabilities are only published when a test asks.
    """
    bench = Lab(everest_core)
    marker = request.node.get_closest_marker('power_supply_api')
    for name, value in (marker.kwargs if marker else {}).items():
        setattr(bench.supply, name, value)
    test_controller.start()
    bench.start_supply()
    bench.wait_heartbeat()
    yield bench
    bench.stop()


def charging_session(lab: Lab, hold_s: int = 12):
    """Start a DC session and return (t0, ChargingStarted record)."""
    t0 = lab.rec.now()
    lab.carsim.dc_iso_session(hold_s)
    started = lab.wait_event('ChargingStarted', since=t0, timeout=60)
    assert started is not None, f'no ChargingStarted; requests: {lab.requests(t0)}'
    return t0, started


# --------------------------------------------------------------------------- boot
def test_entrypoint_discovery(lab: Lab):
    beacon = lab.rec.wait_for('everest_api/ready_beacon', timeout=5, since=0)
    assert beacon is not None and beacon.payload == '{}'
    assert len(lab.rec.find('everest_api/ready_beacon')) == 1

    t = lab.rec.now()
    lab.rec.publish('everest_api/discover', {'headers': {'replyTo': 'test/reply/discover'}})
    reply = lab.rec.wait_for('test/reply/discover', timeout=3, since=t)
    assert reply is not None, 'no reply to discover'
    api = reply.json()['apis'][0]
    assert api['type'] == 'power_supply_DC'
    assert api['module_id'] == 'powersupply_dc'
    assert api['version'] == 1
    assert api['communication_monitoring'] == {'heartbeat_period_ms': 1000, 'communication_check_period_s': 5}

    t = lab.rec.now()
    lab.rec.publish('everest_api/query-modules/power_supply_DC', {'headers': {'replyTo': 'test/reply/own'}})
    assert lab.rec.wait_for('test/reply/own', timeout=3, since=t) is not None
    lab.rec.publish('everest_api/query-modules/isolation_monitor', {'headers': {'replyTo': 'test/reply/foreign'}})
    assert lab.rec.absent('test/reply/foreign', timeout=2, since=t)

    # malformed requests are dropped and the module keeps serving
    lab.rec.publish('everest_api/discover', {})
    lab.rec.publish('everest_api/discover', 'garbage')
    t = lab.rec.now()
    lab.rec.publish('everest_api/discover', {'headers': {'replyTo': 'test/reply/again'}})
    assert lab.rec.wait_for('test/reply/again', timeout=3, since=t) is not None


def test_heartbeat(lab: Lab):
    first = lab.rec.find(E2M + 'heartbeat')[0]
    assert first.json() == 0
    t = lab.rec.now()
    time.sleep(5.2)
    beats = lab.rec.find(E2M + 'heartbeat', since=t)
    ids = [b.json() for b in beats]
    assert 4 <= len(beats) <= 6, ids
    assert all(isinstance(i, int) for i in ids)
    assert all(b - a == 1 for a, b in zip(ids, ids[1:])), ids
    periods = [b.t - a.t for a, b in zip(beats, beats[1:])]
    assert all(0.9 < p < 1.1 for p in periods), periods


@pytest.mark.power_supply_api(comm_check_s=0)
def test_communication_check(lab: Lab):
    ready = lab.rec.find(E2M + 'heartbeat')[0].t
    fault = lab.wait_error('CommunicationFault', 'Active', since=0, timeout=8)
    assert fault is not None, 'no CommunicationFault without communication_check'
    assert 4.0 < fault.t - ready < 7.0, f'fault {fault.t - ready:.2f} s after ready, expected after 5 s'
    assert fault.value()['sub_type'] == 'Initial communication check'
    assert lab.wait_inoperative('Active', since=0, timeout=2) is not None

    t = lab.rec.now()
    lab.supply.communication_check(True)
    assert lab.wait_error('CommunicationFault', 'ClearedByModule', since=t, timeout=3) is not None
    assert lab.wait_inoperative('ClearedByModule', since=t, timeout=3) is not None

    # non-boolean payloads neither count as a check nor break the module
    for payload in ('True', '1', '"true"'):
        lab.rec.publish(M2E + 'communication_check', payload)
    t = lab.rec.now()
    fault = lab.wait_error('CommunicationFault', 'Active', since=t, timeout=8)
    assert fault is not None, 'no fault after 5 s of silence'
    assert fault.value()['sub_type'] == 'Bridge to implementation connection lost'

    # false stops the check; it also clears the active fault
    t = lab.rec.now()
    lab.supply.communication_check(False)
    assert lab.wait_error('CommunicationFault', 'ClearedByModule', since=t, timeout=3) is not None
    assert lab.rec.absent(MOD_ERR + 'CommunicationFault', timeout=6.5, since=lab.rec.now(),
                          pred=lambda r: r.value()['state'] == 'Active')


@pytest.mark.power_supply_api(rate_hz=0)
def test_payload_validation(lab: Lab):
    def forwarded(var: str, payload) -> dict:
        t = lab.rec.now()
        lab.rec.publish(M2E + var, payload)
        record = lab.rec.wait_for(MOD_VAR + var, timeout=1.5, since=t)
        return record.value() if record else None

    required = {k: DEFAULT_CAPABILITIES[k] for k in REQUIRED_CAPABILITY_KEYS}
    assert set(forwarded('capabilities', required).keys()) == set(required.keys())
    missing = dict(required)
    del missing['max_export_power_W']
    assert forwarded('capabilities', missing) is None
    assert 'vendor_field' not in forwarded('capabilities', dict(required, vendor_field=1))
    assert forwarded('capabilities', dict(required, bidirectional='yes')) is None
    precise = forwarded('capabilities', dict(required, max_export_voltage_V=900.123456789))
    assert precise['max_export_voltage_V'] == as_float32(900.123456789)

    for payload, expected in [('"Off"', 'Off'), ('"Export"', 'Export'), ('"Import"', 'Import'), ('"Fault"', 'Fault'),
                              ('"Unknown"', None), ('"off"', None), ('Export', None), ('{"mode":"Off"}', None)]:
        assert forwarded('mode', payload) == expected, payload

    for payload, expected in [
        ('{"voltage_V":400.5,"current_A":-12.25}', {'voltage_V': 400.5, 'current_A': -12.25}),
        ('{"voltage_V":400,"current_A":20}', {'voltage_V': 400.0, 'current_A': 20.0}),
        ('{"voltage_V":400,"current_A":20,"temperature_C":40}', {'voltage_V': 400.0, 'current_A': 20.0}),
        ('{"voltage_V":400}', None),
        ('{"voltage_V":"400","current_A":"20"}', None),
        ('[400,20]', None),
    ]:
        assert forwarded('voltage_current', payload) == expected, payload
    precise = forwarded('voltage_current', '{"voltage_V":400.123456789,"current_A":0.1234567891}')
    assert precise == {'voltage_V': as_float32(400.123456789), 'current_A': as_float32(0.1234567891)}


def test_errors_raise_and_clear(lab: Lab):
    for error_type in ERROR_TYPES:
        fatal = error_type != 'VendorWarning'
        t = lab.rec.now()
        lab.supply.raise_error(error_type, 'sub', 'detail')
        raised = lab.wait_error(error_type, 'Active', since=t, timeout=2)
        assert raised is not None, error_type
        assert raised.value()['sub_type'] == 'sub' and raised.value()['message'] == 'detail'
        assert raised.value()['severity'] == 'High'
        assert (lab.wait_inoperative('Active', since=t, timeout=1.5) is not None) == fatal, error_type
        t = lab.rec.now()
        lab.supply.clear_error(error_type, 'sub')
        assert lab.wait_error(error_type, 'ClearedByModule', since=t, timeout=2) is not None, error_type
        if fatal:
            assert lab.wait_inoperative('ClearedByModule', since=t, timeout=1.5) is not None, error_type

    # sub_type and message are optional; an unknown type is rejected
    t = lab.rec.now()
    lab.supply.raise_error('HardwareFault')
    raised = lab.wait_error('HardwareFault', 'Active', since=t, timeout=2)
    assert raised is not None and raised.value()['sub_type'] == '' and raised.value()['message'] == ''
    lab.supply.raise_error('NoSuchError', 'x', 'y')
    assert lab.rec.absent(prefix=MOD_ERR + 'NoSuchError', timeout=1)

    # a second raise of the same error is ignored; clearing needs the same sub_type
    t = lab.rec.now()
    lab.supply.raise_error('HardwareFault')
    assert lab.rec.absent(MOD_ERR + 'HardwareFault', timeout=1, since=t)
    lab.supply.clear_error('HardwareFault', 'other')
    assert lab.rec.absent(MOD_ERR + 'HardwareFault', timeout=1, since=t)
    t = lab.rec.now()
    lab.supply.clear_error('HardwareFault')
    assert lab.wait_error('HardwareFault', 'ClearedByModule', since=t, timeout=2) is not None


# --------------------------------------------------------------------------- sessions
def test_dc_session_command_sequence(lab: Lab):
    lab.supply.publish_capabilities()
    t0, started = charging_session(lab)
    finished = lab.wait_event('SessionFinished', since=t0, timeout=90)
    assert finished is not None

    modes = lab.modes(t0)
    export_phases = [p['charging_phase'] for _, p in modes if p['mode'] == 'Export']
    assert export_phases == ['CableCheck', 'PreCharge', 'Charging'], modes
    assert modes[-1][1]['mode'] == 'Off', modes

    setpoints = lab.export_setpoints(t0)
    # the cable-check setpoint precedes the mode request; a supply has to keep it while Off
    assert setpoints[0][0] < modes[0][0] < setpoints[0][0] + 0.5, (setpoints[0], modes[0])
    assert setpoints[0][1]['current_A'] >= 2.0
    precharge_t = next(t for t, p in modes if p['charging_phase'] == 'PreCharge')
    precharge = [p for t, p in setpoints if t <= precharge_t + 0.5 and t >= precharge_t - 0.5]
    assert precharge and all(p['current_A'] >= 2.0 for p in precharge), precharge
    assert {'voltage_V': 400.0, 'current_A': 20.0} in [p for _, p in setpoints], setpoints

    # interface guideline 3: the supply is switched off before the contactors open
    off_t = next(t for t, p in modes if p['mode'] == 'Off' and t > started.t - t0)
    power_off = [r.t - t0 for r in lab.rec.find(BSP_CMD + 'allow_power_on', since=t0 + off_t)
                 if r.value()['value']['allow_power_on'] is False]
    assert power_off and power_off[0] > off_t

    # every measurement reaches EvseManager and, until the stop, the ISO stack
    # (during welding detection the configured voltage source reports instead)
    until = t0 + off_t
    sent = len(lab.rec.find(M2E + 'voltage_current', since=t0, pred=lambda r: r.t < until))
    internal = len(lab.rec.find(MOD_VAR + 'voltage_current', since=t0, pred=lambda r: r.t < until))
    hlc = len(lab.rec.find(ISO_CMD + 'update_dc_present_values', since=t0, pred=lambda r: r.t < until))
    assert abs(sent - internal) <= 2 and abs(sent - hlc) <= 2, (sent, internal, hlc)

    # the mode reported by the supply is forwarded
    assert [r.value() for r in lab.rec.find(MOD_VAR + 'mode', since=t0)][:2] == ['Export', 'Off']

    # the heartbeat ran through the whole session
    beats = lab.rec.find(E2M + 'heartbeat', since=t0, pred=lambda r: r.t < finished.t)
    assert len(beats) >= int(finished.t - t0) - 2, len(beats)


def test_capability_update_during_charging(lab: Lab):
    lab.supply.publish_capabilities()
    charging_session(lab, hold_s=40)
    time.sleep(2)
    t = lab.rec.now()
    lab.supply.publish_capabilities(dict(DEFAULT_CAPABILITIES, max_export_current_A=10.0))
    derated = lab.wait_e2m('export_voltage_current', since=t, timeout=2.0,
                           pred=lambda r: r.json()['current_A'] <= 10.0)
    assert derated is not None, f'derated limit not applied within 2 s: {lab.export_setpoints(t)}'
    t = lab.rec.now()
    lab.supply.publish_capabilities()
    assert lab.wait_e2m('export_voltage_current', since=t, timeout=8.0,
                        pred=lambda r: r.json()['current_A'] >= 20.0) is not None


def test_errors_during_charging(lab: Lab):
    lab.supply.publish_capabilities()
    t0, _ = charging_session(lab, hold_s=40)
    time.sleep(2)

    t = lab.rec.now()
    lab.supply.raise_error('VendorWarning', 'w', 'warning')
    assert lab.rec.absent(E2M + 'mode', timeout=3, since=t)
    assert lab.rec.absent(ISO_VAR + 'dc_open_contactor', timeout=0.5, since=t)
    lab.supply.clear_error('VendorWarning', 'w')

    t = lab.rec.now()
    lab.supply.raise_error('HardwareFault', 'h', 'broken')
    assert lab.wait_mode('Off', since=t, timeout=3) is not None
    assert lab.wait_event('StoppingCharging', since=t, timeout=3) is not None
    assert lab.wait_inoperative('Active', since=t, timeout=2) is not None
    assert lab.wait_event('SessionFinished', since=t, timeout=60) is not None

    t = lab.rec.now()
    lab.supply.clear_error('HardwareFault', 'h')
    assert lab.wait_inoperative('ClearedByModule', since=t, timeout=3) is not None


def test_communication_loss_during_charging(lab: Lab):
    lab.supply.publish_capabilities()
    charging_session(lab, hold_s=40)
    time.sleep(2)
    t = lab.rec.now()
    lab.supply.comm_check_s = 0
    fault = lab.wait_error('CommunicationFault', 'Active', since=t, timeout=8)
    assert fault is not None
    assert lab.wait_mode('Off', since=t, timeout=8) is not None
    assert lab.wait_event('StoppingCharging', since=t, timeout=8) is not None
    assert lab.wait_event('SessionFinished', since=t, timeout=60) is not None
    t = lab.rec.now()
    lab.supply.comm_check_s = 2
    assert lab.wait_error('CommunicationFault', 'ClearedByModule', since=t, timeout=5) is not None


def test_ev_target_below_supply_minimum(lab: Lab):
    """Interface guideline 5: a request below min_export_current_A means 0 A."""
    lab.supply.capabilities['min_export_current_A'] = 25.0
    lab.supply.publish_capabilities()
    t0, started = charging_session(lab)
    stop = lab.wait_mode('Off', since=started.t, timeout=60)
    assert stop is not None
    setpoints = [(t, p['current_A']) for t, p in lab.export_setpoints(t0) if t < stop.t - t0]
    assert setpoints[-1][1] == 0.0, setpoints
    demand = [current for t, current in setpoints if t > started.t - t0 + 0.5]
    assert all(current == 0.0 for current in demand), setpoints
    assert lab.wait_event('SessionFinished', since=t0, timeout=90) is not None


def test_no_capabilities_means_zero_power(lab: Lab):
    """Without capabilities EvseManager behaves as with a 0 W supply: the session runs
    through the cable check at its built-in 60 V default, but no current is requested."""
    t0 = lab.rec.now()
    lab.carsim.dc_iso_session(hold_s=5)
    assert lab.wait_event('SessionFinished', since=t0, timeout=90) is not None
    setpoints = [p for _, p in lab.export_setpoints(t0)]
    assert setpoints and all(p['current_A'] == 0.0 and p['voltage_V'] <= 60.0 for p in setpoints), setpoints
    assert lab.wait_event('ChargingStarted', since=t0, timeout=0.1) is None


@pytest.mark.power_supply_api(follow=False)
def test_supply_never_reaches_cable_check_voltage(lab: Lab):
    """The reported voltage drives the cable check; a supply stuck at 0 V ends the session."""
    lab.supply.publish_capabilities()
    t0 = lab.rec.now()
    lab.carsim.dc_iso_session(hold_s=5)
    export = lab.wait_mode('Export', since=t0, timeout=40, phase='CableCheck')
    assert export is not None
    off = lab.wait_mode('Off', since=export.t, timeout=20)
    assert off is not None and 3.0 < off.t - export.t < 20.0, off
    assert lab.wait_error('', 'Active', since=t0, timeout=5, topic=EVSE_ERR + 'MREC11CableCheckFault') is not None
    assert lab.wait_event('SessionFinished', since=t0, timeout=90) is not None


@pytest.mark.power_supply_api(rate_hz=0)
def test_supply_without_measurements_fails_cable_check(lab: Lab):
    """Interface guideline 4: measurements must come regularly. Before switching
    the supply on, EvseManager waits for a fresh measurement below 60 V."""
    lab.supply.publish_capabilities()
    t0 = lab.rec.now()
    lab.carsim.dc_iso_session(hold_s=5)
    assert lab.wait_error('', 'Active', since=t0, timeout=60, topic=EVSE_ERR + 'MREC11CableCheckFault') is not None
    assert lab.wait_event('SessionFinished', since=t0, timeout=90) is not None
    assert not [p for _, p in lab.modes(t0) if p['mode'] == 'Export'], lab.requests(t0)


@pytest.mark.everest_config_adaptions(PowerSupplyApiStrategy(din=False))
@pytest.mark.parametrize('rate_hz, max_response_s', [
    pytest.param(4.0, 0.8, id='4hz'),
    pytest.param(1.0, 2.0, id='1hz'),
])
def test_welding_detection_measurements(lab: Lab, rate_hz, max_response_s):
    """Each WeldingDetectionRes waits for two measurements taken after the request
    (ISO 15118-2 session, the supply is the welding-detection voltage source)."""
    lab.supply.rate_hz = rate_hz
    lab.supply.publish_capabilities()
    t0, _ = charging_session(lab, hold_s=8)
    assert lab.wait_event('SessionFinished', since=t0, timeout=120) is not None
    markers = [r.value() for r in lab.rec.find(ISO_VAR + 'welding_detection', since=t0)]
    assert markers == ['Started', 'Finished'], markers
    requests = lab.v2g_message_times('WeldingDetectionReq', t0)
    responses = lab.v2g_message_times('WeldingDetectionRes', t0)
    assert requests, 'the EV performed no welding detection'
    latencies = [min(r for r in responses if r > req) - req for req in requests
                 if any(r > req for r in responses)]
    assert latencies and max(latencies) <= max_response_s, latencies
