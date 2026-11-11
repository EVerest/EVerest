# SPDX-License-Identifier: Apache-2.0
# Copyright Pionix GmbH and Contributors to EVerest
"""Everything a command needs, assembled once from the parsed arguments.

A command is handed a context and can use only what it was given, so nothing
depends on the order in which commands run.
"""

from __future__ import annotations

from dataclasses import dataclass, field
from pathlib import Path

from ..backends.base import Backend, GeneratedFile, OutputKind, registry
from ..errors import DefinitionNotFound, EvCliError
from ..files import FileWriter, Strategy
from ..ir import build
from ..ir.model import Model
from ..schema.loader import TYPES_DIR, DefinitionLoader, SourceTree
from ..schema.references import strip_yaml_suffix
from ..schema.validate import Validators


@dataclass
class Context:
    """The resolved inputs of one ev-cli invocation."""

    work_dir: Path
    everest_dirs: tuple[Path, ...]
    schemas_dir: Path
    licenses_dir: Path

    _tree: SourceTree | None = field(default=None, init=False, repr=False)
    _loader: DefinitionLoader | None = field(default=None, init=False, repr=False)

    @classmethod
    def from_args(cls, args) -> Context:
        everest_dirs = tuple(Path(entry).resolve() for entry in args.everest_dir)
        schemas_dir = resolve_schemas_dir(Path(args.schemas_dir))
        return cls(
            work_dir=Path(args.work_dir).resolve(),
            everest_dirs=everest_dirs,
            schemas_dir=schemas_dir,
            licenses_dir=Path(args.licenses),
        )

    @property
    def tree(self) -> SourceTree:
        if self._tree is None:
            self._tree = SourceTree(self.everest_dirs, self.work_dir)
        return self._tree

    @property
    def loader(self) -> DefinitionLoader:
        if self._loader is None:
            self._loader = DefinitionLoader(self.tree, Validators.load(self.schemas_dir))
        return self._loader

    def type_model(self) -> Model:
        """Every type unit."""
        return Model(types=build.build_type_units(self.loader))

    def interface_model(self, names) -> Model:
        """Every type unit, and the named interfaces."""
        return Model(
            types=build.build_type_units(self.loader),
            interfaces=build.build_interfaces(self.loader, names),
        )

    def module_model(self, names, *, with_interfaces: bool = False) -> Model:
        """The named modules, and optionally the interfaces they implement.

        Generating a module's loader needs neither the interfaces nor the type
        units: the loader only ever mentions configuration values, which are
        primitives.  Since a build generates a loader once per module, loading the
        whole type tree each time would be pure overhead, so it is not done
        unless something asks for it.
        """
        modules = build.build_modules(self.loader, names)
        if not with_interfaces:
            return Model(modules=modules)

        needed = sorted({
            provided.interface
            for module in modules.values() for provided in module.provides
        })
        interfaces = build.build_interfaces(self.loader, needed)
        return Model(types=build.build_type_units(self.loader), interfaces=interfaces, modules=modules)

    def resolve_output_dir(self, given: str | None, *, default: str | None, backend: str) -> Path:
        """Where a generating command writes.

        An explicit ``--output-dir`` resolves against the current directory, like
        any path on a command line; otherwise the backend's default resolves
        against the work directory.  A backend without a default needs one given.
        """
        if given:
            return Path(given).resolve()
        if default is None:
            raise EvCliError(f'the {backend} backend has no default output directory; pass --output-dir')
        return self.work_dir / default

    def requested_units(self, values) -> list[str]:
        """Type units named on the command line, or all of them."""
        if not values:
            return list(self.tree.type_units())

        known = set(self.tree.type_units())
        units = []
        for value in values:
            unit = self._as_unit(value)
            if unit not in known:
                raise DefinitionNotFound(
                    f'no type unit "{unit}" in any of the everest directories'
                )
            units.append(unit)
        return units

    @staticmethod
    def _as_unit(value) -> str:
        """Accept a unit name, a relative path, or an absolute path."""
        parts = list(Path(strip_yaml_suffix(str(value))).parts)
        if TYPES_DIR in parts:
            last = len(parts) - 1 - parts[::-1].index(TYPES_DIR)
            parts = parts[last + 1:]
        return '/'.join(parts)

    def requested_interfaces(self, values) -> list[str]:
        """Interfaces named on the command line, or every one the tree offers."""
        if not values:
            return list(self.tree.interface_names())
        return [Path(str(v)).stem for v in values]


def resolve_schemas_dir(schemas_dir: Path) -> Path:
    """The framework's schema directory, which the caller names."""
    if not schemas_dir.exists():
        raise EvCliError(
            f'the framework schemas directory "{schemas_dir}" does not exist. '
            f'Pass --schemas-dir naming the "schemas" directory of '
            f'everest-framework.'
        )
    return schemas_dir.resolve()


def write_generated(
    backend: Backend, files: list[GeneratedFile], *, force: bool, diff_only: bool = False
) -> None:
    """Post-process what a generating command emitted and write it.

    A template newer than its output forces the write, as ``--force`` does.
    """
    backend.postprocess(files)
    writer = FileWriter(diff_only=diff_only)
    strategy = Strategy.FORCE_UPDATE if force else Strategy.UPDATE
    for generated in files:
        writer.write_checking_templates(generated, strategy)


def print_templates(args, kind: OutputKind) -> int:
    """``get-templates``: the templates an output kind is rendered from."""
    backend = registry.create(args.backend, args)
    print(args.separator.join(str(path) for path in backend.templates(kind)))
    return 0
