# SPDX-License-Identifier: Apache-2.0
# Copyright Pionix GmbH and Contributors to EVerest
"""The type level intermediate representation.

One :class:`TypeUnit` per ``types/*.yaml`` file.  Everything here describes
what a definition *means* in terms of JSON Schema; nothing here knows about
C++, or about any other target language.  Turning ``integer`` into ``int32_t``
is a backend's job.
"""

from __future__ import annotations

from dataclasses import dataclass
from enum import Enum
from typing import TypeAlias, Union

from .common import NO_CONSTRAINTS, Constraints, SourceRef


class JsonType(str, Enum):
    """The JSON Schema ``type`` keyword's values."""

    NULL = 'null'
    BOOLEAN = 'boolean'
    INTEGER = 'integer'
    NUMBER = 'number'
    STRING = 'string'
    ARRAY = 'array'
    OBJECT = 'object'


@dataclass(frozen=True, order=True)
class TypeRef:
    """A reference to a named type, already resolved to a unit and a name.

    Both reference dialects -- EVerest's historical ``/energy#/Power`` and the
    OpenAPI conformant ``energy.yaml#/types/Power`` -- parse into this, so
    nothing downstream can tell which one the source used.
    """

    unit: str
    name: str

    def __str__(self) -> str:
        return f'{self.unit}#/{self.name}'


# --------------------------------------------------------------------------
# Type expressions
# --------------------------------------------------------------------------

@dataclass(frozen=True)
class Primitive:
    """A scalar: ``boolean``, ``integer``, ``number``, ``string`` or ``null``."""

    json_type: JsonType


@dataclass(frozen=True)
class Array:
    """``type: array`` with a known item type."""

    items: TypeExpr


@dataclass(frozen=True)
class Ref:
    """A ``$ref`` to a named type, possibly in another unit."""

    target: TypeRef


@dataclass(frozen=True)
class LocalEnum:
    """A string enum written inline and promoted to a unit level enum."""

    name: str


@dataclass(frozen=True)
class LocalStruct:
    """An object written inline and promoted to a unit level struct."""

    name: str


@dataclass(frozen=True)
class Opaque:
    """``type: object`` or ``type: array`` with nothing said about the contents.

    The framework passes these through as raw JSON.
    """

    json_type: JsonType


@dataclass(frozen=True)
class Variant:
    """``type: [a, b, ...]`` -- a payload that may take several JSON types."""

    options: tuple[JsonType, ...]

    @property
    def nullable(self) -> bool:
        return JsonType.NULL in self.options

    @property
    def non_null_options(self) -> tuple[JsonType, ...]:
        return tuple(o for o in self.options if o is not JsonType.NULL)


TypeExpr: TypeAlias = Union[Primitive, Array, Ref, LocalEnum, LocalStruct, Opaque, Variant]


# --------------------------------------------------------------------------
# Named declarations
# --------------------------------------------------------------------------

@dataclass(frozen=True)
class Field:
    """One property of a struct."""

    name: str
    description: str | None
    type: TypeExpr
    required: bool
    source: SourceRef
    constraints: Constraints = NO_CONSTRAINTS


@dataclass(frozen=True)
class StructDef:
    """An object type."""

    name: str
    description: str
    #: Declaration order is part of the generated C++ API: generated types are
    #: positionally aggregate initialised in everest-core, so reordering these
    #: is a behavioural change.  Required fields come first, and within each
    #: group the source order is preserved.
    fields: tuple[Field, ...]
    #: Names of structs in the same unit that this one embeds, which decides
    #: emission order so that no forward declaration is needed.
    depends_on: tuple[str, ...]
    source: SourceRef
    constraints: Constraints = NO_CONSTRAINTS


@dataclass(frozen=True)
class EnumDef:
    """A string enum."""

    name: str
    description: str
    values: tuple[str, ...]
    source: SourceRef


@dataclass(frozen=True)
class TypeUnit:
    """One ``types/*.yaml`` file."""

    unit: str
    description: str
    enums: tuple[EnumDef, ...]
    structs: tuple[StructDef, ...]
    #: Units this one references, which drives the generated includes.
    referenced_units: frozenset[str]
    source: SourceRef

    def enum(self, name: str) -> EnumDef | None:
        return next((e for e in self.enums if e.name == name), None)

    def struct(self, name: str) -> StructDef | None:
        return next((s for s in self.structs if s.name == name), None)
