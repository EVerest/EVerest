# SPDX-License-Identifier: Apache-2.0
# Copyright Pionix GmbH and Contributors to EVerest
"""Building interfaces."""

from __future__ import annotations

from collections.abc import Iterable, Mapping, Sequence
from typing import Any

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
from .common import (
    collect_referenced_units,
    read_declared_type,
    reject_non_identifier,
    resolve_reference,
)


def build_interfaces(loader: DefinitionLoader, names: Iterable[str]) -> dict[str, InterfaceDef]:
    return {name: build_interface(loader, name) for name in names}


def build_interface(loader: DefinitionLoader, name: str) -> InterfaceDef:
    """Build one interface.

    Payload types differ from struct fields in one respect: an interface has
    nowhere to *declare* a type, so anything written inline cannot be promoted.
    An inline enum keeps its values as a constraint and travels as a string; an
    inline object travels as raw JSON, with a warning saying so.
    """
    document = loader.interface_document(name)
    data, source = document.data, document.source
    variables = tuple(
        _build_var(loader, var_name, schema, source.child('vars', var_name))
        for var_name, schema in (data.get('vars') or {}).items()
    )
    commands = tuple(
        _build_cmd(loader, cmd_name, schema, source.child('cmds', cmd_name))
        for cmd_name, schema in (data.get('cmds') or {}).items()
    )
    payloads = (
        [var.type for var in variables]
        + [argument.type for cmd in commands for argument in cmd.arguments]
        + [cmd.result for cmd in commands if cmd.result is not None]
    )
    return InterfaceDef(
        name=name,
        description=data['description'],
        vars=variables,
        cmds=commands,
        errors=_resolve_errors(loader, data.get('errors') or [], source.child('errors')),
        referenced_type_units=collect_referenced_units(payloads),
        source=source,
    )


def _build_var(loader: DefinitionLoader, name: str, schema: Mapping[str, Any], source: SourceRef) -> VarDef:
    reject_non_identifier(name, source, 'variable name')
    return VarDef(
        name=name,
        description=schema['description'],
        type=_build_payload(loader, schema, source),
        source=source,
        constraints=Constraints.from_schema(schema),
        qos=schema.get('qos'),
    )


def _build_cmd(loader: DefinitionLoader, name: str, schema: Mapping[str, Any], source: SourceRef) -> CmdDef:
    reject_non_identifier(name, source, 'command name')
    arguments = tuple(
        _build_argument(loader, argument, argument_schema, source.child('arguments', argument))
        # Declaration order is the order of the generated handler's
        # parameters, so it is taken from the document as written.
        for argument, argument_schema in (schema.get('arguments') or {}).items()
    )

    result_schema = schema.get('result')
    result = None
    if result_schema is not None:
        result = _build_payload(loader, result_schema, source.child('result'))

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


def _build_argument(loader: DefinitionLoader, name: str, schema: Mapping[str, Any], source: SourceRef) -> ArgDef:
    reject_non_identifier(name, source, 'argument name')
    return ArgDef(
        name=name,
        description=schema['description'],
        type=_build_payload(loader, schema, source),
        source=source,
        constraints=Constraints.from_schema(schema),
    )


def _build_payload(loader: DefinitionLoader, schema: Mapping[str, Any], source: SourceRef) -> TypeExpr:
    if '$ref' in schema:
        return resolve_reference(loader, schema, source, from_unit=None)

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
        return Array(_build_payload(loader, items, source.child('items')))

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


def _resolve_errors(
    loader: DefinitionLoader, entries: Sequence[Mapping[str, Any]], root: SourceRef
) -> tuple[ErrorDef, ...]:
    """Resolve every ``/errors/<namespace>[#/<Name>]`` reference.

    No template consumes the result, but resolving is what catches a
    reference to an error that does not exist, or the same error pulled in
    twice by a list reference and a named one.
    """
    resolved: dict[tuple[str, str], ErrorDef] = {}

    for index, entry in enumerate(entries):
        source = root.child(index)
        reference = parse_error_reference(entry['reference'])
        for error in _resolve_namespace_errors(loader, reference, source):
            key = (error.namespace, error.name)
            if key in resolved:
                raise UnsupportedDefinition(
                    f'{source}: error {error.full_name} is already '
                    f'referenced by this interface'
                )
            resolved[key] = error

    return tuple(resolved.values())


def _resolve_namespace_errors(loader: DefinitionLoader, reference, source: SourceRef) -> list[ErrorDef]:
    try:
        document = loader.error_document(reference.namespace)
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
