# SPDX-License-Identifier: Apache-2.0
# Copyright Pionix GmbH and Contributors to EVerest
"""Validation against the framework's meta-schemas.

The five meta-schemas in ``lib/everest/framework/schemas`` are draft-07, and so
is the validation done here.  That is deliberate: this generator's job is to
keep reading the definitions EVerest has today, and the framework's own C++
validator supports draft-07 only.  The keyword subset actually used means the
choice of draft is not observable -- but saying so is not the same as changing
it, and changing it belongs with the source migration, not here.
"""

from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path
from typing import Any, Final

import jsonschema
import yaml

from ..errors import SchemaError

#: Validator name -> meta-schema file stem.
SCHEMA_FILES: Final[dict[str, str]] = {
    'interface': 'interface',
    'module': 'manifest',
    'config': 'config',
    'type': 'type',
    'error_declaration_list': 'error-declaration-list',
}


@dataclass(frozen=True)
class Validators:
    """The meta-schema validators, loaded once per run."""

    by_name: dict[str, jsonschema.Draft7Validator]

    @classmethod
    def load(cls, schemas_dir: Path) -> Validators:
        validators: dict[str, jsonschema.Draft7Validator] = {}
        for name, stem in SCHEMA_FILES.items():
            path = schemas_dir / f'{stem}.yaml'
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

            validators[name] = jsonschema.Draft7Validator(schema)
        return cls(by_name=validators)

    def validate(self, name: str, document: Any, path: Path) -> None:
        """Validate a whole definition document against its meta-schema."""
        try:
            self.by_name[name].validate(document)
        except jsonschema.ValidationError as err:
            location = '/'.join(str(part) for part in err.absolute_path)
            where = f'{path}#/{location}' if location else str(path)
            raise SchemaError(f'{where}: {err.message}') from err

    def check_is_schema(self, subschema: Any, path: Path, pointer: str) -> None:
        """Check that an embedded payload declaration is itself a valid schema.

        Interface ``vars`` and command arguments and results are JSON Schema
        fragments that the meta-schema cannot check directly, so the framework
        validates them in a second step and this mirrors that.
        """
        try:
            jsonschema.Draft7Validator.check_schema(subschema)
        except jsonschema.SchemaError as err:
            raise SchemaError(f'{path}#{pointer}: not a valid schema: {err.message}') from err
