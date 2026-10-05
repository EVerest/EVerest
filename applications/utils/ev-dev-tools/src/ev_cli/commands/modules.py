# SPDX-License-Identifier: Apache-2.0
# Copyright Pionix GmbH and Contributors to EVerest
"""``ev-cli module ...``"""

from __future__ import annotations

from pathlib import Path

from ..backends.base import registry
from .context import Context

DEFAULT_LOADER_OUTPUT = 'build/generated/generated/modules'


def generate_loader(args) -> int:
    context = Context.from_args(args)
    output_dir = (
        Path(args.output_dir).resolve() if args.output_dir
        else context.work_dir / DEFAULT_LOADER_OUTPUT
    )

    model = context.module_model([args.module])
    backend = registry.create(args.backend, args)
    writer = context.writer()
    strategy = context.strategy(args.force)

    files = backend.emit_module_loader(model, args.module, output_dir)
    backend.postprocess(files)
    for generated in files:
        writer.write_checking_templates(generated, strategy)

    context.report_diagnostics_summary()
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
