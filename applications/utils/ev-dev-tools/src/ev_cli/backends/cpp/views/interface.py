# SPDX-License-Identifier: Apache-2.0
# Copyright Pionix GmbH and Contributors to EVerest
"""The view the headers generated for every interface are rendered from."""

from __future__ import annotations

from dataclasses import dataclass
from enum import Enum

from ....errors import UnsupportedDefinition
from ....ir.interfaces import ArgDef, CmdDef, InterfaceDef, VarDef
from ....ir.model import Model
from ....ir.types import Array, JsonType, Opaque, Primitive, Ref, TypeExpr, Variant, is_enum
from .. import naming
from ..cpp_type import Flavour, is_typed_payload_array, to_cpp_type, variant_alternatives


class PayloadKind(Enum):
    """How a payload has to be converted on its way to and from JSON.

    The generated interface headers branch on exactly these cases, so they are
    named once here rather than rediscovered by a chain of template
    conditionals.
    """

    #: A reference to a declared struct or enum: converted by nlohmann through
    #: the type's own to_json and from_json.
    REF = 'ref'
    #: A vector of referenced types, converted the same way.
    ARRAY = 'array'
    #: Several possible json types.
    VARIANT = 'variant'
    #: No payload at all; the publish and subscribe signatures lose their
    #: parameter.
    NULL = 'null'
    #: A scalar, or raw JSON.
    PLAIN = 'plain'


@dataclass
class InterfaceView:
    """One ``interfaces/*.yaml`` file, ready to render."""

    interface: InterfaceDef
    model: Model

    @property
    def name(self) -> str:
        return self.interface.name

    def guard(self, filename: str) -> str:
        return naming.interface_header_guard(self.interface.name, filename)

    @property
    def implementation_class(self) -> str:
        return f'{self.interface.name}ImplBase'

    @property
    def interface_class(self) -> str:
        return f'{self.interface.name}Intf'

    @property
    def includes(self) -> list[str]:
        """Type headers for every unit this interface references."""
        return sorted(
            naming.type_header_include(unit)
            for unit in self.interface.referenced_type_units
        )

    @property
    def vars(self) -> tuple[VarDef, ...]:
        return self.interface.vars

    @property
    def cmds(self) -> tuple[CmdDef, ...]:
        return self.interface.cmds

    def kind(self, payload: TypeExpr | None) -> PayloadKind:
        if payload is None:
            return PayloadKind.NULL
        if isinstance(payload, Variant):
            return PayloadKind.VARIANT
        if isinstance(payload, Primitive) and payload.json_type is JsonType.NULL:
            return PayloadKind.NULL
        if isinstance(payload, Ref):
            return PayloadKind.REF
        if isinstance(payload, Array):
            return PayloadKind.ARRAY if is_typed_payload_array(payload) else PayloadKind.PLAIN
        return PayloadKind.PLAIN

    @staticmethod
    def cpp_type(payload: TypeExpr) -> str:
        return to_cpp_type(payload, Flavour.PAYLOAD)

    def result_type(self, cmd: CmdDef) -> str:
        """``void`` only when the interface declares no result at all.

        The meta-schema forbids a command result of type ``null``, so a
        declared result always has a C++ type.
        """
        return 'void' if cmd.result is None else self.cpp_type(cmd.result)

    def argument_declaration(self, argument: ArgDef) -> str:
        """As the handler receives it: a mutable reference."""
        return f'{self.cpp_type(argument.type)}& {argument.name}'

    def call_declaration(self, argument: ArgDef) -> str:
        """As a caller passes it: a const reference.

        Except for a variant, which is passed by value.  Nothing justifies the
        difference, but ``call_store`` in the kvs interface has that signature,
        and quietly changing a generated signature compiles everywhere and is
        noticed nowhere.
        """
        cpp_type = self.cpp_type(argument.type)
        if self.kind(argument.type) is PayloadKind.VARIANT:
            return f'{cpp_type} {argument.name}'
        return f'const {cpp_type}& {argument.name}'

    def json_types_of(self, payload: TypeExpr) -> tuple[JsonType, ...]:
        """The json types the framework should expect for a payload.

        Derived from what the payload resolves to rather than from a ``type``
        written beside a ``$ref``.  For every reference in the tree the two
        agree; where they do not, the referenced definition is the one that
        tells the truth.
        """
        if isinstance(payload, Variant):
            return payload.options
        if isinstance(payload, Array):
            return (JsonType.ARRAY,)
        if isinstance(payload, (Opaque, Primitive)):
            return (payload.json_type,)
        if isinstance(payload, Ref):
            return (
                (JsonType.STRING,) if is_enum(self.model.resolve_type(payload.target))
                else (JsonType.OBJECT,)
            )
        raise UnsupportedDefinition(f'no json type for {payload!r}')

    def json_types_initializer(self, payload: TypeExpr) -> str:
        """Rendered as the framework's initialiser list, e.g. ``{"number"}``."""
        names = ', '.join(f'"{json_type.value}"' for json_type in self.json_types_of(payload))
        return f'{{{names}}}'

    @staticmethod
    def variant_alternatives(payload: Variant) -> str:
        return variant_alternatives(payload)
