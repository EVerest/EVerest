# SPDX-License-Identifier: Apache-2.0
# Copyright Pionix GmbH and Contributors to EVerest
"""The module level intermediate representation.

One :class:`ModuleDef` per ``modules/*/manifest.yaml``.  A manifest says which
interfaces a module implements, which it needs from others, and what it can be
configured with.
"""

from __future__ import annotations

from dataclasses import dataclass, field
from enum import Enum
from typing import Any, Mapping

from .common import NO_CONSTRAINTS, Constraints, SourceRef
from .types import JsonType


class Mutability(Enum):
    """Whether a configuration value may be written at runtime.

    The manifest spells these ``ReadOnly``, ``ReadWrite`` and ``WriteOnly``.
    Note that partitioning a module's configuration into read-only and
    read-write groups is a *presentation* decision and belongs to a backend;
    here each entry simply carries its own mutability.
    """

    READ_ONLY = 'ReadOnly'
    READ_WRITE = 'ReadWrite'
    WRITE_ONLY = 'WriteOnly'


@dataclass(frozen=True)
class ConfigEntry:
    """One configuration value of a module or of one of its implementations."""

    name: str
    description: str
    #: The manifest meta-schema permits only boolean, integer, number, string.
    type: JsonType
    source: SourceRef
    mutability: Mutability = Mutability.READ_ONLY
    constraints: Constraints = NO_CONSTRAINTS

    @property
    def default(self) -> Any:
        return self.constraints.default

    @property
    def has_default(self) -> bool:
        return self.constraints.has_default


@dataclass(frozen=True)
class ProvidesDef:
    """An interface implementation a module offers."""

    #: The implementation id, e.g. ``main``.
    id: str
    interface: str
    description: str
    config: tuple[ConfigEntry, ...]
    source: SourceRef
    #: The manifest allows arbitrary extra primitive properties here, which the
    #: framework uses to match requirements against implementations.  Nothing
    #: in code generation reads them; they are kept so the IR stays lossless.
    extra: Mapping[str, Any] = field(default_factory=dict)


@dataclass(frozen=True)
class RequiresDef:
    """An interface a module needs another module to provide."""

    #: The requirement id, referenced from the connections in a run config.
    id: str
    interface: str
    source: SourceRef
    min_connections: int = 1
    max_connections: int = 1
    #: The ``ignore`` block, used by the Rust bindings.
    ignore_vars: tuple[str, ...] = ()
    ignore_errors: bool = False

    @property
    def is_vector(self) -> bool:
        """Whether this requirement can be fulfilled more than once.

        Derived rather than stored: the manifest defaults both bounds to one,
        and anything other than exactly-one becomes a vector in generated code.
        """
        return not (self.min_connections == 1 and self.max_connections == 1)


@dataclass(frozen=True)
class ModuleMetadata:
    license: str
    authors: tuple[str, ...]
    base_license: str | None = None
    #: The manifest allows further metadata keys; kept for losslessness.
    extra: Mapping[str, Any] = field(default_factory=dict)


@dataclass(frozen=True)
class ModuleDef:
    """One ``modules/<path>/manifest.yaml``."""

    name: str
    description: str
    metadata: ModuleMetadata
    config: tuple[ConfigEntry, ...]
    provides: tuple[ProvidesDef, ...]
    requires: tuple[RequiresDef, ...]
    source: SourceRef
    enable_external_mqtt: bool = False
    enable_telemetry: bool = False
    enable_global_errors: bool = False
    #: Linux capabilities the module's process needs, e.g. ``CAP_NET_RAW``.
    #: Nothing in code generation reads them; they are carried so that a
    #: packaging or documentation backend does not have to re-read the manifest.
    capabilities: tuple[str, ...] = ()

    def provided(self, implementation_id: str) -> ProvidesDef | None:
        return next((p for p in self.provides if p.id == implementation_id), None)
