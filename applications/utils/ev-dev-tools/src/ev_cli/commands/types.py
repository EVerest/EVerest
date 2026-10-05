# SPDX-License-Identifier: Apache-2.0
# Copyright Pionix GmbH and Contributors to EVerest
"""``ev-cli types ...``"""

from __future__ import annotations

from pathlib import Path

from ..backends.base import registry
from ..schema.diagnostics import format_report, format_summary
from .context import Context

DEFAULT_OUTPUT = 'build/generated/generated/types'


def generate_headers(args) -> int:
    context = Context.from_args(args)
    output_dir = (
        Path(args.output_dir).resolve() if args.output_dir
        else context.work_dir / DEFAULT_OUTPUT
    )

    model = context.model()
    backend = registry.create(args.backend, args)
    writer = context.writer(diff_only=args.diff)
    strategy = context.strategy(args.force)

    for unit in context.requested_units(args.types):
        files = backend.emit_type_unit(model, unit, output_dir)
        backend.postprocess(files)
        for generated in files:
            writer.write_checking_templates(generated, strategy)

    context.report_diagnostics_summary()
    return 0


def get_templates(args) -> int:
    backend = registry.create(args.backend, args)
    print(args.separator.join(str(path) for path in backend.templates('types')))
    return 0


def conformance_report(args) -> int:
    """Report where the definitions depend on EVerest's own dialect.

    This is the measure of the cleanup that is still outstanding: every entry
    is a place where the source relies on a reading of JSON Schema that only
    EVerest's own tooling provides.  The counts should only ever fall.
    """
    context = Context.from_args(args)
    # Interfaces reference types in the same dialect, so they belong in the
    # count; anything unreadable is reported rather than fatal.
    context.interface_model(context.tree.interface_names(), skip_unparsable=True)

    report = (
        format_report(context.diagnostics) if args.verbose
        else format_summary(context.diagnostics)
    )
    print(report)

    if args.strict and len(context.diagnostics):
        return 1
    return 0
