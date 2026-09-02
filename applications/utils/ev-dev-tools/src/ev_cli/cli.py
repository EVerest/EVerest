# SPDX-License-Identifier: Apache-2.0
# Copyright Pionix GmbH and Contributors to EVerest
"""The command line surface.

This is a contract, not a design.  EVerest's CMake and Bazel builds drive
ev-cli as a black box -- they parse ``--version``, use ``get-templates`` output
as build dependencies, and declare the generated files as genrule outputs -- so
every option, alias and default below is reproduced deliberately, including the
ones that look odd.  New options may be added; none may be taken away.
"""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

from . import __version__
from .backends.base import DEFAULT_BACKEND, registry
from .commands import helpers as helpers_commands
from .commands import interfaces as interface_commands
from .commands import modules as module_commands
from .commands import types as type_commands
from .errors import EvCliError

#: The repository root, reached from this file's install location.  The default
#: only makes sense for a checkout, which is how the build uses it.
DEFAULT_EVEREST_DIR = Path(__file__).resolve().parents[5]
DEFAULT_SCHEMAS_DIR = DEFAULT_EVEREST_DIR / 'lib' / 'everest' / 'framework' / 'schemas'


def add_common_arguments(parser: argparse.ArgumentParser) -> None:
    """Options accepted by every generating command."""
    cwd = Path.cwd()
    everest_dir_default = [str(DEFAULT_EVEREST_DIR), str(cwd.parent / 'EVerest')]

    parser.add_argument(
        '--work-dir', '-wd', type=str, default=str(cwd),
        help='work directory containing the manifest definitions (default: .)')
    parser.add_argument(
        '--everest-dir', '-ed', nargs='*', default=everest_dir_default,
        help='everest directories containing the interface and type definitions '
             f'(default: {everest_dir_default})')
    parser.add_argument(
        '--everest-projects', '-ep', nargs='*', default=['EVerest'],
        help='everest project names, used to locate their directories via a '
             'configured build (default: EVerest)')
    parser.add_argument(
        '--schemas-dir', '-sd', type=str, default=str(DEFAULT_SCHEMAS_DIR),
        help=f'framework directory containing the schema definitions '
             f'(default: {DEFAULT_SCHEMAS_DIR})')
    parser.add_argument(
        '--licenses', '-lc', type=str, default=str(cwd / '../licenses'),
        help='directory from which custom license texts are read '
             '(default: ../licenses)')
    parser.add_argument(
        '--build-dir', '-bd', type=str, default=str(cwd / 'build'),
        help='build directory used to locate the framework when --schemas-dir '
             'is not given (default: ./build)')
    parser.add_argument(
        '--clang-format-file', type=str, default=str(cwd),
        help='directory containing the .clang-format file (default: .)')
    parser.add_argument(
        '--disable-clang-format', action='store_true', default=False,
        help='do not run clang-format over generated sources')


def add_backend_argument(parser: argparse.ArgumentParser) -> None:
    parser.add_argument(
        '--backend', type=str, default=DEFAULT_BACKEND, choices=registry.names(),
        help='which target to generate for. "ir-dump" writes the intermediate '
             f'representation as JSON instead of code (default: {DEFAULT_BACKEND})')


def add_get_templates(subparsers, handler) -> None:
    parser = subparsers.add_parser(
        'get-templates', aliases=['gt'],
        help='print the paths of the template files used')
    add_common_arguments(parser)
    add_backend_argument(parser)
    parser.add_argument(
        '-s', '--separator', type=str, default='\n',
        help='separator between template paths')
    parser.set_defaults(handler=handler)


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        prog='ev-cli', description='EVerest command line tool')
    # prog is set explicitly so that --version prints "ev-cli <version>"
    # however the tool was invoked; cmake/ev-cli.cmake strips that prefix.
    parser.add_argument('--version', action='version', version=f'%(prog)s {__version__}')

    commands = parser.add_subparsers(
        metavar='<command>', help='available commands', required=True)

    _add_module_command(commands)
    _add_interface_command(commands)
    _add_types_command(commands)
    _add_helpers_command(commands)

    return parser


