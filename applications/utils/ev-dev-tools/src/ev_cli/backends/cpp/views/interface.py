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
from ..mapping import PAYLOAD_SCALARS, CppMapping, Flavour
from .common import MISSING_DESCRIPTION, TEMPLATE_VERSION


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

    def __post_init__(self) -> None:
        self._mapping = CppMapping(Flavour.PAYLOAD)

    # -- header level -------------------------------------------------------

    @property
    def template_version(self) -> str:
        return TEMPLATE_VERSION

    @property
    def name(self) -> str:
        return self.interface.name

    @property
    def vars(self) -> tuple[VarDef, ...]:
        return self.interface.vars

    @property
    def cmds(self) -> tuple[CmdDef, ...]:
        return self.interface.cmds

    @property
    def errors(self):
        return self.interface.errors

    def guard(self, header: naming.InterfaceHeaderFile) -> str:
        return naming.interface_header_guard(self.interface.name, header)

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

    def description(self, text: str | None) -> str:
        return MISSING_DESCRIPTION if text is None else text

    # -- payload classification --------------------------------------------

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
            # Only an array of referenced types becomes a typed vector; an
            # array of scalars travels as raw JSON and so needs no conversion.
            return PayloadKind.ARRAY if isinstance(payload.items, Ref) else PayloadKind.PLAIN
        return PayloadKind.PLAIN

    def is_null(self, payload: TypeExpr | None) -> bool:
        return self.kind(payload) is PayloadKind.NULL

    # -- payload spellings --------------------------------------------------

    def spell(self, payload: TypeExpr) -> str:
        return self._mapping.spell(payload).text

    def raw_spell(self, payload: TypeExpr) -> str:
        """The C++ type the framework hands over, before any conversion.

        A scalar arrives as itself, so this is what the value is cast to on the
        way in.
        """
        json_types = self.json_type_names(payload)
        if len(json_types) != 1:
            raise UnsupportedDefinition(
                f'no single raw type for a payload of {json_types}')
        return PAYLOAD_SCALARS[JsonType(json_types[0])]

    def result_type(self, cmd: CmdDef) -> str:
        """``void`` only when the interface declares no result at all.

        The meta-schema forbids a command result of type ``null``, so a
        declared result always has a type to spell.
        """
        return 'void' if cmd.result is None else self.spell(cmd.result)

    def argument_declaration(self, argument: ArgDef) -> str:
        """As the handler receives it: a mutable reference."""
        return f'{self.spell(argument.type)}& {argument.name}'

    def call_declaration(self, argument: ArgDef) -> str:
        """As a caller passes it: a const reference.

        Except for a variant, which is passed by value.  Nothing justifies the
        difference, but ``call_store`` in the kvs interface has that signature,
        and quietly changing a generated signature compiles everywhere and is
        noticed nowhere.
        """
        spelling = self.spell(argument.type)
        if self.kind(argument.type) is PayloadKind.VARIANT:
            return f'{spelling} {argument.name}'
        return f'const {spelling}& {argument.name}'

    # -- json type lists ----------------------------------------------------

    def json_type_names(self, payload: TypeExpr) -> tuple[str, ...]:
        """The json types the framework should expect for a payload.

        Derived from what the payload resolves to rather than from a ``type``
        written beside a ``$ref``.  For every reference in the tree the two
        agree; where they do not, the referenced definition is the one that
        tells the truth.
        """
        if isinstance(payload, Variant):
            return tuple(option.value for option in payload.options)
        if isinstance(payload, Array):
            return (JsonType.ARRAY.value,)
        if isinstance(payload, Opaque):
            return (payload.json_type.value,)
        if isinstance(payload, Primitive):
            return (payload.json_type.value,)
        if isinstance(payload, Ref):
            return (
                (JsonType.STRING.value,) if is_enum(self.model.resolve_type(payload.target))
                else (JsonType.OBJECT.value,)
            )
        raise UnsupportedDefinition(f'no json type for {payload!r}')

    def json_types(self, payload: TypeExpr) -> str:
        """Rendered as the framework's initialiser list, e.g. ``{"number"}``."""
        names = ', '.join(f'"{name}"' for name in self.json_type_names(payload))
        return f'{{{names}}}'

    # -- conversion helpers -------------------------------------------------

    def variant_alternatives(self, payload: TypeExpr) -> str:
        """The template arguments of the payload's ``std::variant``."""
        spelling = self.spell(payload)
        return spelling[len('std::variant<'):-1]
