# SPDX-License-Identifier: Apache-2.0
# Copyright Pionix GmbH and Contributors to EVerest
"""Everything a command needs, assembled once from the parsed arguments.

The previous implementation kept this in module-level globals that ``main()``
filled in, which meant every function could reach anything and the order of
operations was part of the contract.  Here a command is handed a context and
can only use what it was given.
"""

from __future__ import annotations

import sys
from dataclasses import dataclass, field
from pathlib import Path
from typing import Sequence

from ..errors import DefinitionNotFound, EvCliError
from ..files import FileWriter, Strategy
from ..ir import build
from ..ir.model import Model
from ..schema.diagnostics import CollectingSink, PrintingSink, Severity, TeeSink
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
    build_dir: Path
    everest_projects: tuple[str, ...] = ()
    #: Collects what the loader has to say, so a command can report on it.
    diagnostics: CollectingSink = field(default_factory=CollectingSink)

    _tree: SourceTree | None = field(default=None, init=False, repr=False)
    _loader: DefinitionLoader | None = field(default=None, init=False, repr=False)
    _model: Model | None = field(default=None, init=False, repr=False)

    # -- construction -------------------------------------------------------

    @classmethod
    def from_args(cls, args, *, extra_everest_dirs: Sequence[Path] = ()) -> Context:
        everest_dirs = tuple(Path(entry).resolve() for entry in args.everest_dir)
        everest_dirs += tuple(Path(d).resolve() for d in extra_everest_dirs)
        schemas_dir = resolve_schemas_dir(
            Path(args.schemas_dir), Path(args.build_dir), everest_dirs
        )
        return cls(
            work_dir=Path(args.work_dir).resolve(),
            everest_dirs=everest_dirs,
            schemas_dir=schemas_dir,
            licenses_dir=Path(args.licenses),
            build_dir=Path(args.build_dir),
            everest_projects=tuple(getattr(args, 'everest_projects', ()) or ()),
        )

    # -- lazily built pieces ------------------------------------------------

    @property
    def tree(self) -> SourceTree:
        if self._tree is None:
            self._tree = SourceTree(self.everest_dirs)
        return self._tree

    @property
    def loader(self) -> DefinitionLoader:
        if self._loader is None:
            # Only errors are printed as they happen.  The legacy dialect is
            # expected and pervasive, so reporting each of its 500-odd sites on
            # every build would bury anything genuinely new; the summary below
            # points at the report instead.
            sink = TeeSink(
                self.diagnostics,
                PrintingSink(sys.stderr, minimum=Severity.ERROR),
            )
            self._loader = DefinitionLoader(
                self.tree, Validators.load(self.schemas_dir), sink
            )
        return self._loader

    def report_diagnostics_summary(self, stream=None) -> None:
        """One line, if anything read would change meaning under OpenAPI rules."""
        stream = stream if stream is not None else sys.stderr
        notable = [d for d in self.diagnostics if d.severity is not Severity.INFO]
        if not notable:
            return
        print(
            f'ev-cli: note: {len(notable)} definition site(s) would change meaning '
            f'if read as JSON Schema 2020-12 or OpenAPI 3.1. Run '
            f'"ev-cli types conformance-report --verbose" for details.',
            file=stream,
        )

    def model(self) -> Model:
        """Every type unit the source tree offers.

        All units are loaded even when only one is being emitted: a reference
        into another unit has to resolve for the backend to know whether it
        points at a struct or an enum.
        """
        if self._model is None:
            self._model = build.build_model(self.loader, self.loader.sink)
        return self._model

    def interface_model(self, names, *, skip_unparsable: bool) -> Model:
        """The types, plus the named interfaces."""
        return self.model().with_interfaces(build.build_interfaces(
            self.loader, self.loader.sink, names, skip_unparsable=skip_unparsable,
        ))

    def module_model(self, names, *, with_interfaces: bool = False) -> Model:
        """The named modules, and optionally the interfaces they implement.

        Generating a module's loader needs neither the interfaces nor the type
        units: the loader only ever mentions configuration values, which are
        primitives.  Since a build generates a loader once per module -- 118
        times in everest-core -- loading the whole type tree each time would be
        pure overhead, so it is not done unless something asks for it.
        """
        modules = build.build_modules(
            self.loader, self.loader.sink, self.work_dir, names)
        if not with_interfaces:
            return Model(modules=modules)

        needed = sorted({
            provided.interface
            for module in modules.values() for provided in module.provides
        })
        interfaces = build.build_interfaces(
            self.loader, self.loader.sink, needed, skip_unparsable=False)
        return Model(types=self.model().types, interfaces=interfaces, modules=modules)

    def writer(self, *, diff_only: bool = False) -> FileWriter:
        return FileWriter(diff_only=diff_only)

    @staticmethod
    def strategy(force: bool) -> Strategy:
        return Strategy.FORCE_UPDATE if force else Strategy.UPDATE

    # -- resolving what the user asked for ----------------------------------

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

    def requested_interfaces(self, values) -> tuple[list[str], bool]:
        """Interfaces named on the command line, and whether that was "all".

        The distinction matters: asked for everything, an unparsable interface
        is skipped with a diagnostic, because one broken definition elsewhere
        in the tree should not stop a build.  Asked for by name, it is an
        error, because the caller wanted that one.
        """
        if not values:
            return list(self.tree.interface_names()), True
        return [Path(str(v)).stem for v in values], False


