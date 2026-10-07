# SPDX-License-Identifier: Apache-2.0
# Copyright Pionix GmbH and Contributors to EVerest
"""What the C++ templates are handed.

The templates consume the IR directly, but every question about C++ -- how a
type is spelled, which conversion helper applies, what the include guard is --
is answered here rather than in Jinja.  That keeps the templates declarative
and, more usefully, keeps the answers in Python where they can be unit tested.
"""

from __future__ import annotations

from dataclasses import dataclass, field
from enum import Enum

from ...errors import UnsupportedDefinition
from ...ir.interfaces import ArgDef, CmdDef, InterfaceDef, VarDef
from ...ir.modules import ConfigEntry, ModuleDef, Mutability, ProvidesDef, RequiresDef
from ...ir.model import Model
from ...ir.types import (
    Array,
    Field,
    JsonType,
    Opaque,
    Primitive,
    Ref,
    StructDef,
    TypeExpr,
    TypeUnit,
    Variant,
)
from . import naming
from .mapping import PAYLOAD_SCALARS, CppMapping, Flavour

#: Emitted into every generated header.  Bumped when the templates change in a
#: way that should invalidate already-generated output.
TEMPLATE_VERSION = '6'

#: What the previous generator substituted for a property with no description,
#: and therefore what appears in the headers modules read today.
MISSING_DESCRIPTION = 'TODO: description'


@dataclass
class TypeUnitView:
    """One ``types/*.yaml`` file, ready to render."""

    unit: TypeUnit
    model: Model

    def __post_init__(self) -> None:
        self._mapping = CppMapping(self.model, Flavour.FIELD)

    # -- header level -------------------------------------------------------

    @property
    def template_version(self) -> str:
        return TEMPLATE_VERSION

    @property
    def name(self) -> str:
        """The unit's namespace path, as it appears in generated comments."""
        return naming.unit_namespace_path(self.unit.unit)

    @property
    def guard(self) -> str:
        return naming.type_header_guard(self.unit.unit)

    @property
    def namespaces(self) -> list[str]:
        return naming.unit_namespaces(self.unit.unit)

    @property
    def includes(self) -> list[str]:
        """Headers for the other units this one references.

        Its own header is excluded, which matters because a unit may perfectly
        well reference a type declared beside the one referencing it.
        """
        return sorted(
            naming.type_header_include(referenced)
            for referenced in self.unit.referenced_units
            if referenced != self.unit.unit
        )

    # -- declarations -------------------------------------------------------

    @property
    def enums(self):
        return self.unit.enums

    @property
    def structs(self):
        return self.unit.structs

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
        return self._mapping.spell(field.type).text

    def field_type(self, field: Field) -> str:
        """The member's declared type, wrapped when the property is optional."""
        return self._mapping.field_type(field)

    def is_enum(self, field: Field) -> bool:
        return self._mapping.spell(field.type).is_enum

    def is_vector(self, field: Field) -> bool:
        return self._mapping.spell(field.type).is_vector

    def identifier(self, field: Field) -> str:
        return naming.check_identifier(field.name, str(field.source))

    # -- conversions --------------------------------------------------------
    #
    # A vector is handed to nlohmann whole, even when its elements are enums:
    # the element type's own to_json/from_json is found by ADL.  Only a scalar
    # enum needs the string helpers named explicitly.

    def to_json_value(self, field: Field, expression: str) -> str:
        if self.is_vector(field) or not self.is_enum(field):
            return expression
        return f'{naming.enum_to_string_view(self.type_of(field))}({expression})'

    def from_json_required(self, field: Field) -> str:
        target = f'k.{field.name}'
        access = f'j.at("{field.name}")'
        if self.is_vector(field):
            return f'{target} = std::move({access}.get<{self.type_of(field)}>());'
        if self.is_enum(field):
            return f'{target} = {naming.string_to_enum(self.type_of(field))}({access});'
        return f'{target} = {access};'

    def from_json_optional(self, field: Field) -> str:
        target = f'k.{field.name}'
        if self.is_vector(field):
            return (
                f'{target} = j.at("{field.name}").get<{self.type_of(field)}>();'
            )
        if self.is_enum(field):
            return (
                f'{target} = '
                f'{naming.string_to_enum(self.type_of(field))}(it->get<std::string>());'
            )
        return f'{target} = it->get<{self.type_of(field)}>();'


