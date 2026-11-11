# SPDX-License-Identifier: Apache-2.0
# Copyright Pionix GmbH and Contributors to EVerest
"""How a type expression of the IR becomes a C++ type.

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


def to_cpp_type(expr: TypeExpr, flavour: Flavour) -> str:
    """The C++ type name of a type expression."""
    if isinstance(expr, (Primitive, Opaque)):
        return _to_cpp_scalar(expr.json_type, flavour)

    if isinstance(expr, Ref):
        return qualified_type(expr.target.unit, expr.target.name)

    if isinstance(expr, (LocalEnum, LocalStruct)):
        return expr.name

    if isinstance(expr, Array):
        return _to_cpp_array(expr, flavour)

    if isinstance(expr, Variant):
        return f'std::variant<{variant_alternatives(expr)}>'

    raise UnsupportedDefinition(f'no C++ type for {expr!r}')


def is_typed_payload_array(expr: Array) -> bool:
    """Whether an array payload becomes a typed vector rather than raw JSON.

    A struct member is always a typed vector.  An interface payload only
    becomes one when its items are a reference to a declared type: an array of
    plain strings is handed over as the framework's raw-JSON ``Array`` instead.
    Modules implement against that asymmetry --
    ``handle_get_configuration_key(Array&)`` in ocpp_1_6_charge_point is one --
    so it is part of the generated API.
    """
    return isinstance(expr.items, Ref)


def variant_alternatives(expr: Variant) -> str:
    """The template arguments of a variant payload's ``std::variant``.

    Sorted so that two payloads listing the same types in a different order
    instantiate the same std::variant, with std::nullptr_t leading when null is
    allowed so that the variant default constructs to the empty alternative.
    """
    alternatives = sorted(PAYLOAD_SCALARS[option] for option in expr.non_null_options)
    if expr.nullable:
        alternatives.insert(0, PAYLOAD_SCALARS[JsonType.NULL])
    return ', '.join(alternatives)


def _to_cpp_array(expr: Array, flavour: Flavour) -> str:
    if flavour is Flavour.PAYLOAD and not is_typed_payload_array(expr):
        return PAYLOAD_SCALARS[JsonType.ARRAY]
    return f'std::vector<{to_cpp_type(expr.items, flavour)}>'


def _to_cpp_scalar(json_type: JsonType, flavour: Flavour) -> str:
    scalars = FIELD_SCALARS if flavour is Flavour.FIELD else PAYLOAD_SCALARS
    try:
        return scalars[json_type]
    except KeyError:
        raise UnsupportedDefinition(
            f'no C++ type for json type "{json_type.value}" as a {flavour.value}'
        ) from None
