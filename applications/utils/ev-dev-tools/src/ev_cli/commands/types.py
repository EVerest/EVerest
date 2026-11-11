# SPDX-License-Identifier: Apache-2.0
# Copyright Pionix GmbH and Contributors to EVerest
"""``ev-cli types ...``"""

from __future__ import annotations

from ..backends.base import OutputKind, registry
from .context import Context, print_templates, write_generated


def generate_headers(args) -> int:
    context = Context.from_args(args)
    backend = registry.create(args.backend, args)
    output_dir = context.resolve_output_dir(
        args.output_dir, default=backend.type_output_dir, backend=backend.name)

    model = context.type_model()
    for unit in context.requested_units(args.types):
        files = backend.emit_type_unit(model, unit, output_dir)
        write_generated(files, force=args.force, diff_only=args.diff)
    return 0


def get_templates(args) -> int:
    return print_templates(args, OutputKind.TYPE_UNIT)
