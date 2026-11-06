# SPDX-License-Identifier: Apache-2.0
# Copyright Pionix GmbH and Contributors to EVerest
"""Building blocks shared by every part of the intermediate representation."""

from __future__ import annotations

from dataclasses import dataclass, fields
from typing import Any, Final


#: Schema keyword -> :class:`Constraints` field.  Also serves as the list of
#: validation keywords the IR claims to preserve, which the losslessness test
#: checks against the sources.
CONSTRAINT_KEYWORDS: Final[dict[str, str]] = {
    'minimum': 'minimum',
    'maximum': 'maximum',
    'minLength': 'min_length',
    'maxLength': 'max_length',
    'minItems': 'min_items',
    'maxItems': 'max_items',
    'pattern': 'pattern',
    'format': 'format',
    'additionalProperties': 'additional_properties',
    'enum': 'enum_values',
}

#: Keywords whose value is a sequence and has to be made hashable, because the
#: dataclasses carrying them are frozen.
_SEQUENCE_KEYWORDS = frozenset({'enum'})


@dataclass(frozen=True)
class Constraints:
    """Validation keywords, carried through for the backends that want them.

    The C++ backend ignores all of them: generated structs hold plain members
    and the framework validates payloads at runtime.  A documentation or
    OpenAPI backend needs them, so the IR carries them instead of dropping
    them while parsing.
    """

    minimum: float | None = None
    maximum: float | None = None
    min_length: int | None = None
    max_length: int | None = None
    min_items: int | None = None
    max_items: int | None = None
    pattern: str | None = None
    format: str | None = None
    additional_properties: bool | None = None
    #: The permitted values, where a schema restricts them.  A declared type
    #: with an enum becomes an :class:`~ev_cli.ir.types.EnumDef` and does not
    #: need this, but an enum written inline in an interface payload has
    #: nowhere to be declared, and this is where it survives.
    enum_values: tuple[Any, ...] | None = None
    default: Any = None
    #: Tells ``default: null`` apart from no default at all.
    has_default: bool = False

    @classmethod
    def from_schema(cls, schema: dict[str, Any]) -> Constraints:
        values: dict[str, Any] = {
            attribute: (
                tuple(schema[keyword]) if keyword in _SEQUENCE_KEYWORDS
                else schema[keyword]
            )
            for keyword, attribute in CONSTRAINT_KEYWORDS.items()
            if keyword in schema
        }
        if 'default' in schema:
            values['default'] = schema['default']
            values['has_default'] = True
        return cls(**values)

    @property
    def is_empty(self) -> bool:
        return self == NO_CONSTRAINTS

    def as_dict(self) -> dict[str, Any]:
        """Only the constraints that were actually set, for serialisation."""
        return {
            f.name: getattr(self, f.name)
            for f in fields(self)
            if f.name != 'has_default' and getattr(self, f.name) != f.default
        }


NO_CONSTRAINTS: Final[Constraints] = Constraints()
