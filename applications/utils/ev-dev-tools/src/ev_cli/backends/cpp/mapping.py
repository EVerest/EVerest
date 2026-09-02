# SPDX-License-Identifier: Apache-2.0
# Copyright Pionix GmbH and Contributors to EVerest
"""How a JSON Schema type becomes a C++ type.

This table is the generated ABI.  everest-core's 118 modules and the
hand-written mirror in ``lib/everest/everest_api_types`` compile against these
choices, and a change here compiles cleanly while altering struct layout,
precision, or both.  It is therefore reproduced exactly as it was -- including
the two inconsistencies noted below, which are recorded as follow-up work
rather than fixed in passing.
"""

from __future__ import annotations

from dataclasses import dataclass
from enum import Enum
from typing import Final

from ...errors import UnsupportedDefinition
from ...ir.types import (
    Array,
    JsonType,
    LocalEnum,
    LocalStruct,
    Opaque,
    Primitive,
    Ref,
    TypeExpr,
    Variant,
)
from .naming import qualified_type


class Flavour(Enum):
    """Which of the two historical mappings applies.

    There are two, and they disagree: a struct member declared ``number``
    becomes ``float`` while a command argument declared ``number`` becomes
    ``double``, and ``integer`` is ``int32_t`` in one and ``int`` in the other.
    Nothing justifies the difference -- it is an accident of the previous
    implementation having grown two code paths -- but reconciling it changes
    the layout and the precision of generated structs, so it is deliberately
    left alone here and scheduled as its own change.
    """

    #: Members of a generated struct, from ``types/*.yaml``.
    FIELD = 'field'
    #: Variable and command payloads in an interface's generated signatures.
    PAYLOAD = 'payload'


FIELD_SCALARS: Final[dict[JsonType, str]] = {
    JsonType.BOOLEAN: 'bool',
    JsonType.INTEGER: 'int32_t',
    JsonType.NUMBER: 'float',
    JsonType.STRING: 'std::string',
    JsonType.NULL: 'std::nullptr_t',
}

PAYLOAD_SCALARS: Final[dict[JsonType, str]] = {
    JsonType.BOOLEAN: 'bool',
    JsonType.INTEGER: 'int',
    JsonType.NUMBER: 'double',
    JsonType.STRING: 'std::string',
    JsonType.NULL: 'std::nullptr_t',
    # The framework's own aliases for arbitrary JSON.
    JsonType.OBJECT: 'Object',
    JsonType.ARRAY: 'Array',
}

#: ``format`` values that map to a dedicated C++ type.
#:
#: Deliberately empty.  35 properties across the type files carry
#: ``format: date-time`` and every one of them is generated as a plain
#: ``std::string``; the templates still carry a ``DateTime`` branch that
#: nothing reaches.  Honouring the format would change those members' type,
#: so it is its own change.
FORMAT_TYPES: Final[dict[str, str]] = {}


@dataclass(frozen=True)
class Spelling:
    """A C++ type, plus what a backend needs to know to convert it."""

    text: str
    is_enum: bool = False
    is_vector: bool = False
    #: Whether a vector's elements are enums, which need converting one by one
    #: on their way to and from JSON.
    element_is_enum: bool = False
    #: Whether this is one of the framework's raw-JSON aliases rather than a
    #: type generated from a definition.
    is_opaque: bool = False

    def __str__(self) -> str:
        return self.text


class CppMapping:
    """Turns :data:`~ev_cli.ir.types.TypeExpr` values into C++ spellings."""

    def __init__(self, model, flavour: Flavour = Flavour.FIELD) -> None:
        self._model = model
        self._flavour = flavour

    @property
    def scalars(self) -> dict[JsonType, str]:
        return FIELD_SCALARS if self._flavour is Flavour.FIELD else PAYLOAD_SCALARS

    def for_flavour(self, flavour: Flavour) -> CppMapping:
        return CppMapping(self._model, flavour)

    # -- the mapping --------------------------------------------------------

    def spell(self, expr: TypeExpr) -> Spelling:
        if isinstance(expr, Primitive):
            return Spelling(self._scalar(expr.json_type))

        if isinstance(expr, Ref):
            text = qualified_type(expr.target.unit, expr.target.name)
            return Spelling(text, is_enum=self._model.is_enum(expr.target))

        if isinstance(expr, LocalEnum):
            return Spelling(expr.name, is_enum=True)

        if isinstance(expr, LocalStruct):
            return Spelling(expr.name)

        if isinstance(expr, Array):
            return self._array(expr)

        if isinstance(expr, Opaque):
            return Spelling(self._scalar(expr.json_type), is_opaque=True)

        if isinstance(expr, Variant):
            return Spelling(self._variant(expr))

        raise UnsupportedDefinition(f'cannot spell {expr!r} in C++')

    def _array(self, expr: Array) -> Spelling:
        inner = self.spell(expr.items)

        # A struct member is always a typed vector.  An interface payload only
        # becomes one when its items are a reference to a declared type: an
        # array of plain strings is handed over as the framework's raw-JSON
        # ``Array`` instead.  That asymmetry is what the generated interface
        # headers have always done -- ``handle_get_configuration_key(Array&)``
        # in ocpp_1_6_charge_point is the visible consequence -- so it is
        # reproduced rather than tidied.
        if self._flavour is Flavour.PAYLOAD and not isinstance(expr.items, Ref):
            return Spelling(PAYLOAD_SCALARS[JsonType.ARRAY], is_opaque=True)

        return Spelling(
            f'std::vector<{inner.text}>',
            is_vector=True,
            element_is_enum=inner.is_enum,
        )

    def _scalar(self, json_type: JsonType) -> str:
        try:
            return self.scalars[json_type]
        except KeyError:
            raise UnsupportedDefinition(
                f'no C++ type for json type "{json_type.value}" as a '
                f'{self._flavour.value}'
            ) from None

    def _variant(self, expr: Variant) -> str:
        # Alternatives are sorted so that two payloads listing the same types
        # in a different order instantiate the same std::variant, and
        # std::nullptr_t leads when null is allowed so that the variant default
        # constructs to the empty alternative.
        alternatives = sorted(
            PAYLOAD_SCALARS[option] for option in expr.non_null_options
        )
        if expr.nullable:
            alternatives.insert(0, PAYLOAD_SCALARS[JsonType.NULL])
        return f'std::variant<{", ".join(alternatives)}>'

    # -- convenience for the templates --------------------------------------

    def field_type(self, field) -> str:
        """A struct member's declared type, wrapped when it is optional."""
        text = self.spell(field.type).text
        return text if field.required else f'std::optional<{text}>'
