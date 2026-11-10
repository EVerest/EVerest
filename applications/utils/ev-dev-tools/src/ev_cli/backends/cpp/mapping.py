# SPDX-License-Identifier: Apache-2.0
# Copyright Pionix GmbH and Contributors to EVerest
"""How a type expression of the IR is spelled as a C++ type.

These choices are the generated ABI.  Every module in everest-core and the
hand-written mirror in ``lib/everest/everest_api_types`` compile against them,
and a change here compiles cleanly while altering struct layout, precision, or
both.
"""

from __future__ import annotations

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
    """Which of the two scalar mappings applies.

    They disagree: a struct member declared ``number`` becomes ``float`` while
    a command argument declared ``number`` becomes ``double``, and ``integer``
    is ``int32_t`` in one and ``int`` in the other.  Reconciling them would
    change the layout and the precision of generated structs.
    """

    #: Members of a generated struct, from ``types/*.yaml``.
    FIELD = 'field'
    #: Interface variables and command arguments and results, and module
    #: configuration.
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


class CppMapping:
    """Turns :data:`~ev_cli.ir.types.TypeExpr` values into C++ type names."""

    def __init__(self, flavour: Flavour = Flavour.FIELD) -> None:
        self._flavour = flavour

    def spell(self, expr: TypeExpr) -> str:
        if isinstance(expr, (Primitive, Opaque)):
            return self._scalar(expr.json_type)

        if isinstance(expr, Ref):
            return qualified_type(expr.target.unit, expr.target.name)

        if isinstance(expr, (LocalEnum, LocalStruct)):
            return expr.name

        if isinstance(expr, Array):
            return self._array(expr)

        if isinstance(expr, Variant):
            return self._variant(expr)

        raise UnsupportedDefinition(f'cannot spell {expr!r} in C++')

    def _array(self, expr: Array) -> str:
        # A struct member is always a typed vector.  An interface payload only
        # becomes one when its items are a reference to a declared type: an
        # array of plain strings is handed over as the framework's raw-JSON
        # ``Array`` instead.  Modules implement against that asymmetry --
        # ``handle_get_configuration_key(Array&)`` in ocpp_1_6_charge_point is
        # one -- so it is part of the generated API.
        if self._flavour is Flavour.PAYLOAD and not isinstance(expr.items, Ref):
            return PAYLOAD_SCALARS[JsonType.ARRAY]

        return f'std::vector<{self.spell(expr.items)}>'

    def _scalar(self, json_type: JsonType) -> str:
        scalars = FIELD_SCALARS if self._flavour is Flavour.FIELD else PAYLOAD_SCALARS
        try:
            return scalars[json_type]
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
