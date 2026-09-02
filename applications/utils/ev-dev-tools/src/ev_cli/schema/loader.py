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

from dataclasses import dataclass, field
from pathlib import Path
from typing import Any, Iterable, Sequence

import yaml

from ..errors import DefinitionNotFound, SchemaError
from ..ir.common import SourceRef
from .diagnostics import Category, Diagnostic, Severity, Sink
from .validate import Validators

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
    """The everest directories that definitions are looked up in."""

    def __init__(self, everest_dirs: Sequence[Path]) -> None:
        self.everest_dirs = tuple(Path(d) for d in everest_dirs)

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

    # -- discovery ----------------------------------------------------------

    def type_units(self) -> tuple[str, ...]:
        """Every type unit across all roots, first root winning on a clash."""
        return self._discover(TYPES_DIR, recursive=True)

    def interface_names(self) -> tuple[str, ...]:
        return self._discover(INTERFACES_DIR, recursive=False)

    def _discover(self, subdir: str, *, recursive: bool) -> tuple[str, ...]:
        seen: dict[str, Path] = {}
        for root in self.everest_dirs:
            directory = root / subdir
            if not directory.is_dir():
                continue
            paths = directory.rglob('*.yaml') if recursive else directory.glob('*.yaml')
            for path in paths:
                name = path.relative_to(directory).with_suffix('').as_posix()
                seen.setdefault(name, path)
        return tuple(sorted(seen))


class DefinitionLoader:
    """Reads and validates definition documents, caching by resolved path.

    The cache is what replaces the previous implementation's module-level
    dictionaries: everything is loaded once and then queried, instead of
    interface and module generation prompting the type parser to fill a global
    cache as a side effect.
    """

    def __init__(self, tree: SourceTree, validators: Validators, sink: Sink) -> None:
        self.tree = tree
        self.validators = validators
        self.sink = sink
        self._cache: dict[Path, Document] = {}

    # -- typed accessors ----------------------------------------------------

    def type_document(self, unit: str) -> Document:
        return self._load(self.tree.type_file(unit), 'type')

    def interface_document(self, name: str) -> Document:
        document = self._load(self.tree.interface_file(name), 'interface')
        self._check_interface_payloads(document)
        return document

    def error_document(self, namespace: str) -> Document:
        return self._load(self.tree.error_file(namespace), 'error_declaration_list')

    def module_document(self, work_dir: Path, relative_module_dir: str) -> Document:
        """Module manifests come from the work directory, not the search path."""
        path = Path(work_dir) / MODULES_DIR / relative_module_dir / 'manifest.yaml'
        if not path.exists():
            raise DefinitionNotFound(f'could not find module manifest {path}')
        return self._load(path, 'module')

    # -- internals ----------------------------------------------------------

    def _load(self, path: Path, kind: str) -> Document:
        cached = self._cache.get(path)
        if cached is not None:
            return cached

        try:
            data = yaml.safe_load(path.read_text())
        except OSError as err:
            raise DefinitionNotFound(
                f'could not open {kind} definition {path}: {err.strerror}'
            ) from err
        except yaml.YAMLError as err:
            raise SchemaError(f'could not parse {kind} definition {path}: {err}') from err

        if not isinstance(data, dict):
            raise SchemaError(f'{path}: expected a mapping at the top level')

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
