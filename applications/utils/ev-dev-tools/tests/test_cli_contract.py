# SPDX-License-Identifier: Apache-2.0
# Copyright Pionix GmbH and Contributors to EVerest
"""Acceptance gate for ev-cli's command line contract.

EVerest's CMake and Bazel builds drive ev-cli as a black box.  This module
pins down what they rely on, so that reimplementing the generator cannot
silently change any of it: the exact set of produced files, the shape of
``--version``, the ``get-templates`` output, and the developer-facing
``module create`` / ``module update`` workflow.

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

from conftest import EVEREST_DIR, SCHEMAS_DIR, needs_everest

EV_CLI = shlex.split(os.environ.get('EV_CLI', 'ev-cli'))

pytestmark = needs_everest


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

def test_get_templates_lists_existing_template_files():
    """CMake stores the result as a target property used as DEPENDS."""
    for command in ('types', 'interface', 'module'):
        for separator in (';', '\n'):
            out = run(command, 'get-templates', f'--separator={separator}',
                      '--schemas-dir', str(SCHEMAS_DIR)).stdout.strip()
            paths = [Path(p) for p in out.split(separator) if p]
            assert paths, f'{command} get-templates returned nothing'
            assert [path for path in paths if not path.is_file()] == []


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
    """A manifest the generator cannot read would break the whole build."""
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
# module create / update: the documented developer workflow
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
    for only_key in ('cmakelists', 'module.hpp', 'module.cpp', 'main.hpp', 'main.cpp', 'index.rst'):
        assert only_key in out
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


def test_module_create_and_update_accept_a_backend(scratch_module):
    """They emit code, so they pick a target like every other emitting command."""
    work_dir, _, _ = scratch_module
    for action in ('create', 'update'):
        run('module', action, 'Example', *common_args(work_dir), '--backend', 'cpp')


def test_module_create_can_target_another_backend(scratch_module):
    """The backend seam has to hold for the scaffolding commands too."""
    work_dir, module_dir, _ = scratch_module
    run('module', 'create', 'Example', *common_args(work_dir), '--backend', 'ir-dump')

    produced = relative_files(module_dir)
    assert any(name.endswith('.ir.json') for name in produced), produced


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

def test_invalid_invocations_exit_nonzero():
    invocations = [
        ('nonsense',),
        ('types',),                      # a subcommand is required
        ('module',),
        ('interface',),
        ('helpers',),
        ('types', 'nonsense'),
    ]
    assert [argv for argv in invocations if run(*argv, check=False).returncode == 0] == []


def test_missing_everest_dir_fails(tmp_path):
    """A root that is not there must stop the run, not empty the search path."""
    proc = run(
        'interface', 'generate-headers',
        '--disable-clang-format',
        '--schemas-dir', str(SCHEMAS_DIR),
        '--everest-dir', str(tmp_path / 'absent'),
        '--output-dir', str(tmp_path / 'out'),
        check=False,
    )
    assert proc.returncode != 0
    assert 'absent' in proc.stderr
    assert not (tmp_path / 'out').exists()


def test_type_units_are_read_only_from_the_types_directory(tmp_path):
    """A file in a subdirectory of ``types`` is not a type unit."""
    unit = (
        'description: A unit\n'
        'types:\n'
        '  Thing:\n'
        '    description: A thing\n'
        '    type: string\n'
        '    enum: [a]\n'
    )
    (tmp_path / 'types' / 'sub').mkdir(parents=True)
    (tmp_path / 'types' / 'top.yaml').write_text(unit)
    (tmp_path / 'types' / 'sub' / 'nested.yaml').write_text(unit)
    run(
        'types', 'generate-headers',
        '--disable-clang-format',
        '--schemas-dir', str(SCHEMAS_DIR),
        '--everest-dir', str(tmp_path),
        '--output-dir', str(tmp_path / 'out'),
    )
    assert relative_files(tmp_path / 'out') == {'top.hpp'}


def test_a_definition_without_key_value_entries_is_explained(tmp_path):
    explanations = {
        '': 'has no content',
        '# only a comment\n': 'has no content',
        '- description: x\n': 'holds a list',
        'description\n': 'holds a single value',
    }
    (tmp_path / 'types').mkdir()
    unexplained = []
    for content, explanation in explanations.items():
        (tmp_path / 'types' / 'odd.yaml').write_text(content)
        proc = run(
            'types', 'generate-headers',
            '--disable-clang-format',
            '--schemas-dir', str(SCHEMAS_DIR),
            '--everest-dir', str(tmp_path),
            '--output-dir', str(tmp_path / 'out'),
            check=False,
        )
        if proc.returncode == 0 or explanation not in proc.stderr:
            unexplained.append(content)
    assert unexplained == []


def test_missing_work_dir_fails_before_writing(tmp_path):
    """Output defaults to a directory beneath it, which must not be created."""
    work_dir = tmp_path / 'absent'
    proc = run(
        'types', 'generate-headers',
        '--disable-clang-format',
        '--schemas-dir', str(SCHEMAS_DIR),
        '--everest-dir', str(EVEREST_DIR),
        '--work-dir', str(work_dir),
        check=False,
    )
    assert proc.returncode != 0
    assert 'absent' in proc.stderr
    assert not work_dir.exists()


def test_an_interface_payload_must_be_a_valid_schema(tmp_path):
    """The interface meta-schema cannot check its embedded payload schemas."""
    payloads = {
        '#/vars/broken': 'vars:\n  broken:\n    description: A var\n    type: notatype\n',
        '#/cmds/run/arguments/broken': (
            'cmds:\n  run:\n    description: A command\n    arguments:\n      broken:\n'
            '        description: An argument\n        type: notatype\n'),
        '#/cmds/run/result': (
            'cmds:\n  run:\n    description: A command\n    result:\n'
            '      description: A result\n      type: notatype\n'),
    }
    (tmp_path / 'interfaces').mkdir()
    unreported = []
    for pointer, payload in payloads.items():
        (tmp_path / 'interfaces' / 'odd.yaml').write_text('description: An interface\n' + payload)
        proc = run(
            'interface', 'generate-headers', 'odd',
            '--disable-clang-format',
            '--schemas-dir', str(SCHEMAS_DIR),
            '--everest-dir', str(tmp_path),
            '--output-dir', str(tmp_path / 'out'),
            check=False,
        )
        if proc.returncode == 0 or f'odd.yaml{pointer}: not a valid schema' not in proc.stderr:
            unreported.append(pointer)
    assert unreported == []


def test_generating_every_interface_fails_on_a_broken_one(tmp_path):
    """No interface is skipped: one that does not load fails the run, with why."""
    (tmp_path / 'interfaces').mkdir()
    (tmp_path / 'interfaces' / 'good.yaml').write_text(
        'description: A fine interface\nvars:\n  ok:\n    description: A var\n    type: integer\n')
    (tmp_path / 'interfaces' / 'broken.yaml').write_text('description: A broken interface\nvars:\n')
    proc = run(
        'interface', 'generate-headers',
        '--disable-clang-format',
        '--schemas-dir', str(SCHEMAS_DIR),
        '--everest-dir', str(tmp_path),
        '--output-dir', str(tmp_path / 'out'),
        check=False,
    )
    assert proc.returncode != 0
    assert "broken.yaml#/vars: None is not of type 'object'" in proc.stderr


def test_inline_payload_properties_are_warned_about(tmp_path):
    (tmp_path / 'interfaces').mkdir()
    (tmp_path / 'interfaces' / 'inline.yaml').write_text(
        'description: An interface\nvars:\n  reading:\n    description: An object\n'
        '    type: object\n    properties:\n      value:\n        description: A member\n'
        '        type: integer\n')
    proc = run(
        'interface', 'generate-headers', 'inline',
        '--disable-clang-format',
        '--schemas-dir', str(SCHEMAS_DIR),
        '--everest-dir', str(tmp_path),
        '--output-dir', str(tmp_path / 'out'),
    )
    assert 'ev-cli: warning: ' in proc.stderr
    assert 'inline.yaml#/vars/reading' in proc.stderr
    assert 'the properties have no effect' in proc.stderr


def test_conformance_report_sees_every_reference_and_document(tmp_path):
    """Including a $ref the IR passes through untouched, and module manifests."""
    (tmp_path / 'types').mkdir()
    (tmp_path / 'types' / 'units.yaml').write_text(
        'description: Units\ntypes:\n'
        '  Power:\n    description: Power\n    type: object\n    properties:\n'
        '      watts:\n        description: Watts\n        type: number\n')
    (tmp_path / 'interfaces').mkdir()
    (tmp_path / 'interfaces' / 'hidden.yaml').write_text(
        'description: An interface\nvars:\n'
        '  reading:\n    description: Passed through as raw JSON\n    type: object\n'
        '    properties:\n      power:\n        description: Nested inside it\n'
        '        type: object\n        $ref: /units#/Power\n')
    (tmp_path / 'modules' / 'Sample').mkdir(parents=True)
    (tmp_path / 'modules' / 'Sample' / 'manifest.yaml').write_text(
        'description: A module\nmetadata:\n  license: https://opensource.org/licenses/Apache-2.0\n'
        '  authors:\n    - ev-cli tests\n')
    proc = run(
        'conformance-report', '--verbose',
        '--schemas-dir', str(SCHEMAS_DIR),
        '--everest-dir', str(tmp_path),
        '--work-dir', str(tmp_path),
    )
    assert 'legacy-ref: 1' in proc.stdout
    assert 'ref-sibling-type: 1' in proc.stdout
    assert 'hidden.yaml#/vars/reading/properties/power' in proc.stdout
    assert 'modules/Sample/manifest.yaml' in proc.stdout


def test_generation_does_not_summarise_conformance(tmp_path):
    """The legacy dialect is the conformance report's business, not every build's."""
    proc = run('interface', 'generate-headers', *common_args(), '--output-dir', str(tmp_path))
    assert 'note:' not in proc.stderr


def test_cpp_output_defaults_beneath_the_work_directory(tmp_path):
    run('types', 'generate-headers', *common_args(tmp_path))
    assert (tmp_path / 'build' / 'generated' / 'generated' / 'types' / 'units.hpp').is_file()


def test_a_backend_without_a_default_output_needs_one_given(tmp_path):
    """The IR dump does not belong in the C++ build's generated tree."""
    proc = run('types', 'generate-headers', *common_args(tmp_path), '--backend', 'ir-dump', check=False)
    assert proc.returncode != 0
    assert 'pass --output-dir' in proc.stderr
    assert not (tmp_path / 'build').exists()


def test_everest_dir_without_entries_fails(tmp_path):
    proc = run(
        'interface', 'generate-headers',
        '--disable-clang-format',
        '--schemas-dir', str(SCHEMAS_DIR),
        '--everest-dir',
        '--output-dir', str(tmp_path / 'out'),
        check=False,
    )
    assert proc.returncode != 0


def test_help_is_available_at_every_level():
    invocations = [
        ('--help',),
        ('module', '--help'),
        ('mod', 'create', '--help'),
        ('if', 'generate-headers', '--help'),
        ('ty', 'gh', '--help'),
        ('conformance-report', '--help'),
        ('hlp', '--help'),
    ]
    assert [argv for argv in invocations if 'usage' not in run(*argv).stdout.lower()] == []
