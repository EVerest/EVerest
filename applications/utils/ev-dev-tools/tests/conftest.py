# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Pionix GmbH and Contributors to EVerest

import os
import shutil
import subprocess
import sys
from pathlib import Path

import pytest
import yaml

REPO_ROOT = Path(__file__).resolve().parents[4]
SCHEMAS_DIR = REPO_ROOT / 'lib' / 'everest' / 'framework' / 'schemas'
EV_CLI_SRC = Path(__file__).resolve().parents[1] / 'src'

TYPES = {
    'description': 'Types referenced by telemetry tests',
    'types': {
        'Mode': {'description': 'Operating mode', 'type': 'string', 'enum': ['Idle', 'Running', 'Fault']},
        'Label': {'description': 'Free text', 'type': 'string'},
        'Diagnostics': {
            'description': 'Diagnostic snapshot',
            'type': 'object',
            'additionalProperties': False,
            'properties': {
                'code': {'description': 'Diagnostic code', 'type': 'integer'},
            },
        },
    },
}

TELEMETRY = {
    'temperature': {'kind': 'gauge', 'type': 'number', 'unit': 'Celsius', 'description': 'Board temperature'},
    'plug_ins': {'kind': 'counter', 'type': 'integer', 'description': 'Plug-in events'},
    'fw_state': {'kind': 'state', 'type': 'string', 'enum': ['Idle', 'Measuring', 'Error'],
                 'description': 'Firmware state'},
    'mode': {'kind': 'state', 'type': 'string', '$ref': '/tel_test#/Mode', 'description': 'Operating mode'},
    'relay_closed': {'kind': 'state', 'type': 'boolean', 'description': 'Relay state'},
    'fw_version': {'kind': 'state', 'type': 'string', 'description': 'Firmware version'},
    'diagnostics': {'kind': 'event', 'type': 'object', '$ref': '/tel_test#/Diagnostics',
                    'description': 'Diagnostic event'},
}


def make_manifest(telemetry=None):
    manifest = {
        'description': 'Telemetry test module',
        'provides': {'main': {'interface': 'empty', 'description': 'Main implementation'}},
        'metadata': {'license': 'https://opensource.org/licenses/Apache-2.0', 'authors': ['EVerest']},
    }
    if telemetry is not None:
        manifest['telemetry'] = telemetry
    return manifest


@pytest.fixture
def everest_dir(tmp_path):
    """A minimal everest project directory with a test type file and the empty interface."""
    (tmp_path / 'types').mkdir()
    (tmp_path / 'types' / 'tel_test.yaml').write_text(yaml.safe_dump(TYPES))
    (tmp_path / 'interfaces').mkdir()
    shutil.copy(REPO_ROOT / 'interfaces' / 'empty.yaml', tmp_path / 'interfaces' / 'empty.yaml')
    (tmp_path / 'modules' / 'Test').mkdir(parents=True)
    return tmp_path


def add_module(everest_dir: Path, name: str, manifest: dict) -> str:
    module_dir = everest_dir / 'modules' / 'Test' / name
    module_dir.mkdir(parents=True, exist_ok=True)
    (module_dir / 'manifest.yaml').write_text(yaml.safe_dump(manifest, sort_keys=False))
    return f'Test/{name}'


def run_ev_cli(everest_dir: Path, *args: str) -> subprocess.CompletedProcess:
    env = dict(os.environ)
    env['PYTHONPATH'] = str(EV_CLI_SRC)
    return subprocess.run(
        [sys.executable, '-m', 'ev_cli.ev', *args],
        cwd=everest_dir, env=env, capture_output=True, text=True)


@pytest.fixture
def parser_setup(everest_dir):
    """Configure the ev-cli type parser globals the way ev-cli main() does."""
    sys.path.insert(0, str(EV_CLI_SRC))
    from ev_cli import helpers
    from ev_cli.type_parsing import TypeParser

    validators = helpers.load_validators(SCHEMAS_DIR)
    previous_dirs = list(helpers.everest_dirs)
    helpers.everest_dirs[:] = [everest_dir]
    TypeParser.validators = validators
    TypeParser.all_types = {}
    TypeParser.validated_type_defs = {}
    yield everest_dir
    helpers.everest_dirs[:] = previous_dirs
