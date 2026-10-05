# SPDX-License-Identifier: Apache-2.0
# Copyright Pionix GmbH and Contributors to EVerest
"""``ev-cli module create`` and ``ev-cli module update``.

These two write into a directory a human owns, which makes the write strategies
the important part rather than the rendering.  ``create`` never replaces an
existing file unless asked; ``update`` replaces the headers and the build file
but never the ``.cpp`` sources, and carries the contents of every
``ev@<uuid>`` region across from the file it is replacing.
"""

from __future__ import annotations

from pathlib import Path

from .. import blocks, license_headers
from ..backends.base import registry
from ..errors import EvCliError
from ..backends.base import UpdatePolicy
from ..files import Strategy
from .context import Context, detect_everest_projects

#: The order categories are reported in by ``--only which``.
CATEGORY_ORDER = ('core', 'interfaces', 'docs')


def write_module_files(args, *, updating: bool) -> int:
    context = _context_for(args)
    module_dir = context.work_dir / 'modules' / args.module

    model = _model_for(context, args.module)
    license_header = license_headers.find_header(
        license_headers.search_dirs(context.work_dir, context.licenses_dir),
        model.modules[args.module].metadata.license,
    )

    backend = registry.create(args.backend, args)
    generated = backend.emit_module_files(
        model, args.module, module_dir,
        license_header=license_header,
        read_blocks=lambda block_set, path: blocks.load(
            block_set, path, update=updating),
    )

    if args.only == 'which':
        _print_available(generated)
        return 0

    try:
        generated = _filter(generated, args.only)
    except EvCliError as err:
        # Historically a bad filter is reported without failing, so that
        # "--only <typo>" does not look like a generator error.
        print(f'ev-cli: {err}')
        return 0

    backend.postprocess(generated)

    writer = context.writer(diff_only=args.diff)
    if updating:
        _update(generated, writer, force=args.force)
    else:
        _create(generated, writer, force=args.force)

    context.report_diagnostics_summary()
    return 0


# --------------------------------------------------------------------------
# Inputs
# --------------------------------------------------------------------------

def _context_for(args) -> Context:
    """A context that can see the project the module is written against.

    A module being created outside everest-core needs that project's interface
    definitions; if none of the ``--everest-dir`` entries looks like one of the
    named projects, a configured build directory can say where it is.
    """
    everest_dirs = tuple(Path(d).resolve() for d in args.everest_dir)
    detected = detect_everest_projects(
        everest_dirs,
        tuple(getattr(args, 'everest_projects', ()) or ()),
        Path(args.build_dir),
    )
    return Context.from_args(args, extra_everest_dirs=detected)


def _model_for(context: Context, module: str):
    """The module, the interfaces it implements, and the types those use."""
    return context.module_model([module], with_interfaces=True)


# --------------------------------------------------------------------------
# --only
# --------------------------------------------------------------------------

def _print_available(generated) -> None:
    for category in CATEGORY_ORDER:
        in_category = [f for f in generated if f.category == category]
        if not in_category:
            continue
        print(f'Available files for category "{category}"')
        for file in in_category:
            print(f'  {file.abbr}')


def _filter(generated, only: str | None):
    if not only:
        return generated

    wanted = {name.strip() for name in only.split(',')}
    known = {f.abbr for f in generated}
    unknown = wanted - known
    if unknown:
        raise EvCliError(
            f'unknown file filters for --only: {", ".join(sorted(unknown))}\n'
            f'Use "--only which" to show the available names'
        )
    return [f for f in generated if f.abbr in wanted]


# --------------------------------------------------------------------------
# Writing
# --------------------------------------------------------------------------

def _create(generated, writer, *, force: bool) -> None:
    strategy = Strategy.FORCE_CREATE if force else Strategy.CREATE
    for file in generated:
        writer.write(file, strategy)


def _update(generated, writer, *, force: bool) -> None:
    """Replace what the generator owns, keep what a human owns.

    Which is which comes from the backend: for C++ the ``.cpp`` files are where
    a module's behaviour lives and are only ever created, never replaced -- not
    even with ``--force``, for which the deliberate escape is
    ``module create --force --only <id>.cpp``.  The command does not need to
    know that, and with another target it would not be true.
    """
    primary = Strategy.FORCE_UPDATE if force else Strategy.UPDATE

    for file in generated:
        if file.update_policy is UpdatePolicy.LEAVE_ALONE:
            continue
        strategy = (
            primary if file.update_policy is UpdatePolicy.REGENERATE
            else Strategy.UPDATE_IF_MISSING
        )
        writer.write(file, strategy, preserve_license_header=True)
