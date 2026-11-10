# SPDX-License-Identifier: Apache-2.0
# Copyright Pionix GmbH and Contributors to EVerest
"""Building type units.

Each unit is built by its own short-lived :class:`_UnitBuild`, so no state
survives between units and the order units are built in cannot matter.
"""

from __future__ import annotations

from typing import Any, Mapping, Sequence

from ...errors import UnsupportedDefinition
from ...schema.loader import DefinitionLoader
from ...schema.source import SourceRef
from ..common import Constraints
from ..types import (
    SCALAR_TYPES,
    Array,
    EnumDef,
    Field,
    JsonType,
    LocalEnum,
    LocalStruct,
    Opaque,
    Primitive,
    Ref,
    StructDef,
    TypeExpr,
    TypeUnit,
    Variant,
)
from .common import json_type_of, resolve_reference


def build_type_units(loader: DefinitionLoader) -> dict[str, TypeUnit]:
    """Build every unit the source tree offers.

    Emitting one unit still needs them all: a reference into another unit has
    to resolve for a backend to know whether it points at a struct or an enum.
    """
    return {unit: build_type_unit(loader, unit) for unit in loader.tree.type_units()}


def build_type_unit(loader: DefinitionLoader, unit: str) -> TypeUnit:
    document = loader.type_document(unit)
    return _UnitBuild(unit, document.data, document.source, loader).run()


