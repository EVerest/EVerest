# SPDX-License-Identifier: Apache-2.0
# Copyright Pionix GmbH and Contributors to EVerest
"""``ev-cli module ...``"""

from __future__ import annotations

from pathlib import Path

from ..backends.base import registry
from ..files import clang_format
from .context import Context

DEFAULT_LOADER_OUTPUT = 'build/generated/generated/modules'


def generate_loader(args) -> int:
    context = Context.from_args(args)
    output_dir = (
        Path(args.output_dir).resolve() if args.output_dir
        else context.work_dir / DEFAULT_LOADER_OUTPUT
    )

    model = context.module_model([args.module])
    backend = registry.create(args.backend)
    writer = context.writer()
    strategy = context.strategy(args.force)

    for generated in backend.emit_module_loader(model, args.module, output_dir):
        if not context.disable_clang_format:
            clang_format(context.clang_format_dir, generated)
        writer.write_checking_templates(generated, strategy)

    context.report_diagnostics_summary()
    return 0


def get_templates(args) -> int:
    backend = registry.create(args.backend)
    print(args.separator.join(str(path) for path in backend.templates('module')))
    return 0


def create(args) -> int:
    from .module_files import write_module_files
    return write_module_files(args, updating=False)


def update(args) -> int:
    from .module_files import write_module_files
    return write_module_files(args, updating=True)
