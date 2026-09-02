# SPDX-License-Identifier: Apache-2.0
# Copyright Pionix GmbH and Contributors to EVerest
"""``ev-cli interface ...``"""

from __future__ import annotations

from pathlib import Path

from ..backends.base import registry
from ..files import clang_format
from .context import Context

DEFAULT_OUTPUT = 'build/generated/include/generated/interfaces'


def generate_headers(args) -> int:
    context = Context.from_args(args)
    output_dir = (
        Path(args.output_dir).resolve() if args.output_dir
        else context.work_dir / DEFAULT_OUTPUT
    )

    requested, everything = context.requested_interfaces(args.interfaces)
    # Asked for the whole tree, one unreadable interface is reported and
    # skipped rather than failing the build; asked for specific ones, the
    # caller wanted them and gets the error.
    model = context.interface_model(requested, skip_unparsable=everything)

    backend = registry.create(args.backend)
    writer = context.writer(diff_only=args.diff)
    strategy = context.strategy(args.force)

    for interface in requested:
        if interface not in model.interfaces:
            continue
        for generated in backend.emit_interface(model, interface, output_dir):
            if not context.disable_clang_format:
                clang_format(context.clang_format_dir, generated)
            writer.write_checking_templates(generated, strategy)

    context.report_diagnostics_summary()
    return 0


def get_templates(args) -> int:
    backend = registry.create(args.backend)
    print(args.separator.join(str(path) for path in backend.templates('interface')))
    return 0
