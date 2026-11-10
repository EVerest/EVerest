# SPDX-License-Identifier: Apache-2.0
# Copyright Pionix GmbH and Contributors to EVerest
"""The command line surface.

This is a contract, not a design.  EVerest's CMake and Bazel builds drive
ev-cli as a black box -- they parse ``--version``, use ``get-templates`` output
as build dependencies, and declare the generated files as genrule outputs -- so
every option, alias and default below is deliberate, including the ones that
look odd.  New options may be added; none may be taken away.

Options shared between subcommands are declared once and attached through
argparse's ``parents=``, which is the only sharing argparse offers here: a
subparser does not inherit its command parser's options, and putting them
there would move them ahead of the action name on the command line, which the
build does not do.

Every generating command takes the same set -- see :func:`emitting` --
including each backend's own options.  An option belonging to a backend that
was not selected is simply inert, as is true of ``--disable-clang-format``.
``conformance-report`` generates nothing and takes only the definition sources.
"""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

from . import __version__
from .backends.base import DEFAULT_BACKEND, registry
from .commands import conformance as conformance_commands
from .commands import helpers as helpers_commands
from .commands import interfaces as interface_commands
from .commands import modules as module_commands
from .commands import types as type_commands
from .errors import EvCliError


def _common_options() -> argparse.ArgumentParser:
    """Options shared by every command that reads definitions.

    Only what all of them need: an option one backend uses belongs in that
    backend's own group.

    Built per call rather than kept as a module-level singleton, because most
    of the defaults derive from the working directory and would otherwise be
    frozen at import time.
    """
    parser = argparse.ArgumentParser(add_help=False)
    group = parser.add_argument_group('definition sources')
    cwd = Path.cwd()

    group.add_argument(
        '--work-dir', '-wd', type=str, default=str(cwd),
        help='work directory containing the manifest definitions (default: .)')
    group.add_argument(
        '--everest-dir', '-ed', nargs='*', default=[str(cwd)],
        help='everest directories containing the interface and type definitions '
             '(default: .)')
    group.add_argument(
        '--schemas-dir', '-sd', type=str,
        default=str(cwd / 'lib' / 'everest' / 'framework' / 'schemas'),
        help='framework directory containing the schema definitions '
             '(default: ./lib/everest/framework/schemas)')
    group.add_argument(
        '--licenses', '-lc', type=str, default=str(cwd / '../licenses'),
        help='directory from which custom license texts are read '
             '(default: ../licenses)')
    return parser


def _backend_choice() -> argparse.ArgumentParser:
    """The target selector, for the commands that emit something."""
    parser = argparse.ArgumentParser(add_help=False)
    parser.add_argument(
        '--backend', type=str, default=DEFAULT_BACKEND, choices=registry.names(),
        help='which target to generate for. "ir-dump" writes the intermediate '
             f'representation as JSON instead of code (default: {DEFAULT_BACKEND})')
    return parser


def emitting() -> list[argparse.ArgumentParser]:
    """Everything a generating command accepts.

    Built on demand rather than threaded through, which costs about 2 ms per
    run in total and keeps the working-directory-derived defaults out of import
    time.  Each backend's options are included because the selected backend is
    only known once ``--backend`` has been parsed.
    """
    return [_common_options(), _backend_choice(), *registry.options()]


def _output_dir_help(what: str, attribute: str) -> str:
    """Help for ``--output-dir``, naming the default each backend declares."""
    defaults, required = [], []
    for name in registry.names():
        default = getattr(registry.backend_class(name), attribute)
        if default is None:
            required.append(name)
        else:
            defaults.append(f'{{work-dir}}/{default} for {name}')
    notes = []
    if defaults:
        notes.append('default: ' + ', '.join(defaults))
    if required:
        notes.append('required for ' + ', '.join(required))
    return f'output directory for {what} ({"; ".join(notes)})'


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
    _add_conformance_command(commands)
    _add_helpers_command(commands)

    return parser


