# SPDX-License-Identifier: Apache-2.0
# Copyright Pionix GmbH and Contributors to EVerest
"""The definitions one command works on.

A command builds a :class:`Model` holding what it needs, and the backends query
it instead of keeping caches of their own.
"""

from __future__ import annotations

from dataclasses import dataclass, field
from typing import Mapping

from ..errors import DefinitionNotFound
from ..schema.references import TypeRef
from .interfaces import InterfaceDef
from .modules import ModuleDef
from .types import Declaration, TypeUnit


@dataclass(frozen=True)
class Model:
    """The type units, interfaces and modules a command loaded."""

    types: Mapping[str, TypeUnit] = field(default_factory=dict)
    interfaces: Mapping[str, InterfaceDef] = field(default_factory=dict)
    modules: Mapping[str, ModuleDef] = field(default_factory=dict)

    def resolve_type(self, ref: TypeRef) -> Declaration:
        """The struct or enum a type reference names."""
        unit = self.types.get(ref.unit)
        if unit is None:
            raise DefinitionNotFound(
                f'reference {ref} points at type unit "{ref.unit}", which was not loaded'
            )
        declaration = unit.struct(ref.name) or unit.enum(ref.name)
        if declaration is None:
            raise DefinitionNotFound(
                f'reference {ref} names a type that "{ref.unit}" does not declare'
            )
        return declaration
