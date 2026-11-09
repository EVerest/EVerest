# SPDX-License-Identifier: Apache-2.0
# Copyright Pionix GmbH and Contributors to EVerest
"""``ev-cli module ...``"""

from __future__ import annotations

from ..backends.base import registry
from .context import Context


def generate_loader(args) -> int:
    context = Context.from_args(args)
    backend = registry.create(args.backend, args)
    output_dir = context.resolve_output_dir(
        args.output_dir, default=backend.loader_output_dir, backend=backend.name)

    model = context.module_model([args.module])
    writer = context.writer()
    strategy = context.strategy(args.force)

    files = backend.emit_module_loader(model, args.module, output_dir)
    backend.postprocess(files)
    for generated in files:
        writer.write_checking_templates(generated, strategy)

    return 0


def get_templates(args) -> int:
    backend = registry.create(args.backend, args)
    print(args.separator.join(str(path) for path in backend.templates('module')))
    return 0


def create(args) -> int:
    from .module_files import write_module_files
    return write_module_files(args, updating=False)


def update(args) -> int:
    from .module_files import write_module_files
    return write_module_files(args, updating=True)
