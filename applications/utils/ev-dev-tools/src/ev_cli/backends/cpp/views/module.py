# SPDX-License-Identifier: Apache-2.0
# Copyright Pionix GmbH and Contributors to EVerest
"""The views a module's loader and skeleton sources are rendered from."""

from __future__ import annotations

from dataclasses import dataclass, field

from ....ir.modules import ConfigEntry, ModuleDef, Mutability, ProvidesDef, RequiresDef
from ....ir.types import Primitive
from .. import naming
from .. import mapping
from ..mapping import Flavour
from .common import TEMPLATE_VERSION


@dataclass
class ProvidedView:
    """One implementation a module provides, ready to render."""

    provided: ProvidesDef

    @property
    def id(self) -> str:
        return self.provided.id

    @property
    def interface(self) -> str:
        return self.provided.interface

    @property
    def class_name(self) -> str:
        return f'{self.provided.interface}Impl'

    @property
    def base_class(self) -> str:
        return f'{self.provided.interface}ImplBase'

    @property
    def base_class_header(self) -> str:
        return f'generated/interfaces/{self.provided.interface}/Implementation.hpp'

    @property
    def class_header(self) -> str:
        """The implementation's own header, relative to the module directory."""
        return f'{self.provided.id}/{self.class_name}.hpp'

    @property
    def cpp_file(self) -> str:
        return f'{self.provided.id}/{self.class_name}.cpp'

    @property
    def config(self) -> tuple[ConfigEntry, ...]:
        return read_only_config(self.provided.config)

    @property
    def rwconfig(self) -> tuple[ConfigEntry, ...]:
        return read_write_config(self.provided.config)

    def config_type(self, entry: ConfigEntry) -> str:
        return mapping.spell(Primitive(entry.type), Flavour.PAYLOAD)


@dataclass
class RequiredView:
    """One requirement a module declares, ready to render."""

    required: RequiresDef

    @property
    def id(self) -> str:
        return self.required.id

    @property
    def class_name(self) -> str:
        return f'{self.required.interface}Intf'

    @property
    def exports_header(self) -> str:
        return f'generated/interfaces/{self.required.interface}/Interface.hpp'

    @property
    def is_vector(self) -> bool:
        return self.required.is_vector


def read_only_config(entries) -> tuple[ConfigEntry, ...]:
    """Configuration a module reads but cannot be asked to change.

    Splitting a module's configuration in two is a presentation decision -- the
    IR records each entry's mutability and nothing more -- so it happens here.
    """
    return tuple(e for e in entries if e.mutability is not Mutability.READ_WRITE)


def read_write_config(entries) -> tuple[ConfigEntry, ...]:
    return tuple(e for e in entries if e.mutability is Mutability.READ_WRITE)


@dataclass
class ModuleView:
    """One ``modules/<path>/manifest.yaml``, ready to render."""

    module: ModuleDef
    blocks: dict = field(default_factory=dict)
    license_header: str = ''

    # -- names --------------------------------------------------------------

    @property
    def template_version(self) -> str:
        return TEMPLATE_VERSION

    @property
    def name(self) -> str:
        return self.module.name

    @property
    def class_name(self) -> str:
        return self.module.name

    @property
    def description(self) -> str:
        return self.module.description

    @property
    def guard(self) -> str:
        return naming.module_header_guard(self.module.name)

    @property
    def ld_ev_guard(self) -> str:
        return 'LD_EV_HPP'

    @property
    def ld_ev_header(self) -> str:
        return 'ld-ev.hpp'

    @property
    def module_header(self) -> str:
        return f'{self.module.name}.hpp'

    # -- manifest flags -----------------------------------------------------

    @property
    def enable_external_mqtt(self) -> bool:
        return self.module.enable_external_mqtt

    @property
    def enable_telemetry(self) -> bool:
        return self.module.enable_telemetry

    @property
    def enable_global_errors(self) -> bool:
        return self.module.enable_global_errors

    # -- structure ----------------------------------------------------------

    @property
    def provides(self) -> list[ProvidedView]:
        return [ProvidedView(p) for p in self.module.provides]

    @property
    def requires(self) -> list[RequiredView]:
        return [RequiredView(r) for r in self.module.requires]

    @property
    def config(self) -> tuple[ConfigEntry, ...]:
        return read_only_config(self.module.config)

    @property
    def rwconfig(self) -> tuple[ConfigEntry, ...]:
        return read_write_config(self.module.config)

    def config_type(self, entry: ConfigEntry) -> str:
        return mapping.spell(Primitive(entry.type), Flavour.PAYLOAD)

    # -- the runtime config client ------------------------------------------
    #
    # The generated loader declares it at most once, before whichever set of
    # read-write parameters comes first.

    @property
    def has_impl_rwconfig(self) -> bool:
        return any(p.rwconfig for p in self.provides)

    @property
    def has_module_rwconfig(self) -> bool:
        return bool(self.rwconfig)

    @property
    def declare_config_client_for_module(self) -> bool:
        return self.has_module_rwconfig and not self.has_impl_rwconfig

    # -- protected regions --------------------------------------------------

    def block(self, name: str, indent: int = 0) -> str:
        return self.blocks[name].render(indent)
