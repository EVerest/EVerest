# SPDX-License-Identifier: Apache-2.0
# Copyright Pionix GmbH and Contributors to EVerest
"""``ev-cli interface ...``"""

from __future__ import annotations

from pathlib import Path

from ..backends.base import registry
from .context import Context

DEFAULT_OUTPUT = 'build/generated/include/generated/interfaces'


def generate_headers(args) -> int:
    context = Context.from_args(args)
    output_dir = (
        Path(args.output_dir).resolve() if args.output_dir
        else context.work_dir / DEFAULT_OUTPUT
    )

    requested = context.requested_interfaces(args.interfaces)
    model = context.interface_model(requested)

    backend = registry.create(args.backend, args)
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
