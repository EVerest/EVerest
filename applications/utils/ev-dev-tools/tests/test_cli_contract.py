# SPDX-License-Identifier: Apache-2.0
# Copyright Pionix GmbH and Contributors to EVerest
"""Acceptance gate for ev-cli's command line contract.

EVerest's CMake and Bazel builds drive ev-cli as a black box.  This module
pins down what they rely on -- the exact set of produced files, the shape of
``--version``, the ``get-templates`` output, and the developer-facing
``module create`` / ``module update`` workflow -- so that reimplementing the
generator cannot silently change any of it.

The command under test is taken from the ``EV_CLI`` environment variable and
defaults to ``ev-cli``, so the same suite can be pointed at any build.
"""

from __future__ import annotations

import json
import os
import re
import shlex
import subprocess
import uuid
from pathlib import Path

import pytest
import yaml

EV_CLI = shlex.split(os.environ.get('EV_CLI', 'ev-cli'))

# tests/ -> ev-dev-tools/ -> utils/ -> applications/ -> repository root
EVEREST_DIR = Path(os.environ.get('EVEREST_CORE', Path(__file__).resolve().parents[4]))
SCHEMAS_DIR = EVEREST_DIR / 'lib' / 'everest' / 'framework' / 'schemas'

pytestmark = pytest.mark.skipif(
    not SCHEMAS_DIR.is_dir(),
    reason=f'no everest-core checkout at {EVEREST_DIR} (set EVEREST_CORE)',
)


def run(*args: str, cwd: Path | None = None, check: bool = True) -> subprocess.CompletedProcess:
    """Invoke ev-cli, failing the test on an unexpected exit code."""
    proc = subprocess.run(
        [*EV_CLI, *args],
        cwd=cwd or EVEREST_DIR,
        capture_output=True,
        encoding='utf-8',
    )
    if check and proc.returncode != 0:
        pytest.fail(
            f'ev-cli {" ".join(args)} exited {proc.returncode}\n'
            f'--- stdout ---\n{proc.stdout}\n--- stderr ---\n{proc.stderr}'
        )
    return proc


def common_args(work_dir: Path | None = None) -> list[str]:
    return [
        '--disable-clang-format',
        '--schemas-dir', str(SCHEMAS_DIR),
        '--everest-dir', str(EVEREST_DIR),
        '--work-dir', str(work_dir or EVEREST_DIR),
    ]


def relative_files(root: Path) -> set[str]:
    return {p.relative_to(root).as_posix() for p in root.rglob('*') if p.is_file()}


# --------------------------------------------------------------------------
# --version
# --------------------------------------------------------------------------

def test_version_is_parseable_by_cmake():
    """cmake/ev-cli.cmake strips the ``ev-cli `` prefix and compares versions."""
    out = run('--version').stdout.strip()
    assert re.fullmatch(r'ev-cli \d+\.\d+\.\d+', out), out
    # this is literally what require_ev_cli_version() does
    assert out.replace('ev-cli ', '').count('.') == 2


# --------------------------------------------------------------------------
# get-templates
# --------------------------------------------------------------------------

@pytest.mark.parametrize('command', ['types', 'interface', 'module'])
@pytest.mark.parametrize('separator', [';', '\n'])
def test_get_templates_lists_existing_template_files(command, separator):
    """CMake stores the result as a target property used as DEPENDS."""
    out = run(command, 'get-templates', f'--separator={separator}',
              '--schemas-dir', str(SCHEMAS_DIR)).stdout.strip()
    paths = [Path(p) for p in out.split(separator) if p]
    assert paths, f'{command} get-templates returned nothing'
    for path in paths:
        assert path.is_file(), f'{path} does not exist'


def test_get_templates_default_separator_is_newline():
    out = run('module', 'get-templates', '--schemas-dir', str(SCHEMAS_DIR)).stdout.strip()
    assert all(Path(line).is_file() for line in out.splitlines() if line)


# --------------------------------------------------------------------------
# types generate-headers
# --------------------------------------------------------------------------

def test_types_generate_headers_produces_exactly_one_header_per_type_file(tmp_path):
    out_dir = tmp_path / 'types'
    run('types', 'generate-headers', *common_args(), '--output-dir', str(out_dir))

    expected = {f'{p.stem}.hpp' for p in (EVEREST_DIR / 'types').glob('*.yaml')}
    assert relative_files(out_dir) == expected