def _add_module_command(commands) -> None:
    module = commands.add_parser('module', aliases=['mod'], help='module related actions')
    actions = module.add_subparsers(metavar='<action>', help='available actions', required=True)

    create = actions.add_parser('create', aliases=['c'], help='create a module')
    add_common_arguments(create)
    create.add_argument('module', type=str, help='name of the module to create')
    create.add_argument('-f', '--force', action='store_true',
                        help='force overwriting - use with care!')
    create.add_argument('-d', '--diff', '--dry-run', action='store_true',
                        help='show the resulting diff instead of writing')
    create.add_argument('--only', type=str,
                        help='comma separated list of module files to touch. '
                             'Use "--only which" to list the available names.')
    create.set_defaults(handler=module_commands.create)

    update = actions.add_parser('update', aliases=['u'], help='update a module')
    add_common_arguments(update)
    update.add_argument('module', type=str, help='name of the module to update')
    update.add_argument('-f', '--force', action='store_true', help='force overwriting')
    update.add_argument('-d', '--diff', '--dry-run', action='store_true',
                        help='show the resulting diff instead of writing')
    update.add_argument('--only', type=str,
                        help='comma separated list of module files to touch. '
                             'Use "--only which" to list the available names.')
    update.set_defaults(handler=module_commands.update)

    loader = actions.add_parser('generate-loader', aliases=['gl'],
                                help='generate the everest module loader')
    add_common_arguments(loader)
    add_backend_argument(loader)
    loader.add_argument('module', type=str,
                        help='name of the module to generate the loader for')
    loader.add_argument('-f', '--force', action='store_true', help='force overwriting')
    loader.add_argument('-o', '--output-dir', type=str,
                        help='output directory for the generated loader files '
                             '(default: {work-dir}/build/generated/generated/modules)')
    loader.set_defaults(handler=module_commands.generate_loader)

    add_get_templates(actions, module_commands.get_templates)


def _add_interface_command(commands) -> None:
    interface = commands.add_parser('interface', aliases=['if'],
                                    help='interface related actions')
    actions = interface.add_subparsers(
        metavar='<action>', help='available actions', required=True)

    headers = actions.add_parser('generate-headers', aliases=['gh'],
                                 help='generate interface headers')
    add_common_arguments(headers)
    add_backend_argument(headers)
    headers.add_argument('-f', '--force', action='store_true', help='force overwriting')
    headers.add_argument('-o', '--output-dir', type=str,
                         help='output directory for the generated interface headers '
                              '(default: {work-dir}/build/generated/include/generated/interfaces)')
    headers.add_argument('-d', '--diff', '--dry-run', action='store_true',
                         help='show the resulting diff instead of writing')
    headers.add_argument('interfaces', nargs='*',
                         help='interfaces to generate headers for. With none given '
                              'all are processed and unparsable ones are skipped.')
    headers.set_defaults(handler=interface_commands.generate_headers)

    add_get_templates(actions, interface_commands.get_templates)


def _add_types_command(commands) -> None:
    types = commands.add_parser('types', aliases=['ty'], help='type related actions')
    actions = types.add_subparsers(
        metavar='<action>', help='available actions', required=True)

    headers = actions.add_parser('generate-headers', aliases=['gh'],
                                 help='generate type headers')
    add_common_arguments(headers)
    add_backend_argument(headers)
    headers.add_argument('-f', '--force', action='store_true', help='force overwriting')
    headers.add_argument('-o', '--output-dir', type=str,
                         help='output directory for the generated type headers '
                              '(default: {work-dir}/build/generated/generated/types)')
    headers.add_argument('-d', '--diff', '--dry-run', action='store_true',
                         help='show the resulting diff instead of writing')
    headers.add_argument('types', nargs='*',
                         help='types to generate headers for. With none given all '
                              'are processed.')
    headers.set_defaults(handler=type_commands.generate_headers)

    report = actions.add_parser(
        'conformance-report',
        help='report where the definitions rely on EVerest\'s own dialect '
             'rather than on standard JSON Schema')
    add_common_arguments(report)
    report.add_argument('--strict', action='store_true',
                        help='exit non-zero if anything is reported')
    report.add_argument('-v', '--verbose', action='store_true',
                        help='list every site rather than counts per category')
    report.set_defaults(handler=type_commands.conformance_report)

    add_get_templates(actions, type_commands.get_templates)


def _add_helpers_command(commands) -> None:
    helpers = commands.add_parser('helpers', aliases=['hlp'], help='helper actions')
    actions = helpers.add_subparsers(
        metavar='<action>', help='available actions', required=True)

    uuids = actions.add_parser('generate-uuids', help='generate uuids')
    uuids.add_argument('count', type=int, default=3)
    uuids.set_defaults(handler=helpers_commands.generate_uuids)

    yaml2json = actions.add_parser('yaml2json', help='convert yaml into json')
    yaml2json.add_argument('input', type=str, help='path to the yaml input file')
    yaml2json.add_argument('output', type=str, help='path to the json output file')
    yaml2json.set_defaults(handler=helpers_commands.yaml2json)

    json2yaml = actions.add_parser('json2yaml', help='convert json into yaml')
    json2yaml.add_argument('input', type=str, help='path to the json input file')
    json2yaml.add_argument('output', type=str, help='path to the yaml output file')
    json2yaml.set_defaults(handler=helpers_commands.json2yaml)


def main(argv: list[str] | None = None) -> int:
    args = build_parser().parse_args(argv)
    try:
        return args.handler(args) or 0
    except EvCliError as err:
        print(f'ev-cli: error: {err}', file=sys.stderr)
        return 1


if __name__ == '__main__':
    sys.exit(main())
