# SPDX-License-Identifier: Apache-2.0
# Copyright Pionix GmbH and Contributors to EVerest
"""Helpers shared by building type units, interfaces and modules."""

from __future__ import annotations

from typing import Any, Mapping

from ...errors import UnsupportedDefinition
from ...schema.loader import DefinitionLoader
from ...schema.references import parse_type_reference
from ...schema.source import SourceRef
from ..types import JsonType, Ref, Variant


def json_type_of(declared: Any, source: SourceRef) -> JsonType:
    try:
        return JsonType(declared)
    except ValueError:
        raise UnsupportedDefinition(
            f'{source}: unknown json type {declared!r}'
        ) from None


def read_declared_type(schema: Mapping[str, Any], source: SourceRef) -> JsonType | Variant:
    """The ``type`` a schema without a ``$ref`` declares: one json type, or a list of them."""
    declared = schema.get('type')
    if declared is None:
        raise UnsupportedDefinition(f'{source}: neither a "type" nor a "$ref"')
    if isinstance(declared, list):
        return Variant(tuple(json_type_of(entry, source) for entry in declared))
    return json_type_of(declared, source)


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
