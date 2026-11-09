# SPDX-License-Identifier: Apache-2.0
# Copyright Pionix GmbH and Contributors to EVerest
"""``ev-cli interface ...``"""

from __future__ import annotations

from ..backends.base import registry
from .context import Context


def generate_headers(args) -> int:
    context = Context.from_args(args)
    backend = registry.create(args.backend, args)
    output_dir = context.resolve_output_dir(
        args.output_dir, default=backend.interface_output_dir, backend=backend.name)

    requested = context.requested_interfaces(args.interfaces)
    model = context.interface_model(requested)

    writer = context.writer(diff_only=args.diff)
    strategy = context.strategy(args.force)

    for interface in requested:
        files = backend.emit_interface(model, interface, output_dir)
        backend.postprocess(files)
        for generated in files:
            writer.write_checking_templates(generated, strategy)

    return 0


def get_templates(args) -> int:
    backend = registry.create(args.backend, args)
    print(args.separator.join(str(path) for path in backend.templates('interface')))
    return 0
