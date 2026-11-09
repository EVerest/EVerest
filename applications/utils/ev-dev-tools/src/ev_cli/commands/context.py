# SPDX-License-Identifier: Apache-2.0
# Copyright Pionix GmbH and Contributors to EVerest
"""Everything a command needs, assembled once from the parsed arguments.

A command is handed a context and can use only what it was given, so nothing
depends on the order in which commands run.
"""

from __future__ import annotations

from dataclasses import dataclass, field
from pathlib import Path

from ..errors import DefinitionNotFound, EvCliError
from ..files import FileWriter, Strategy
from ..ir import build
from ..ir.model import Model
from ..schema.loader import DefinitionLoader, SourceTree
from ..schema.validate import Validators

YAML_SUFFIXES = ('.yaml', '.yml')


@dataclass
class Context:
    """The resolved inputs of one ev-cli invocation."""

    work_dir: Path
    everest_dirs: tuple[Path, ...]
    schemas_dir: Path
    licenses_dir: Path

    _tree: SourceTree | None = field(default=None, init=False, repr=False)
    _loader: DefinitionLoader | None = field(default=None, init=False, repr=False)
    _model: Model | None = field(default=None, init=False, repr=False)

    # -- construction -------------------------------------------------------

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

    # -- lazily built pieces ------------------------------------------------

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

    def model(self) -> Model:
        """Every type unit the source tree offers.

        All units are loaded even when only one is being emitted: a reference
        into another unit has to resolve for the backend to know whether it
        points at a struct or an enum.
        """
        if self._model is None:
            self._model = build.build_model(self.loader)
        return self._model

    def interface_model(self, names) -> Model:
        """The types, plus the named interfaces."""
        return self.model().with_interfaces(build.build_interfaces(self.loader, names))

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
        return Model(types=self.model().types, interfaces=interfaces, modules=modules)

    def writer(self, *, diff_only: bool = False) -> FileWriter:
        return FileWriter(diff_only=diff_only)

    @staticmethod
    def strategy(force: bool) -> Strategy:
        return Strategy.FORCE_UPDATE if force else Strategy.UPDATE

    # -- resolving what was asked for on the command line --------------------

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
        text = str(value)
        for suffix in YAML_SUFFIXES:
            if text.endswith(suffix):
                text = text[: -len(suffix)]
                break

        parts = list(Path(text).parts)
        if 'types' in parts:
            last = len(parts) - 1 - parts[::-1].index('types')
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
