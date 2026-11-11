# SPDX-License-Identifier: Apache-2.0
# Copyright Pionix GmbH and Contributors to EVerest
"""The view a module's implementation of an interface is rendered from."""

from __future__ import annotations

from dataclasses import dataclass, field

from ....ir.interfaces import ArgDef, CmdDef, InterfaceDef
from ....ir.modules import ConfigEntry, ModuleDef, ProvidesDef
from ....ir.model import Model
from ....ir.types import JsonType
from .. import naming
from .common import ProtectedRegions
from .interface import InterfaceView
from .module import ProvidedView


#: What ``module create`` writes for a command it cannot know the answer to.
DUMMY_RESULTS = {
    JsonType.BOOLEAN: 'true',
    JsonType.INTEGER: '42',
    JsonType.NUMBER: '3.14',
    JsonType.STRING: '"everest"',
    JsonType.OBJECT: '{}',
    JsonType.ARRAY: '{}',
    JsonType.NULL: '{}',
}


@dataclass
class ImplementationView(ProtectedRegions):
    """One interface implementation inside a module, ready to render."""

    provided: ProvidesDef
    interface: InterfaceDef
    module: ModuleDef
    model: Model
    blocks: dict = field(default_factory=dict)
    license_header: str = ''

    def __post_init__(self) -> None:
        self._provided = ProvidedView(self.provided)
        self._signatures = InterfaceView(interface=self.interface, model=self.model)

    @property
    def id(self) -> str:
        return self.provided.id

    @property
    def class_name(self) -> str:
        return self._provided.class_name

    @property
    def base_class(self) -> str:
        return self._provided.base_class

    @property
    def base_class_header(self) -> str:
        return self._provided.base_class_header

    @property
    def module_class(self) -> str:
        return self.module.name

    @property
    def module_header(self) -> str:
        """From an implementation subdirectory, the module header is one up."""
        return f'../{self.module.name}.hpp'

    @property
    def guard(self) -> str:
        return naming.implementation_header_guard(
            self.provided.id, self.provided.interface)

    @property
    def cmds(self):
        return self.interface.cmds

    @property
    def config(self) -> tuple[ConfigEntry, ...]:
        return self._provided.config

    @property
    def rwconfig(self) -> tuple[ConfigEntry, ...]:
        return self._provided.rwconfig

    def result_type(self, cmd: CmdDef) -> str:
        return self._signatures.result_type(cmd)

    def argument_declaration(self, argument: ArgDef) -> str:
        return self._signatures.argument_declaration(argument)

    def dummy_result(self, cmd: CmdDef) -> str:
        """A value of the right shape, for the skeleton to compile against."""
        json_types = self._signatures.json_types_of(cmd.result)
        if len(json_types) != 1:
            # A variant default-constructs to its first alternative.
            return '{}'
        return DUMMY_RESULTS[json_types[0]]
