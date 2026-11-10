# SPDX-License-Identifier: Apache-2.0
# Copyright Pionix GmbH and Contributors to EVerest
"""The view ``types.hpp`` is rendered from."""

from __future__ import annotations

from dataclasses import dataclass

from ....ir.model import Model
from ....ir.types import Array, Field, StructDef, TypeUnit
from .. import naming
from ..mapping import CppMapping, Flavour
from ..order import order_fields, order_structs
from .common import MISSING_DESCRIPTION, TEMPLATE_VERSION


@dataclass
class TypeUnitView:
    """One ``types/*.yaml`` file, ready to render."""

    unit: TypeUnit
    model: Model

    def __post_init__(self) -> None:
        self._mapping = CppMapping(Flavour.FIELD)

    # -- header level -------------------------------------------------------

    @property
    def template_version(self) -> str:
        return TEMPLATE_VERSION

    @property
    def name(self) -> str:
        """The unit's namespace, as it appears in generated comments."""
        return self.unit.name

    @property
    def guard(self) -> str:
        return naming.type_header_guard(self.unit.name)

    @property
    def namespaces(self) -> list[str]:
        return naming.unit_namespaces(self.unit.name)

    @property
    def includes(self) -> list[str]:
        """Headers for the other units this one references.

        Its own header is excluded, which matters because a unit may perfectly
        well reference a type declared beside the one referencing it.
        """
        return sorted(
            naming.type_header_include(referenced)
            for referenced in self.unit.referenced_type_units
            if referenced != self.unit.name
        )

    # -- declarations -------------------------------------------------------

    @property
    def enums(self):
        return self.unit.enums

    @property
    def structs(self) -> tuple[StructDef, ...]:
        return order_structs(self.unit.structs)

    @staticmethod
    def fields(struct: StructDef) -> tuple[Field, ...]:
        return order_fields(struct.fields)

    @staticmethod
    def required(struct: StructDef) -> tuple[Field, ...]:
        return tuple(f for f in struct.fields if f.required)

    @staticmethod
    def optional(struct: StructDef) -> tuple[Field, ...]:
        return tuple(f for f in struct.fields if not f.required)

    def description(self, text: str | None) -> str:
        return MISSING_DESCRIPTION if text is None else text

    # -- types --------------------------------------------------------------

    def type_of(self, field: Field) -> str:
        """The member's type, without the optional wrapper."""
        return self._mapping.spell(field.type)

    def field_type(self, field: Field) -> str:
        """The member's declared type, wrapped when the property is optional."""
        spelling = self.type_of(field)
        return spelling if field.required else f'std::optional<{spelling}>'

    @staticmethod
    def is_vector(field: Field) -> bool:
        """A struct member that is an array is always a typed vector."""
        return isinstance(field.type, Array)

    def identifier(self, field: Field) -> str:
        return naming.check_identifier(field.name, str(field.source))

    # -- conversions --------------------------------------------------------
    #
    # Every member goes through nlohmann; a generated enum's own to_json and
    # from_json are found by ADL.

    def from_json_required(self, field: Field) -> str:
        target = f'k.{field.name}'
        access = f'j.at("{field.name}")'
        if self.is_vector(field):
            return f'{target} = std::move({access}.get<{self.type_of(field)}>());'
        return f'{target} = {access};'

    def from_json_optional(self, field: Field) -> str:
        target = f'k.{field.name}'
        if self.is_vector(field):
            return (
                f'{target} = j.at("{field.name}").get<{self.type_of(field)}>();'
            )
        return f'{target} = it->get<{self.type_of(field)}>();'