def resolve_schemas_dir(
    schemas_dir: Path, build_dir: Path, everest_dirs: tuple[Path, ...]
) -> Path:
    """Find the framework's schema directory, falling back to a CMake cache.

    The build always passes ``--schemas-dir`` explicitly; this is for humans
    running the tool by hand in a source tree they have already configured.
    """
    if schemas_dir.exists():
        return schemas_dir.resolve()

    print(
        f'The schemas directory "{schemas_dir}" does not exist; looking for '
        f'everest-framework in {build_dir / "CMakeCache.txt"}',
        file=sys.stderr,
    )
    found = path_from_cmake_cache('everest-framework', build_dir / 'CMakeCache.txt')
    if found is not None:
        candidate = found / 'schemas'
        if candidate.exists():
            print(f'Using schemas from {candidate}', file=sys.stderr)
            return candidate.resolve()

    raise EvCliError(
        f'could not find the framework schemas. Pass --schemas-dir, or point '
        f'--build-dir at a configured build directory (tried "{schemas_dir}" '
        f'and {build_dir / "CMakeCache.txt"}).'
    )


def path_from_cmake_cache(variable_prefix: str, cmake_cache: Path) -> Path | None:
    """Read ``<prefix>_SOURCE_DIR`` out of a CMake cache.

    Unlike the previous implementation this never prompts.  A generator that
    blocks on ``input()`` cannot be run from a build, and the answer here is
    not a judgement call: either the cache names a directory that exists or it
    does not.
    """
    if not cmake_cache.exists():
        return None

    needle = f'{variable_prefix}_SOURCE_DIR:STATIC='
    try:
        for line in cmake_cache.read_text().splitlines():
            if line.startswith(needle):
                candidate = Path(line[len(needle):].strip())
                return candidate if candidate.exists() else None
    except OSError:
        return None
    return None


def detect_everest_projects(
    everest_dirs: tuple[Path, ...], projects: tuple[str, ...], build_dir: Path
) -> list[Path]:
    """Additional everest directories found through a configured build.

    ``module create`` and ``module update`` need the interface definitions of
    whatever project a module is being written against; if none of the
    ``--everest-dir`` entries looks like one of the named projects, a
    configured build directory can say where it is.
    """
    if any(d.exists() and d.name in projects for d in everest_dirs):
        return []

    if projects:
        print(
            f'Could not find {", ".join(projects)} among the everest '
            f'directories; looking in {build_dir / "CMakeCache.txt"}',
            file=sys.stderr,
        )

    found = []
    for project in projects:
        directory = path_from_cmake_cache(project, build_dir / 'CMakeCache.txt')
        if directory is not None:
            print(f'Using {project} from {directory}', file=sys.stderr)
            found.append(directory)
    return found
