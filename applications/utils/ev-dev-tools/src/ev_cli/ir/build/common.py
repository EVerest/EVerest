# SPDX-License-Identifier: Apache-2.0
# Copyright Pionix GmbH and Contributors to EVerest
"""Helpers shared by building type units, interfaces and modules."""

from __future__ import annotations

import re
from collections.abc import Iterable, Mapping
from typing import Any

from ...errors import UnsupportedDefinition
from ...schema.loader import DefinitionLoader
from ...schema.references import parse_type_reference
from ...schema.source import SourceRef
from ..types import Array, JsonType, Ref, TypeExpr, Variant


def read_json_type(declared: Any, source: SourceRef) -> JsonType:
    try:
        return JsonType(declared)
    except ValueError:
        raise UnsupportedDefinition(
            f'{source}: unknown json type {declared!r}'
        ) from None


#: What every target language accepts as an identifier: ASCII letters, digits
#: and underscores, not starting with a digit.
_IDENTIFIER = re.compile(r'[A-Za-z_][A-Za-z0-9_]*')


def reject_non_identifier(name: str, source: SourceRef, what: str) -> None:
    """Refuse a name that generated code in any language would need as an identifier."""
    if not _IDENTIFIER.fullmatch(name):
        raise UnsupportedDefinition(
            f'{source}: {what} "{name}" is not usable as an identifier in generated code'
        )


def read_declared_type(schema: Mapping[str, Any], source: SourceRef) -> JsonType | Variant:
    """The ``type`` a schema without a ``$ref`` declares: one json type, or a list of them."""
    declared = schema.get('type')
    if declared is None:
        raise UnsupportedDefinition(f'{source}: neither a "type" nor a "$ref"')
    if isinstance(declared, list):
        return Variant(tuple(read_json_type(entry, source) for entry in declared))
    return read_json_type(declared, source)


def resolve_reference(
    loader: DefinitionLoader,
    schema: Mapping[str, Any],
    source: SourceRef,
    *,
    from_unit: str | None,
) -> Ref:
    """Parse one ``$ref`` node and check that it resolves.

    Shared by type and interface building so that both reference dialects are accepted in
    exactly one place.
    """
    reference = parse_type_reference(schema['$ref'], from_unit=from_unit)
    loader.type_declaration(reference.target, source)
    return Ref(reference.target)


def collect_referenced_units(types: Iterable[TypeExpr]) -> frozenset[str]:
    """The type units the given type expressions refer into."""
    units: set[str] = set()
    pending = list(types)
    while pending:
        expr = pending.pop()
        if isinstance(expr, Ref):
            units.add(expr.target.unit)
        elif isinstance(expr, Array):
            pending.append(expr.items)
    return frozenset(units)
