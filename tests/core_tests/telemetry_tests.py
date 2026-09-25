# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Pionix GmbH and Contributors to EVerest
"""End-to-end tests of manifest-declared telemetry.

TelemetryExample publishes telemetry, TelemetryRouter receives it on the socket the manager hands over and
writes it through its ocpp requirement, which the probe module implements.
"""

import asyncio
import subprocess
import threading
import uuid
from copy import deepcopy
from pathlib import Path
from tempfile import mkdtemp
from typing import Dict

import pytest
import yaml

from everest.testing.core_utils.everest_core import EverestCore
from everest.testing.core_utils.fixtures import *
from everest.testing.core_utils.probe_module import ProbeModule
from everest.testing.core_utils import EverestConfigAdjustmentStrategy

RULES = {
    'version': 1,
    'sinks': {
        'csms': {'type': 'ocpp', 'flush_interval_ms': 100, 'min_interval_s': 0},
        'debug': {'type': 'log', 'max_per_second': 5},
    },
    'default_action': 'drop',
    'rules': [
        {
            'name': 'example-to-csms',
            'match': {'module_type': 'TelemetryExample', 'kind': ['gauge', 'counter', 'state']},
            'forward': [
                {'sink': 'csms',
                 'component': {'name': '${module_type}', 'instance': '${module_id}'},
                 'variable': {'name': '${element}'}},
                {'sink': 'debug'},
            ],
        },
    ],
}


class TelemetryConfigurationStrategy(EverestConfigAdjustmentStrategy):
    """Writes the rules file and uses a socket path of its own, so that test runs do not collide."""

    def __init__(self):
        self.directory = Path(mkdtemp(prefix='everest_telemetry_test_'))
        self.rules_file = self.directory / 'rules.yaml'
        self.rules_file.write_text(yaml.safe_dump(RULES))

    def adjust_everest_configuration(self, everest_config: Dict) -> Dict:
        adjusted_config = deepcopy(everest_config)
        adjusted_config['active_modules']['telemetry_router']['config_module']['rules_file'] = str(self.rules_file)
        adjusted_config.setdefault('settings', {})['telemetry_socket_path'] = str(
            self.directory / f'{uuid.uuid4().hex[:8]}.sock')
        return adjusted_config


class SetVariablesRecorder:
    def __init__(self):
        self.lock = threading.Lock()
        self.requests = []

    def set_variables(self, args: dict) -> list:
        with self.lock:
            self.requests.extend(args['requests'])
        return [{'status': 'Accepted', 'component_variable': request['component_variable']}
                for request in args['requests']]

    def values_by_variable(self) -> Dict[str, list]:
        with self.lock:
            result = {}
            for request in self.requests:
                result.setdefault(request['component_variable']['variable']['name'], []).append(request)
            return result


@pytest.mark.asyncio
@pytest.mark.everest_core_config('config-test-telemetry.yaml')
@pytest.mark.everest_config_adaptions(TelemetryConfigurationStrategy())
async def test_telemetry_reaches_the_ocpp_device_model(everest_core: EverestCore):
    everest_core.start(standalone_module='probe')

    recorder = SetVariablesRecorder()
    probe_module = ProbeModule(everest_core.get_runtime_session())
    probe_module.implement_command('ProbeModuleOcpp', 'set_variables', recorder.set_variables)
    probe_module.implement_command('ProbeModuleOcpp', 'stop', lambda arg: True)
    probe_module.implement_command('ProbeModuleOcpp', 'restart', lambda arg: True)
    probe_module.implement_command('ProbeModuleOcpp', 'security_event', lambda arg: None)
    probe_module.implement_command('ProbeModuleOcpp', 'get_variables', lambda arg: [])
    probe_module.implement_command('ProbeModuleOcpp', 'change_availability', lambda arg: {'status': 'Accepted'})
    probe_module.implement_command('ProbeModuleOcpp', 'monitor_variables', lambda arg: None)
    probe_module.implement_command('ProbeModuleOcpp', 'monitor_and_get_variables', lambda arg: [])
    probe_module.start()
    await probe_module.wait_to_be_ready()

    expected = {'temperature', 'supply_voltage_raw', 'plug_ins', 'energy_delivered', 'firmware_state',
                'charge_mode', 'relay_closed', 'firmware_version'}
    for _ in range(100):
        if expected <= set(recorder.values_by_variable()):
            break
        await asyncio.sleep(0.1)

    by_variable = recorder.values_by_variable()
    assert expected <= set(by_variable), f'missing variables: {expected - set(by_variable)}'
    assert 'cp_event' not in by_variable, 'events cannot be written to the device model'

    first = by_variable['temperature'][0]
    component = first['component_variable']['component']
    assert component['name'] == 'TelemetryExample'
    assert component['instance'] == 'telemetry_example'
    assert component['evse']['id'] == 1
    assert first['attribute_type'] == 'Actual'

    assert 25.0 <= float(first['value']) <= 45.0
    assert int(by_variable['plug_ins'][0]['value']) >= 1
    assert by_variable['firmware_state'][0]['value'] in ('Idle', 'Measuring')
    assert by_variable['charge_mode'][0]['value'] == 'AC'
    assert by_variable['relay_closed'][0]['value'] in ('true', 'false')
    # published in init(), before TelemetryRouter was reading the socket
    assert by_variable['firmware_version'][0]['value'] == '1.4.2'


@pytest.mark.everest_core_config('config-test-telemetry.yaml')
def test_telemetry_without_receiver_is_rejected(core_config):
    config = yaml.safe_load(core_config.template_everest_config_path.read_text())
    config['active_modules'] = {'telemetry_example': config['active_modules']['telemetry_example']}
    config_file = Path(mkdtemp(prefix='everest_telemetry_test_')) / 'config-no-receiver.yaml'
    config_file.write_text(yaml.safe_dump(config))

    manager = core_config.everest_core_path / 'bin' / 'manager'
    result = subprocess.run([str(manager), '--prefix', str(core_config.everest_core_path), '--config',
                             str(config_file), '--check'], capture_output=True, text=True, timeout=60)
    assert result.returncode != 0
    assert 'telemetry_receiver' in result.stdout + result.stderr