class _UnitBuild:
    """The build of a single type unit."""

    def __init__(self, name, data, source, loader: DefinitionLoader) -> None:
        self.name = name
        self.data = data
        self.source = source
        self.loader = loader
        # Declared enums keep their source order and come first; enums written
        # inline are promoted and appended after them, in discovery order.
        self._declared_enums: list[EnumDef] = []
        self._promoted_enums: list[EnumDef] = []
        self._structs: list[StructDef] = []
        self._referenced_type_units: set[str] = set()

    # -- entry point --------------------------------------------------------

    def run(self) -> TypeUnit:
        root = self.source.child('types')
        for name, schema in (self.data.get('types') or {}).items():
            self._declare(name, schema, root.child(name))

        enums = tuple(self._declared_enums) + tuple(self._promoted_enums)
        structs = tuple(self._structs)
        self._reject_name_clashes(enums, structs)

        return TypeUnit(
            name=self.name,
            description=self.data['description'],
            enums=enums,
            structs=structs,
            referenced_type_units=frozenset(self._referenced_type_units),
            source=self.source,
        )

    def _reject_name_clashes(
        self, enums: Sequence[EnumDef], structs: Sequence[StructDef]
    ) -> None:
        """Two declarations of the same name would not compile once emitted."""
        seen: dict[str, SourceRef] = {}
        for declaration in (*enums, *structs):
            previous = seen.get(declaration.name)
            if previous is not None:
                raise UnsupportedDefinition(
                    f'{declaration.source}: "{declaration.name}" is already '
                    f'declared at {previous} in the same unit'
                )
            seen[declaration.name] = declaration.source

    # -- declarations -------------------------------------------------------

    def _declare(self, name: str, schema: Mapping[str, Any], source: SourceRef) -> None:
        declared = schema.get('type')

        if declared == JsonType.STRING.value and 'enum' in schema:
            self._declared_enums.append(EnumDef(
                name=capitalize_first(name),
                description=schema.get('description', ''),
                values=tuple(schema['enum']),
                source=source,
            ))
            return

        if declared == JsonType.OBJECT.value and 'properties' in schema:
            self._register_struct(capitalize_first(name), schema, source)
            return

        # Only these two shapes produce anything in a generated header.  A
        # third would be dropped silently, leaving a header that refers to a
        # type it does not declare, so it is rejected here instead.
        raise UnsupportedDefinition(
            f'{source}: a declared type must be either an object with '
            f'properties or a string with an enum, but this one declares '
            f'type={declared!r}'
        )

    def _register_struct(
        self, name: str, schema: Mapping[str, Any], source: SourceRef
    ) -> None:
        required = set(schema.get('required') or [])
        depends_on: list[str] = []
        fields: list[Field] = []

        for property_name, property_schema in (schema.get('properties') or {}).items():
            if not property_name.isidentifier():
                raise UnsupportedDefinition(
                    f'{source}: property name "{property_name}" is not usable as '
                    f'an identifier in generated code'
                )
            property_source = source.child('properties', property_name)
            fields.append(Field(
                name=property_name,
                description=property_schema.get('description'),
                type=self._build_type_expr(
                    property_schema, property_source,
                    enclosing_name=property_name, depends_on=depends_on,
                ),
                required=property_name in required,
                source=property_source,
                constraints=Constraints.from_schema(property_schema),
            ))

        self._structs.append(StructDef(
            name=name,
            description=schema.get('description', ''),
            fields=tuple(fields),
            depends_on=tuple(depends_on),
            source=source,
            constraints=Constraints.from_schema(schema),
        ))

    # -- type expressions ---------------------------------------------------

    def _build_type_expr(
        self,
        schema: Mapping[str, Any],
        source: SourceRef,
        *,
        enclosing_name: str,
        depends_on: list[str],
    ) -> TypeExpr:
        if '$ref' in schema:
            return self._build_reference(schema, source)

        declared = schema.get('type')
        if declared is None:
            raise UnsupportedDefinition(
                f'{source}: neither a "type" nor a "$ref"'
            )

        if isinstance(declared, list):
            return Variant(tuple(json_type_of(entry, source) for entry in declared))

        json_type = json_type_of(declared, source)

        if json_type is JsonType.STRING and 'enum' in schema:
            return self._promote_enum(enclosing_name, schema, source)

        if json_type in SCALAR_TYPES:
            return Primitive(json_type)

        if json_type is JsonType.ARRAY:
            items = schema.get('items')
            if items is None:
                raise UnsupportedDefinition(
                    f'{source}: an array must declare its "items"'
                )
            return Array(self._build_type_expr(
                items, source.child('items'),
                enclosing_name=enclosing_name, depends_on=depends_on,
            ))

        if json_type is JsonType.OBJECT:
            if 'properties' in schema:
                return self._promote_struct(enclosing_name, schema, source, depends_on)
            # An object with nothing said about its contents travels as raw
            # JSON; the framework passes it through untouched.
            return Opaque(JsonType.OBJECT)

        raise UnsupportedDefinition(f'{source}: cannot express type {declared!r}')

    def _build_reference(self, schema: Mapping[str, Any], source: SourceRef) -> Ref:
        reference = resolve_reference(
            self.loader, schema, source, from_unit=self.name
        )
        self._referenced_type_units.add(reference.target.unit)
        return reference

    # -- promotion of inline declarations -----------------------------------

    def _promote_enum(
        self, enclosing_name: str, schema: Mapping[str, Any], source: SourceRef
    ) -> LocalEnum:
        name = capitalize_first(enclosing_name)
        values = tuple(schema['enum'])

        existing = next((e for e in self._promoted_enums if e.name == name), None)
        if existing is not None:
            if existing.values != values:
                raise UnsupportedDefinition(
                    f'{source}: an inline enum named "{name}" was already '
                    f'promoted at {existing.source} with different values'
                )
            return LocalEnum(name)

        self._promoted_enums.append(EnumDef(
            name=name,
            description=schema.get('description', ''),
            values=values,
            source=source,
        ))
        return LocalEnum(name)

    def _promote_struct(
        self,
        enclosing_name: str,
        schema: Mapping[str, Any],
        source: SourceRef,
        depends_on: list[str],
    ) -> LocalStruct:
        name = capitalize_first(enclosing_name)
        depends_on.append(name)
        if not any(struct.name == name for struct in self._structs):
            self._register_struct(name, schema, source)
        return LocalStruct(name)


def capitalize_first(name: str) -> str:
    """Upper-case the first character and leave the rest alone.

    EVerest's declared type names are already PascalCase, so this matters only
    for declarations written inline and named after a snake_case property.
    """
    return name[:1].upper() + name[1:] if name else name
