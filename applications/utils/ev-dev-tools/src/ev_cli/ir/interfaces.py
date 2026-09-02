# SPDX-License-Identifier: Apache-2.0
# Copyright Pionix GmbH and Contributors to EVerest
"""The interface level intermediate representation.

One :class:`InterfaceDef` per ``interfaces/*.yaml`` file.  An interface
declares published variables, commands with a request/reply shape, and the
errors an implementation is allowed to raise.  Payload types reuse the type
level :data:`~ev_cli.ir.types.TypeExpr`, so a command argument and a struct
field are described the same way.
"""

from __future__ import annotations

from dataclasses import dataclass

from .common import NO_CONSTRAINTS, Constraints, SourceRef
from .types import TypeExpr


@dataclass(frozen=True)
class VarDef:
    """A variable an implementation publishes."""

    name: str
    description: str
    type: TypeExpr
    source: SourceRef
    constraints: Constraints = NO_CONSTRAINTS
    #: MQTT quality of service, if the interface asked for one.  The framework
    #: meta-schema allows it; nothing in everest-core sets it today.
    qos: int | None = None


@dataclass(frozen=True)
class ArgDef:
    """One argument of a command."""

    name: str
    description: str
    type: TypeExpr
    source: SourceRef
    constraints: Constraints = NO_CONSTRAINTS


@dataclass(frozen=True)
class CmdDef:
    """A command an implementation handles."""

    name: str
    description: str
    #: Argument order is part of the generated C++ API: the generated base
    #: class declares ``handle_<cmd>()`` taking them positionally, so
    #: reordering these is a source-breaking change for every implementer.
    arguments: tuple[ArgDef, ...]
    source: SourceRef
    #: ``None`` where the interface declares no result, which the framework
    #: meta-schema defaults to ``{type: 'null'}`` and C++ renders as ``void``.
    result: TypeExpr | None = None
    result_description: str | None = None
    result_constraints: Constraints = NO_CONSTRAINTS


@dataclass(frozen=True)
class ErrorDef:
    """One error, resolved from an ``/errors/<namespace>[#/<Name>]`` reference."""

    namespace: str
    name: str
    description: str
    source: SourceRef

    @property
    def full_name(self) -> str:
        return f'{self.namespace}/{self.name}'


@dataclass(frozen=True)
class InterfaceDef:
    """One ``interfaces/*.yaml`` file."""

    name: str
    description: str
    vars: tuple[VarDef, ...]
    cmds: tuple[CmdDef, ...]
    errors: tuple[ErrorDef, ...]
    #: Type units this interface references, which drives generated includes.
    referenced_units: frozenset[str]
    source: SourceRef

    def var(self, name: str) -> VarDef | None:
        return next((v for v in self.vars if v.name == name), None)

    def cmd(self, name: str) -> CmdDef | None:
        return next((c for c in self.cmds if c.name == name), None)
