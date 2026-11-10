# SPDX-License-Identifier: Apache-2.0
# Copyright Pionix GmbH and Contributors to EVerest
"""Helpers shared by building type units, interfaces and modules."""

from __future__ import annotations

from typing import Any, Mapping

from ...errors import UnsupportedDefinition
from ...schema.loader import DefinitionLoader
from ...schema.references import parse_type_reference
from ...schema.source import SourceRef
from ..types import JsonType, Ref


def json_type_of(declared: Any, source: SourceRef) -> JsonType:
    try:
        return JsonType(declared)
    except ValueError:
        raise UnsupportedDefinition(
            f'{source}: unknown json type {declared!r}'
        ) from None


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
