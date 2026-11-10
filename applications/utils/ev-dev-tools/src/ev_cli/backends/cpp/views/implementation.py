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
from .common import TEMPLATE_VERSION
from .interface import InterfaceView
from .module import ProvidedView


#: What ``module create`` writes for a command it cannot know the answer to.
DUMMY_RESULTS = {
    JsonType.BOOLEAN.value: 'true',
    JsonType.INTEGER.value: '42',
    JsonType.NUMBER.value: '3.14',
    JsonType.STRING.value: '"everest"',
    JsonType.OBJECT.value: '{}',
    JsonType.ARRAY.value: '{}',
    JsonType.NULL.value: '{}',
}


@dataclass
class ImplementationView:
    """One interface implementation inside a module, ready to render."""

    provided: ProvidesDef
    interface: InterfaceDef
    module: ModuleDef
    model: Model
    blocks: dict = field(default_factory=dict)
    license_header: str = ''

    def __post_init__(self) -> None:
        self._provided = ProvidedView(self.provided, self.model)
        self._signatures = InterfaceView(interface=self.interface, model=self.model)

    # -- names --------------------------------------------------------------

    @property
    def template_version(self) -> str:
        return TEMPLATE_VERSION

    @property
    def id(self) -> str:
        return self.provided.id

    @property
    def class_name(self) -> str:
        return self._provided.class_name

    @property
    def class_parent(self) -> str:
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

    # -- structure ----------------------------------------------------------

    @property
    def cmds(self):
        return self.interface.cmds

    @property
    def config(self) -> tuple[ConfigEntry, ...]:
        return self._provided.config

    @property
    def rwconfig(self) -> tuple[ConfigEntry, ...]:
        return self._provided.rwconfig

    def config_type(self, entry: ConfigEntry) -> str:
        return self._provided.config_type(entry)

    # -- command signatures -------------------------------------------------

    def result_type(self, cmd: CmdDef) -> str:
        return self._signatures.result_type(cmd)

    def argument_declaration(self, argument: ArgDef) -> str:
        return self._signatures.argument_declaration(argument)

    def dummy_result(self, cmd: CmdDef) -> str:
        """A value of the right shape, for the skeleton to compile against."""
        names = self._signatures.json_type_names(cmd.result)
        if len(names) != 1:
            # A variant default-constructs to its first alternative.
            return '{}'
        return DUMMY_RESULTS[names[0]]

    # -- protected regions --------------------------------------------------

    def block(self, name: str, indent: int = 0) -> str:
        return self.blocks[name].render(indent)
