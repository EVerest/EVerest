# SPDX-License-Identifier: Apache-2.0
# Copyright Pionix GmbH and Contributors to EVerest
"""``ev-cli module ...``"""

from __future__ import annotations

from ..backends.base import OutputKind, registry
from .context import Context, print_templates, write_generated
from .module_files import write_module_files


def generate_loader(args) -> int:
    context = Context.from_args(args)
    backend = registry.create(args.backend, args)
    output_dir = context.resolve_output_dir(
        args.output_dir, default=backend.loader_output_dir, backend=backend.name)

    model = context.module_model([args.module])
    files = backend.emit_module_loader(model, args.module, output_dir)
    write_generated(backend, files, force=args.force)
    return 0


def get_templates(args) -> int:
    return print_templates(args, OutputKind.MODULE_LOADER)


def create(args) -> int:
    return write_module_files(args, updating=False)


def update(args) -> int:
    return write_module_files(args, updating=True)
