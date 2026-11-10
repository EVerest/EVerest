# SPDX-License-Identifier: Apache-2.0
# Copyright Pionix GmbH and Contributors to EVerest
"""Validation against the framework's meta-schemas.

The meta-schemas in ``lib/everest/framework/schemas`` are draft-07, and so is
the validation done here.  That is deliberate: this generator's job is to
keep reading the definitions EVerest has today, and the framework's own C++
validator supports draft-07 only.  The keyword subset actually used means the
choice of draft is not observable -- but saying so is not the same as changing
it, and changing it belongs with the source migration, not here.
"""

from __future__ import annotations

from dataclasses import dataclass
from enum import Enum
from pathlib import Path
from typing import Any

import jsonschema
import yaml

from ..errors import SchemaError
from .source import SourceRef


class DocumentKind(Enum):
    """A kind of definition document; the value names its meta-schema file."""

    TYPE = 'type'
    INTERFACE = 'interface'
    ERROR_DECLARATION_LIST = 'error-declaration-list'
    MANIFEST = 'manifest'


@dataclass(frozen=True)
class Validators:
    """The meta-schema validators, loaded once per run."""

    by_kind: dict[DocumentKind, jsonschema.Draft7Validator]

    @classmethod
    def load(cls, schemas_dir: Path) -> Validators:
        validators: dict[DocumentKind, jsonschema.Draft7Validator] = {}
        for kind in DocumentKind:
            path = schemas_dir / f'{kind.value}.yaml'
            try:
                schema = yaml.safe_load(path.read_text())
            except OSError as err:
                raise SchemaError(
                    f'could not open meta-schema {path}: {err.strerror}'
                ) from err
            except yaml.YAMLError as err:
                raise SchemaError(f'could not parse meta-schema {path}: {err}') from err

            try:
                jsonschema.Draft7Validator.check_schema(schema)
            except jsonschema.SchemaError as err:
                raise SchemaError(f'meta-schema {path} is itself invalid: {err}') from err

            validators[kind] = jsonschema.Draft7Validator(schema)
        return cls(by_kind=validators)

    def validate(self, kind: DocumentKind, document: Any, path: Path) -> None:
        """Validate a whole definition document of the given kind."""
        try:
            self.by_kind[kind].validate(document)
        except jsonschema.ValidationError as err:
            where = SourceRef(path).child(*err.absolute_path)
            raise SchemaError(f'{where}: {err.message}') from err

        if kind is DocumentKind.INTERFACE:
            self._check_interface_payloads(document, path)

    def _check_interface_payloads(self, document: Any, path: Path) -> None:
        """Mirror the framework's second pass over an interface.

        Interface ``vars`` and command arguments and results are JSON Schema
        fragments that the meta-schema cannot check directly.
        """
        root = SourceRef(path)
        for name, definition in (document.get('vars') or {}).items():
            self._check_is_schema(definition, root.child('vars', name))
        for name, definition in (document.get('cmds') or {}).items():
            command = root.child('cmds', name)
            for argument, schema in (definition.get('arguments') or {}).items():
                self._check_is_schema(schema, command.child('arguments', argument))
            if 'result' in definition:
                self._check_is_schema(definition['result'], command.child('result'))

    @staticmethod
    def _check_is_schema(subschema: Any, source: SourceRef) -> None:
        try:
            jsonschema.Draft7Validator.check_schema(subschema)
        except jsonschema.SchemaError as err:
            raise SchemaError(f'{source}: not a valid schema: {err.message}') from err