def test_types_generate_headers_accepts_short_options(tmp_path):
    out_dir = tmp_path / 'types'
    run('ty', 'gh', '--disable-clang-format',
        '-sd', str(SCHEMAS_DIR), '-ed', str(EVEREST_DIR), '-wd', str(EVEREST_DIR),
        '-o', str(out_dir))
    assert relative_files(out_dir)


# --------------------------------------------------------------------------
# interface generate-headers
# --------------------------------------------------------------------------

def test_interface_generate_headers_produces_the_three_declared_outputs(tmp_path):
    """interfaces/BUILD.bazel declares all three per interface as genrule outputs."""
    out_dir = tmp_path / 'interfaces'
    run('interface', 'generate-headers', *common_args(), '--output-dir', str(out_dir))

    expected = {
        f'{p.stem}/{name}'
        for p in (EVEREST_DIR / 'interfaces').glob('*.yaml')
        for name in ('Implementation.hpp', 'Interface.hpp', 'Types.hpp')
    }
    assert relative_files(out_dir) == expected


def test_interface_generate_headers_for_a_single_named_interface(tmp_path):
    out_dir = tmp_path / 'interfaces'
    name = sorted(p.stem for p in (EVEREST_DIR / 'interfaces').glob('*.yaml'))[0]
    run('interface', 'generate-headers', *common_args(), '--output-dir', str(out_dir), name)

    assert relative_files(out_dir) == {
        f'{name}/Implementation.hpp', f'{name}/Interface.hpp', f'{name}/Types.hpp'
    }


# --------------------------------------------------------------------------
# module generate-loader
# --------------------------------------------------------------------------

def module_paths() -> list[str]:
    return sorted(
        p.parent.relative_to(EVEREST_DIR / 'modules').as_posix()
        for p in (EVEREST_DIR / 'modules').rglob('manifest.yaml')
    )


def test_module_generate_loader_produces_ld_ev_pair(tmp_path):
    out_dir = tmp_path / 'modules'
    rel = module_paths()[0]
    run('module', 'generate-loader', *common_args(), '--output-dir', str(out_dir), rel)

    name = rel.rpartition('/')[2]
    assert relative_files(out_dir) == {f'{name}/ld-ev.hpp', f'{name}/ld-ev.cpp'}


def test_module_generate_loader_works_for_every_module(tmp_path):
    """118 modules; a manifest the generator cannot read breaks the whole build."""
    out_dir = tmp_path / 'modules'
    failures = []
    for rel in module_paths():
        proc = run('module', 'generate-loader', *common_args(),
                   '--output-dir', str(out_dir), rel, check=False)
        if proc.returncode != 0:
            failures.append((rel, proc.stdout[-400:], proc.stderr[-400:]))
    assert not failures, f'{len(failures)} modules failed: {failures[:3]}'


# --------------------------------------------------------------------------
# helpers
# --------------------------------------------------------------------------

def test_helpers_generate_uuids_prints_the_requested_count():
    out = run('helpers', 'generate-uuids', '5').stdout.split()
    assert len(out) == 5
    for value in out:
        uuid.UUID(value)


def test_helpers_generate_uuids_rejects_zero():
    assert run('helpers', 'generate-uuids', '0', check=False).returncode != 0


def test_helpers_yaml_json_round_trip(tmp_path):
    payload = {'description': 'round trip', 'nested': {'list': [1, 2, 3], 'flag': True}}
    src = tmp_path / 'in.yaml'
    src.write_text(yaml.safe_dump(payload))

    as_json = tmp_path / 'out.json'
    run('helpers', 'yaml2json', str(src), str(as_json))
    assert json.loads(as_json.read_text()) == payload

    back = tmp_path / 'back.yaml'
    run('helpers', 'json2yaml', str(as_json), str(back))
    assert yaml.safe_load(back.read_text()) == payload


# --------------------------------------------------------------------------
# module create / update -- the documented developer workflow
# --------------------------------------------------------------------------

MANIFEST = """\
description: Example module used by the ev-cli contract tests
config:
  a_string:
    description: a string configuration value
    type: string
    default: hello
provides:
  main:
    description: An implementation used for contract testing
    interface: {interface}
metadata:
  license: https://opensource.org/licenses/Apache-2.0
  authors:
    - ev-cli contract tests
"""


@pytest.fixture
def scratch_module(tmp_path):
    """A work-dir holding a single module named Example."""
    interface = sorted(p.stem for p in (EVEREST_DIR / 'interfaces').glob('*.yaml'))[0]
    module_dir = tmp_path / 'modules' / 'Example'
    module_dir.mkdir(parents=True)
    (module_dir / 'manifest.yaml').write_text(MANIFEST.format(interface=interface))
    return tmp_path, module_dir, interface


