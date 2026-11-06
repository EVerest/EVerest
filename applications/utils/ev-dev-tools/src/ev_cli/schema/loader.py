# SPDX-License-Identifier: Apache-2.0
# Copyright Pionix GmbH and Contributors to EVerest
"""Finding, reading and caching definition documents.

``--everest-dir`` may name several roots, and a definition is looked up in each
in turn.  That search path is what lets one repository reference another's
types, and it is the reason EVerest's reference form is not resolvable by
outside tools.  Keeping the search here, in one place, means the reference
*syntax* can migrate to something standard without the lookup changing at all.
"""

from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path
from typing import Any, Sequence

import yaml

from ..errors import DefinitionNotFound, EverestDirNotFound, SchemaError, WorkDirNotFound
from ..ir.common import SourceRef
from .diagnostics import Category, Diagnostic, Severity, Sink
from .validate import DocumentKind, Validators

TYPES_DIR = 'types'
INTERFACES_DIR = 'interfaces'
ERRORS_DIR = 'errors'
MODULES_DIR = 'modules'


@dataclass(frozen=True)
class Document:
    """One parsed definition file."""

    path: Path
    data: dict[str, Any]

    @property
    def source(self) -> SourceRef:
        return SourceRef(self.path)

    @property
    def mtime(self) -> float:
        return self.path.stat().st_mtime


class SourceTree:
    """Where definitions are read from.

    Types, interfaces and errors are looked up across the everest directories;
    module manifests come from the work directory.
    """

    def __init__(self, everest_dirs: Sequence[Path], work_dir: Path) -> None:
        self.everest_dirs = tuple(Path(d) for d in everest_dirs)
        self.work_dir = Path(work_dir)
        self._reject_unusable_directories()

    def _reject_unusable_directories(self) -> None:
        """A directory that is not there is an error, not something to work around.

        A mistyped ``--everest-dir`` would otherwise make a run that finds no
        definitions and reports success, and a mistyped ``--work-dir`` would be
        created when output is written beneath it.
        """
        if not self.everest_dirs:
            raise EverestDirNotFound(
                'no everest directory given. Pass --everest-dir naming a '
                'directory that holds "interfaces", "types" and "errors".'
            )
        missing = [d for d in self.everest_dirs if not d.is_dir()]
        if missing:
            raise EverestDirNotFound(
                'not an everest directory: '
                f'{", ".join(str(d) for d in missing)}. Every --everest-dir '
                'entry must exist; out-of-tree definitions are not found '
                'implicitly.'
            )
        if not self.work_dir.is_dir():
            raise WorkDirNotFound(
                f'not a directory: {self.work_dir}. --work-dir must name an '
                f'existing directory.'
            )

    def resolve(self, relative: str | Path) -> Path:
        """Return the first root in which ``relative`` exists."""
        for root in self.everest_dirs:
            candidate = root / relative
            if candidate.exists():
                return candidate
        raise DefinitionNotFound(
            f'could not find "{relative}" in any of the everest directories: '
            f'{", ".join(str(d) for d in self.everest_dirs)}'
        )

    # -- individual files ---------------------------------------------------

    def type_file(self, unit: str) -> Path:
        return self.resolve(f'{TYPES_DIR}/{unit}.yaml')

    def interface_file(self, name: str) -> Path:
        return self.resolve(f'{INTERFACES_DIR}/{name}.yaml')

    def error_file(self, namespace: str) -> Path:
        return self.resolve(f'{ERRORS_DIR}/{namespace}.yaml')

    def manifest_file(self, module: str) -> Path:
        path = self.work_dir / MODULES_DIR / module / 'manifest.yaml'
        if not path.exists():
            raise DefinitionNotFound(f'could not find module manifest {path}')
        return path

    # -- discovery ----------------------------------------------------------

    def type_units(self) -> tuple[str, ...]:
        """Every type unit across all roots, first root winning on a clash."""
        return self._discover(TYPES_DIR)

    def interface_names(self) -> tuple[str, ...]:
        return self._discover(INTERFACES_DIR)

    def _discover(self, subdir: str) -> tuple[str, ...]:
        seen: dict[str, Path] = {}
        for root in self.everest_dirs:
            directory = root / subdir
            if not directory.is_dir():
                continue
            for path in directory.glob('*.yaml'):
                seen.setdefault(path.stem, path)
        return tuple(sorted(seen))


class DefinitionLoader:
    """Reads and validates definition documents, caching by resolved path.

    Every document is read at most once per run, however many references reach
    it.
    """

    def __init__(self, tree: SourceTree, validators: Validators, sink: Sink) -> None:
        self.tree = tree
        self.validators = validators
        self.sink = sink
        self._cache: dict[Path, Document] = {}

    # -- typed accessors ----------------------------------------------------

    def type_document(self, unit: str) -> Document:
        return self._load(self.tree.type_file(unit), DocumentKind.TYPE)

    def interface_document(self, name: str) -> Document:
        document = self._load(self.tree.interface_file(name), DocumentKind.INTERFACE)
        self._check_interface_payloads(document)
        return document

    def error_document(self, namespace: str) -> Document:
        return self._load(self.tree.error_file(namespace), DocumentKind.ERROR_DECLARATION_LIST)

    def module_document(self, module: str) -> Document:
        return self._load(self.tree.manifest_file(module), DocumentKind.MANIFEST)

    # -- internals ----------------------------------------------------------

    def _load(self, path: Path, kind: DocumentKind) -> Document:
        cached = self._cache.get(path)
        if cached is not None:
            return cached

        try:
            data = yaml.safe_load(path.read_text())
        except OSError as err:
            raise DefinitionNotFound(
                f'could not open {kind.value} definition {path}: {err.strerror}'
            ) from err
        except yaml.YAMLError as err:
            raise SchemaError(f'could not parse {kind.value} definition {path}: {err}') from err

        if data is None:
            raise SchemaError(f'{path}: the {kind.value} definition has no content')
        if not isinstance(data, dict):
            found = 'a list' if isinstance(data, list) else 'a single value'
            raise SchemaError(
                f'{path}: a {kind.value} definition must consist of "key: value" '
                f'entries such as "description:", but this file holds {found}'
            )

        self.validators.validate(kind, data, path)

        if '$schema' not in data:
            self.sink.emit(Diagnostic(
                category=Category.UNDECLARED_DIALECT,
                severity=Severity.INFO,
                source=SourceRef(path),
                message=(
                    'no $schema; the dialect is implied by the framework '
                    'meta-schema rather than declared'
                ),
                conformant_form='$schema: https://json-schema.org/draft/2020-12/schema',
            ))

        document = Document(path=path, data=data)
        self._cache[path] = document
        return document

    def _check_interface_payloads(self, document: Document) -> None:
        """Mirror the framework's second validation pass over embedded schemas."""
        data = document.data
        for name, definition in (data.get('vars') or {}).items():
            self.validators.check_is_schema(definition, document.path, f'/vars/{name}')
        for name, definition in (data.get('cmds') or {}).items():
            for argument, schema in (definition.get('arguments') or {}).items():
                self.validators.check_is_schema(
                    schema, document.path, f'/cmds/{name}/arguments/{argument}')
            if 'result' in definition:
                self.validators.check_is_schema(
                    definition['result'], document.path, f'/cmds/{name}/result')