class PayloadKind(Enum):
    """How a payload has to be converted on its way to and from JSON.

    The generated interface headers branch on exactly these six cases, so they
    are named once here rather than rediscovered by a chain of template
    conditionals.
    """

    #: A reference to a declared struct: handed to nlohmann as it is.
    OBJECT = 'object'
    #: A reference to a declared enum: converted through the string helpers.
    ENUM = 'enum'
    #: A vector of referenced types, converted element by element when those
    #: elements are enums.
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
        self._mapping = CppMapping(self.model, Flavour.PAYLOAD)

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

    def guard(self, kind: str) -> str:
        return naming.interface_header_guard(self.interface.name, kind)

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
            for unit in self.interface.referenced_units
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
            return (
                PayloadKind.ENUM if self.model.is_enum(payload.target)
                else PayloadKind.OBJECT
            )
        if isinstance(payload, Array):
            # Only an array of referenced types becomes a typed vector; an
            # array of scalars travels as raw JSON and so needs no conversion.
            return (
                PayloadKind.ARRAY if isinstance(payload.items, Ref)
                else PayloadKind.PLAIN
            )
        return PayloadKind.PLAIN

    def is_null(self, payload: TypeExpr | None) -> bool:
        return self.kind(payload) is PayloadKind.NULL

    # -- payload spellings --------------------------------------------------

    def spell(self, payload: TypeExpr) -> str:
        return self._mapping.spell(payload).text

    def raw_spell(self, payload: TypeExpr) -> str:
        """The C++ type the framework hands over, before any conversion.

        An enum arrives as a string and a scalar as itself, so this is what the
        value is cast to on the way in.
        """
        json_types = self.json_type_names(payload)
        if len(json_types) != 1:
            raise UnsupportedDefinition(
                f'no single raw type for a payload of {json_types}')
        return PAYLOAD_SCALARS[JsonType(json_types[0])]

    def element(self, payload: TypeExpr) -> str:
        """The element type of a typed vector payload."""
        assert isinstance(payload, Array)
        return self._mapping.spell(payload.items).text

    def element_is_enum(self, payload: TypeExpr) -> bool:
        return self._mapping.spell(payload).element_is_enum

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

        Except for a variant, which is passed by value.  There is no reason for
        the difference -- the previous implementation's type-spelling macro
        returned early for variants and never reached the code that added the
        ``const`` and the ``&`` -- but ``call_store`` in the kvs interface has
        that signature today, and quietly changing a generated signature is
        exactly the kind of change that compiles everywhere and is noticed
        nowhere.  Reconciling it is its own change.
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
                (JsonType.STRING.value,) if self.model.is_enum(payload.target)
                else (JsonType.OBJECT.value,)
            )
        raise UnsupportedDefinition(f'no json type for {payload!r}')

    def json_types(self, payload: TypeExpr) -> str:
        """Rendered as the framework's initialiser list, e.g. ``{"number"}``."""
        names = ', '.join(f'"{name}"' for name in self.json_type_names(payload))
        return f'{{{names}}}'

    # -- conversion helpers -------------------------------------------------

    def to_string(self, payload: TypeExpr) -> str:
        return naming.enum_to_string(self.spell(payload))

    def to_string_view(self, payload: TypeExpr) -> str:
        return naming.enum_to_string_view(self.spell(payload))

    def from_string(self, payload: TypeExpr) -> str:
        return naming.string_to_enum(self.spell(payload))

    def element_to_string(self, payload: TypeExpr) -> str:
        return naming.enum_to_string(self.element(payload))

    def element_from_string(self, payload: TypeExpr) -> str:
        return naming.string_to_enum(self.element(payload))

    def variant_alternatives(self, payload: TypeExpr) -> str:
        """The template arguments of the payload's ``std::variant``."""
        spelling = self.spell(payload)
        return spelling[len('std::variant<'):-1]


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
class ProvidedView:
    """One implementation a module provides, ready to render."""

    provided: ProvidesDef
    model: Model

    def __post_init__(self) -> None:
        self._mapping = CppMapping(self.model, Flavour.PAYLOAD)

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
        return self._mapping.spell(Primitive(entry.type)).text


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
    model: Model
    blocks: dict = field(default_factory=dict)
    license_header: str = ''

    def __post_init__(self) -> None:
        self._mapping = CppMapping(self.model, Flavour.PAYLOAD)

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
        return [ProvidedView(p, self.model) for p in self.module.provides]

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
        return self._mapping.spell(Primitive(entry.type)).text

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
