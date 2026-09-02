# SPDX-License-Identifier: Apache-2.0
# Copyright Pionix GmbH and Contributors to EVerest
"""The whole world, loaded once and then queried.

This is what replaces the previous implementation's module-level caches.  There,
interface and module generation each looped over every type calling the type
parser and discarding the result, purely so that a global dictionary would be
populated as a side effect.  Here the units are loaded into a :class:`Model` and
everything else asks it questions.
"""

from __future__ import annotations

from dataclasses import dataclass, field
from typing import Mapping, Union

from ..errors import ReferenceError as EvReferenceError
from .interfaces import InterfaceDef
from .modules import ModuleDef
from .types import EnumDef, StructDef, TypeRef, TypeUnit

Declaration = Union[StructDef, EnumDef]


@dataclass(frozen=True)
class Model:
    """Every definition ev-cli was asked to load."""

    types: Mapping[str, TypeUnit] = field(default_factory=dict)
    interfaces: Mapping[str, InterfaceDef] = field(default_factory=dict)
    modules: Mapping[str, ModuleDef] = field(default_factory=dict)

    # -- queries ------------------------------------------------------------

    def resolve(self, ref: TypeRef) -> Declaration:
        """The struct or enum a reference points at."""
        unit = self.types.get(ref.unit)
        if unit is None:
            raise EvReferenceError(
                f'reference {ref} points at type unit "{ref.unit}", which was not loaded'
            )
        declaration = unit.struct(ref.name) or unit.enum(ref.name)
        if declaration is None:
            raise EvReferenceError(
                f'reference {ref} names a type that "{ref.unit}" does not declare'
            )
        return declaration

    def is_enum(self, ref: TypeRef) -> bool:
        return isinstance(self.resolve(ref), EnumDef)

    def enum_of(self, ref: TypeRef) -> EnumDef | None:
        declaration = self.resolve(ref)
        return declaration if isinstance(declaration, EnumDef) else None

    def with_types(self, types: Mapping[str, TypeUnit]) -> Model:
        return Model(types=types, interfaces=self.interfaces, modules=self.modules)

    def with_interfaces(self, interfaces: Mapping[str, InterfaceDef]) -> Model:
        return Model(types=self.types, interfaces=interfaces, modules=self.modules)

    def with_modules(self, modules: Mapping[str, ModuleDef]) -> Model:
        return Model(types=self.types, interfaces=self.interfaces, modules=modules)
