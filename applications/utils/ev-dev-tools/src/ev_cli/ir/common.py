# SPDX-License-Identifier: Apache-2.0
# Copyright Pionix GmbH and Contributors to EVerest
"""Building blocks shared by every part of the intermediate representation."""

from __future__ import annotations

from dataclasses import dataclass, fields
from pathlib import Path
from typing import Any, Final


def escape_pointer_segment(segment: str) -> str:
    """Escape one segment of a JSON pointer as RFC 6901 requires."""
    return str(segment).replace('~', '~0').replace('/', '~1')


@dataclass(frozen=True)
class SourceRef:
    """Where a node came from.

    This is provenance, not judgement.  It records which file and which
    position within it produced a node so that a backend can raise a useful
    error, and says nothing about whether that position was written well.
    Opinions about the source text are diagnostics and live in
    :mod:`ev_cli.schema.diagnostics` instead, so that nothing a backend can
    reach depends on how the source happened to be spelled.
    """

    file: Path
    pointer: str = ''

    def child(self, *segments: str | int) -> SourceRef:
        """Return a reference to a position nested below this one."""
        pointer = self.pointer
        for segment in segments:
            pointer += '/' + escape_pointer_segment(segment)
        return SourceRef(self.file, pointer)

    def __str__(self) -> str:
        return f'{self.file}#{self.pointer}' if self.pointer else str(self.file)


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

    The C++ backend ignores every one of these: generated structs hold plain
    members and the framework validates payloads at runtime instead.  A
    documentation or OpenAPI backend needs all of them, though, and the
    previous generator dropped them while parsing -- which is why EVerest's RST
    generation ended up re-parsing the YAML itself rather than reusing the
    generator's view of it.  Keeping them here is what stops the next backend
    from having to do the same.
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
