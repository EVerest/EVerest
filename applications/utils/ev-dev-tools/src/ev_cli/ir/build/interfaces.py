# SPDX-License-Identifier: Apache-2.0
# Copyright Pionix GmbH and Contributors to EVerest
"""Building interfaces."""

from __future__ import annotations

from typing import Any, Iterable, Mapping, Sequence

from ...errors import DefinitionNotFound, UnsupportedDefinition, warn
from ...schema.loader import DefinitionLoader
from ...schema.references import parse_error_reference
from ...schema.source import SourceRef
from ..common import NO_CONSTRAINTS, Constraints
from ..interfaces import ArgDef, CmdDef, ErrorDef, InterfaceDef, VarDef
from ..types import (
    SCALAR_TYPES,
    Array,
    JsonType,
    Opaque,
    Primitive,
    TypeExpr,
    Variant,
)
from .common import read_declared_type, resolve_reference


def build_interfaces(loader: DefinitionLoader, names: Iterable[str]) -> dict[str, InterfaceDef]:
    return {name: build_interface(loader, name) for name in names}


def build_interface(loader: DefinitionLoader, name: str) -> InterfaceDef:
    document = loader.interface_document(name)
    return _InterfaceBuild(name, document.data, document.source, loader).run()


class _InterfaceBuild:
    """The build of a single interface.

    Payload types differ from struct fields in one respect: an interface has
    nowhere to *declare* a type, so anything written inline cannot be promoted.
    An inline enum keeps its values as a constraint and travels as a string; an
    inline object travels as raw JSON, with a warning saying so.
    """

    def __init__(self, name, data, source, loader: DefinitionLoader) -> None:
        self.name = name
        self.data = data
        self.source = source
        self.loader = loader
        self._referenced_type_units: set[str] = set()

    def run(self) -> InterfaceDef:
        return InterfaceDef(
            name=self.name,
            description=self.data['description'],
            vars=tuple(
                self._build_var(name, schema, self.source.child('vars', name))
                for name, schema in (self.data.get('vars') or {}).items()
            ),
            cmds=tuple(
                self._build_cmd(name, schema, self.source.child('cmds', name))
                for name, schema in (self.data.get('cmds') or {}).items()
            ),
            errors=self._resolve_errors(self.data.get('errors') or []),
            referenced_type_units=frozenset(self._referenced_type_units),
            source=self.source,
        )

    # -- vars and cmds ------------------------------------------------------

    def _build_var(self, name: str, schema: Mapping[str, Any], source: SourceRef) -> VarDef:
        return VarDef(
            name=name,
            description=schema['description'],
            type=self._build_payload(schema, source),
            source=source,
            constraints=Constraints.from_schema(schema),
            qos=schema.get('qos'),
        )

    def _build_cmd(self, name: str, schema: Mapping[str, Any], source: SourceRef) -> CmdDef:
        arguments = tuple(
            self._build_argument(argument, argument_schema, source.child('arguments', argument))
            # Declaration order is the order of the generated handler's
            # parameters, so it is taken from the document as written.
            for argument, argument_schema in (schema.get('arguments') or {}).items()
        )

        result_schema = schema.get('result')
        result = None
        if result_schema is not None:
            result = self._build_payload(result_schema, source.child('result'))

        return CmdDef(
            name=name,
            description=schema['description'],
            arguments=arguments,
            source=source,
            result=result,
            result_description=(
                result_schema.get('description') if result_schema else None),
            result_constraints=(
                Constraints.from_schema(result_schema) if result_schema
                else NO_CONSTRAINTS
            ),
        )

    def _build_argument(self, name: str, schema: Mapping[str, Any], source: SourceRef) -> ArgDef:
        return ArgDef(
            name=name,
            description=schema['description'],
            type=self._build_payload(schema, source),
            source=source,
            constraints=Constraints.from_schema(schema),
        )

    # -- payloads -----------------------------------------------------------

    def _build_payload(self, schema: Mapping[str, Any], source: SourceRef) -> TypeExpr:
        if '$ref' in schema:
            reference = resolve_reference(
                self.loader, schema, source, from_unit=None
            )
            self._referenced_type_units.add(reference.target.unit)
            return reference

        json_type = read_declared_type(schema, source)
        if isinstance(json_type, Variant):
            return json_type

        if json_type in SCALAR_TYPES:
            # A string with an enum is a string too: its permitted values
            # survive on the constraints, because there is no enum to name.
            return Primitive(json_type)

        if json_type is JsonType.ARRAY:
            items = schema.get('items')
            if items is None:
                # Nothing said about the elements: the payload travels as raw
                # JSON rather than as a typed vector.
                return Opaque(JsonType.ARRAY)
            return Array(self._build_payload(items, source.child('items')))

        # What remains is an object.
        if 'properties' in schema:
            warn(
                f'{source}: an interface payload declares properties inline. '
                f'There is nowhere in a generated interface header to declare '
                f'the resulting type, so the payload is passed through as raw '
                f'JSON and the properties have no effect. Declare the object '
                f'in types/ and reference it with $ref.'
            )
        return Opaque(JsonType.OBJECT)

    # -- errors -------------------------------------------------------------

    def _resolve_errors(self, entries: Sequence[Mapping[str, Any]]) -> tuple[ErrorDef, ...]:
        """Resolve every ``/errors/<namespace>[#/<Name>]`` reference.

        No template consumes the result, but resolving is what catches a
        reference to an error that does not exist, or the same error pulled in
        twice by a list reference and a named one.
        """
        root = self.source.child('errors')
        resolved: dict[tuple[str, str], ErrorDef] = {}

        for index, entry in enumerate(entries):
            source = root.child(index)
            reference = parse_error_reference(entry['reference'])
            for error in self._resolve_namespace_errors(reference, source):
                key = (error.namespace, error.name)
                if key in resolved:
                    raise UnsupportedDefinition(
                        f'{source}: error {error.full_name} is already '
                        f'referenced by this interface'
                    )
                resolved[key] = error

        return tuple(resolved.values())

    def _resolve_namespace_errors(self, reference, source: SourceRef) -> list[ErrorDef]:
        try:
            document = self.loader.error_document(reference.namespace)
        except DefinitionNotFound as err:
            raise DefinitionNotFound(f'{source}: {err}') from err

        errors = [
            ErrorDef(
                namespace=reference.namespace,
                name=entry['name'],
                description=entry['description'],
                source=document.source.child('errors', index),
            )
            for index, entry in enumerate(document.data.get('errors') or [])
        ]
        if reference.is_whole_namespace:
            return errors

        for error in errors:
            if error.name == reference.name:
                return [error]

        raise DefinitionNotFound(
            f'{source}: error namespace "{reference.namespace}" declares no '
            f'error named "{reference.name}"'
        )
