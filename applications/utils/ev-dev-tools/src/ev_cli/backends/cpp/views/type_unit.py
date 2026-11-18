# SPDX-License-Identifier: Apache-2.0
# Copyright Pionix GmbH and Contributors to EVerest
"""The view ``types.hpp`` is rendered from."""

from __future__ import annotations

from dataclasses import dataclass

from ....ir.types import Array, Field, StructDef, TypeUnit
from .. import naming
from ..cpp_type import Flavour, to_cpp_type
from .common import MISSING_DESCRIPTION


@dataclass
class TypeUnitView:
    """One ``types/*.yaml`` file, ready to render.

    Every member goes through nlohmann; a generated enum's own to_json and
    from_json are found by ADL.
    """

    unit: TypeUnit

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

    @property
    def enums(self):
        return self.unit.enums

    @property
    def structs(self) -> tuple[StructDef, ...]:
        return self.unit.structs

    @classmethod
    def fields(cls, struct: StructDef) -> tuple[Field, ...]:
        """Required members first, each group in source order.

        The generated ``to_json`` builds the required part as a brace
        initialiser, and aggregate initialisation of a generated struct would
        otherwise change meaning.
        """
        return cls.required(struct) + cls.optional(struct)

    @staticmethod
    def required(struct: StructDef) -> tuple[Field, ...]:
        return tuple(f for f in struct.fields if f.required)

    @staticmethod
    def optional(struct: StructDef) -> tuple[Field, ...]:
        return tuple(f for f in struct.fields if not f.required)

    def description(self, text: str | None) -> str:
        return MISSING_DESCRIPTION if text is None else text

    @staticmethod
    def _cpp_type(field: Field) -> str:
        """The member's type, without the optional wrapper."""
        return to_cpp_type(field.type, Flavour.FIELD)

    def field_type(self, field: Field) -> str:
        """The member's declared type, wrapped when the property is optional."""
        cpp_type = self._cpp_type(field)
        return cpp_type if field.required else f'std::optional<{cpp_type}>'

    @staticmethod
    def _is_vector(field: Field) -> bool:
        """A struct member that is an array is always a typed vector."""
        return isinstance(field.type, Array)

    def identifier(self, field: Field) -> str:
        return naming.check_identifier(field.name, str(field.source))

    def from_json_required(self, field: Field) -> str:
        target = f'k.{field.name}'
        access = f'j.at("{field.name}")'
        if self._is_vector(field):
            return f'{target} = std::move({access}.get<{self._cpp_type(field)}>());'
        return f'{target} = {access};'

    def from_json_optional(self, field: Field) -> str:
        target = f'k.{field.name}'
        if self._is_vector(field):
            return (
                f'{target} = j.at("{field.name}").get<{self._cpp_type(field)}>();'
            )
        return f'{target} = it->get<{self._cpp_type(field)}>();'
