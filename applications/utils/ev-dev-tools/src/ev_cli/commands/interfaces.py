# SPDX-License-Identifier: Apache-2.0
# Copyright Pionix GmbH and Contributors to EVerest
"""``ev-cli interface ...``"""

from __future__ import annotations

from ..backends.base import OutputKind, registry
from .context import Context, print_templates, write_generated


def generate_headers(args) -> int:
    context = Context.from_args(args)
    backend = registry.create(args.backend, args)
    output_dir = context.resolve_output_dir(
        args.output_dir, default=backend.interface_output_dir, backend=backend.name)

    requested = context.requested_interfaces(args.interfaces)
    model = context.interface_model(requested)
    for interface in requested:
        files = backend.emit_interface(model, interface, output_dir)
        write_generated(backend, files, force=args.force, diff_only=args.diff)
    return 0


def get_templates(args) -> int:
    return print_templates(args, OutputKind.INTERFACE)