def test_module_create_produces_the_documented_file_set(scratch_module):
    work_dir, module_dir, interface = scratch_module
    run('module', 'create', 'Example', *common_args(work_dir))

    assert relative_files(module_dir) == {
        'manifest.yaml',
        'CMakeLists.txt',
        'Example.hpp',
        'Example.cpp',
        f'main/{interface}Impl.hpp',
        f'main/{interface}Impl.cpp',
        'docs/index.rst',
    }


def test_module_create_only_which_lists_the_file_filters(scratch_module):
    work_dir, module_dir, _ = scratch_module
    out = run('module', 'create', 'Example', *common_args(work_dir), '--only', 'which').stdout

    for category in ('core', 'interfaces', 'docs'):
        assert f'category "{category}"' in out
    for abbr in ('cmakelists', 'module.hpp', 'module.cpp', 'main.hpp', 'main.cpp', 'index.rst'):
        assert abbr in out
    # "which" must not touch the filesystem
    assert relative_files(module_dir) == {'manifest.yaml'}


def test_module_create_only_filters_to_the_named_files(scratch_module):
    work_dir, module_dir, _ = scratch_module
    run('module', 'create', 'Example', *common_args(work_dir), '--only', 'cmakelists,module.hpp')

    assert relative_files(module_dir) == {'manifest.yaml', 'CMakeLists.txt', 'Example.hpp'}


def test_module_create_only_rejects_an_unknown_filter(scratch_module):
    work_dir, module_dir, _ = scratch_module
    run('module', 'create', 'Example', *common_args(work_dir), '--only', 'nonsense')

    assert relative_files(module_dir) == {'manifest.yaml'}


def test_module_create_diff_writes_nothing(scratch_module):
    work_dir, module_dir, _ = scratch_module
    run('module', 'create', 'Example', *common_args(work_dir), '--diff')

    assert relative_files(module_dir) == {'manifest.yaml'}


def test_module_create_does_not_overwrite_without_force(scratch_module):
    work_dir, module_dir, _ = scratch_module
    run('module', 'create', 'Example', *common_args(work_dir))

    marker = '// a human wrote this\n'
    (module_dir / 'Example.cpp').write_text(marker)
    run('module', 'create', 'Example', *common_args(work_dir))
    assert (module_dir / 'Example.cpp').read_text() == marker

    run('module', 'create', 'Example', *common_args(work_dir), '--force')
    assert (module_dir / 'Example.cpp').read_text() != marker


def test_module_update_preserves_code_inside_ev_blocks(scratch_module):
    """The ev@<uuid>:v1 markers are the contract that protects hand-written code."""
    work_dir, module_dir, _ = scratch_module
    run('module', 'create', 'Example', *common_args(work_dir))

    header = module_dir / 'Example.hpp'
    original = header.read_text()
    marker = re.search(r'^(\s*)// (ev@[0-9a-f-]+:v1)$', original, re.MULTILINE)
    assert marker, 'no ev@ block found in the generated module header'

    sentinel = 'int a_field_a_human_added{42};'
    tag = marker.group(2)
    patched = original.replace(
        f'// {tag}\n', f'// {tag}\n    {sentinel}\n', 1
    )
    header.write_text(patched)

    run('module', 'update', 'Example', *common_args(work_dir), '--force')
    assert sentinel in header.read_text()


def test_module_update_does_not_overwrite_the_module_cpp(scratch_module):
    work_dir, module_dir, _ = scratch_module
    run('module', 'create', 'Example', *common_args(work_dir))

    marker = '// human-owned implementation\n'
    (module_dir / 'Example.cpp').write_text(marker)
    run('module', 'update', 'Example', *common_args(work_dir), '--force')
    assert (module_dir / 'Example.cpp').read_text() == marker


# --------------------------------------------------------------------------
# argument surface
# --------------------------------------------------------------------------

@pytest.mark.parametrize('argv', [
    ('nonsense',),
    ('types',),                      # a subcommand is required
    ('module',),
    ('interface',),
    ('helpers',),
    ('types', 'nonsense'),
])
def test_invalid_invocations_exit_nonzero(argv):
    assert run(*argv, check=False).returncode != 0


@pytest.mark.parametrize('argv', [
    ('--help',),
    ('module', '--help'),
    ('mod', 'create', '--help'),
    ('if', 'generate-headers', '--help'),
    ('ty', 'gh', '--help'),
    ('hlp', '--help'),
])
def test_help_is_available_at_every_level(argv):
    assert 'usage' in run(*argv).stdout.lower()