def _add_write_options(
    parser, *, force_help: str = 'force overwriting', diff: bool = True, only: bool = False,
) -> None:
    """``--force``, and ``--diff`` and ``--only`` where a command offers them."""
    parser.add_argument('-f', '--force', action='store_true', help=force_help)
    if diff:
        parser.add_argument('-d', '--diff', '--dry-run', action='store_true',
                            help='show the resulting diff instead of writing')
    if only:
        parser.add_argument('--only', type=str,
                            help='comma separated list of module files to touch. '
                                 'Use "--only which" to list the available names.')


def _add_get_templates(actions, handler) -> None:
    parser = actions.add_parser(
        'get-templates', aliases=['gt'], parents=emitting(),
        help='print the paths of the template files used')
    parser.add_argument(
        '-s', '--separator', type=str, default='\n',
        help='separator between template paths')
    parser.set_defaults(handler=handler)


def _add_module_command(commands) -> None:
    module = commands.add_parser('module', aliases=['mod'], help='module related actions')
    actions = module.add_subparsers(metavar='<action>', help='available actions', required=True)

    create = actions.add_parser('create', aliases=['c'], parents=emitting(),
                                help='create a module')
    create.add_argument('module', type=str, help='name of the module to create')
    _add_write_options(create, force_help='force overwriting - use with care!', only=True)
    create.set_defaults(handler=module_commands.create)

    update = actions.add_parser('update', aliases=['u'], parents=emitting(),
                                help='update a module')
    update.add_argument('module', type=str, help='name of the module to update')
    _add_write_options(update, only=True)
    update.set_defaults(handler=module_commands.update)

    loader = actions.add_parser('generate-loader', aliases=['gl'],
                                parents=emitting(),
                                help='generate the everest module loader')
    loader.add_argument('module', type=str,
                        help='name of the module to generate the loader for')
    _add_write_options(loader, diff=False)
    loader.add_argument('-o', '--output-dir', type=str,
                        help=_output_dir_help('the generated loader files', 'loader_output_dir'))
    loader.set_defaults(handler=module_commands.generate_loader)

    _add_get_templates(actions, module_commands.get_templates)


def _add_interface_command(commands) -> None:
    interface = commands.add_parser('interface', aliases=['if'],
                                    help='interface related actions')
    actions = interface.add_subparsers(
        metavar='<action>', help='available actions', required=True)

    headers = actions.add_parser('generate-headers', aliases=['gh'],
                                 parents=emitting(),
                                 help='generate interface headers')
    _add_write_options(headers)
    headers.add_argument('-o', '--output-dir', type=str,
                         help=_output_dir_help('the generated interface headers', 'interface_output_dir'))
    headers.add_argument('interfaces', nargs='*',
                         help='interfaces to generate headers for. With none given '
                              'all are processed.')
    headers.set_defaults(handler=interface_commands.generate_headers)

    _add_get_templates(actions, interface_commands.get_templates)


def _add_types_command(commands) -> None:
    types = commands.add_parser('types', aliases=['ty'], help='type related actions')
    actions = types.add_subparsers(
        metavar='<action>', help='available actions', required=True)

    headers = actions.add_parser('generate-headers', aliases=['gh'],
                                 parents=emitting(),
                                 help='generate type headers')
    _add_write_options(headers)
    headers.add_argument('-o', '--output-dir', type=str,
                         help=_output_dir_help('the generated type headers', 'type_output_dir'))
    headers.add_argument('types', nargs='*',
                         help='types to generate headers for. With none given all '
                              'are processed.')
    headers.set_defaults(handler=type_commands.generate_headers)

    _add_get_templates(actions, type_commands.get_templates)


def _add_conformance_command(commands) -> None:
    report = commands.add_parser(
        'conformance-report', parents=[_common_options()],
        help='report where the definitions rely on EVerest\'s own dialect '
             'rather than on standard JSON Schema')
    report.add_argument('--strict', action='store_true',
                        help='exit non-zero if anything is reported')
    report.add_argument('-v', '--verbose', action='store_true',
                        help='list every site rather than counts per category')
    report.set_defaults(handler=conformance_commands.report)


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
