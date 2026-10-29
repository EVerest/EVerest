# SPDX-License-Identifier: Apache-2.0
# Copyright Pionix GmbH and Contributors to EVerest
"""Turning validated definition documents into the IR.

Each unit is built by its own short-lived :class:`_UnitBuild`, so no state
survives between units and the order units are built in cannot matter.
"""

from __future__ import annotations

from pathlib import Path
from typing import Any, Iterable, Mapping, Sequence

from ..errors import DefinitionNotFound, EvCliError, UnsupportedDefinition
from ..schema import normalize
from ..schema.diagnostics import Category, Diagnostic, Severity, Sink
from ..schema.loader import DefinitionLoader
from ..schema.references import parse_error_reference, parse_type_reference
from .common import Constraints, SourceRef
from .interfaces import ArgDef, CmdDef, ErrorDef, InterfaceDef, VarDef
from .model import Model
from .modules import (
    ConfigEntry,
    ModuleDef,
    ModuleMetadata,
    Mutability,
    ProvidesDef,
    RequiresDef,
)
from .order import order_fields, order_structs
from .types import (
    Array,
    EnumDef,
    Field,
    JsonType,
    LocalEnum,
    LocalStruct,
    Opaque,
    Primitive,
    Ref,
    StructDef,
    TypeExpr,
    TypeRef,
    TypeUnit,
    Variant,
)

SCALAR_TYPES = frozenset({
    JsonType.BOOLEAN, JsonType.INTEGER, JsonType.NUMBER, JsonType.STRING, JsonType.NULL,
})


def capitalize_first(name: str) -> str:
    """Upper-case the first character and leave the rest alone.

    EVerest's declared type names are already PascalCase, so this matters only
    for declarations written inline and named after a snake_case property.
    """
    return name[:1].upper() + name[1:] if name else name


def json_type_of(declared: Any, source: SourceRef) -> JsonType:
    try:
        return JsonType(declared)
    except ValueError:
        raise UnsupportedDefinition(
            f'{source}: unknown json type {declared!r}'
        ) from None


def target_json_type(
    loader: DefinitionLoader, target: TypeRef, source: SourceRef
) -> str | None:
    """What the referenced definition declares.

    Looked up for two reasons: it proves the reference resolves, and it is what
    makes a redundant sibling ``type`` distinguishable from a contradictory one.
    """
    try:
        document = loader.type_document(target.unit)
    except DefinitionNotFound as err:
        raise DefinitionNotFound(f'{source}: {err}') from err

    declared = (document.data.get('types') or {}).get(target.name)
    if declared is None:
        raise DefinitionNotFound(
            f'{source}: reference to {target} names a type that unit '
            f'"{target.unit}" does not declare'
        )
    return declared.get('type')


def resolve_reference(
    loader: DefinitionLoader,
    sink: Sink,
    schema: Mapping[str, Any],
    source: SourceRef,
    *,
    from_unit: str | None,
) -> Ref:
    """Parse, validate and report one ``$ref`` node.

    Shared by every builder so that both reference dialects are accepted, and
    the legacy one reported, in exactly one place.
    """
    reference = parse_type_reference(schema['$ref'], from_unit=from_unit)
    normalize.report_reference(
        dict(schema),
        reference=reference,
        source=source,
        from_unit=from_unit,
        target_json_type=target_json_type(loader, reference.target, source),
        sink=sink,
    )
    return Ref(reference.target)


class TypeUnitBuilder:
    """Builds :class:`TypeUnit` values from the loader's documents."""

    def __init__(self, loader: DefinitionLoader, sink: Sink) -> None:
        self._loader = loader
        self._sink = sink

    def build(self, unit: str) -> TypeUnit:
        document = self._loader.type_document(unit)
        return _UnitBuild(unit, document.data, document.source, self._loader, self._sink).run()


class _UnitBuild:
    """The build of a single type unit."""

    def __init__(self, unit, data, source, loader: DefinitionLoader, sink: Sink) -> None:
        self.unit = unit
        self.data = data
        self.source = source
        self.loader = loader
        self.sink = sink
        # Declared enums keep their source order and come first; enums written
        # inline are promoted and appended after them, in discovery order.
        self._declared_enums: list[EnumDef] = []
        self._promoted_enums: list[EnumDef] = []
        self._structs: list[StructDef] = []
        self._referenced_units: set[str] = set()

    # -- entry point --------------------------------------------------------

    def run(self) -> TypeUnit:
        root = self.source.child('types')
        for name, schema in (self.data.get('types') or {}).items():
            self._declare(name, schema, root.child(name))

        enums = tuple(self._declared_enums) + tuple(self._promoted_enums)
        structs = order_structs(self._structs)
        self._reject_name_clashes(enums, structs)

        return TypeUnit(
            unit=self.unit,
            description=self.data['description'],
            enums=enums,
            structs=structs,
            referenced_units=frozenset(self._referenced_units),
            source=self.source,
        )

    def _reject_name_clashes(
        self, enums: Sequence[EnumDef], structs: Sequence[StructDef]
    ) -> None:
        """Two declarations of the same name would not compile once emitted."""
        seen: dict[str, SourceRef] = {}
        for declaration in (*enums, *structs):
            previous = seen.get(declaration.name)
            if previous is not None:
                raise UnsupportedDefinition(
                    f'{declaration.source}: "{declaration.name}" is already '
                    f'declared at {previous} in the same unit'
                )
            seen[declaration.name] = declaration.source

    # -- declarations -------------------------------------------------------

    def _declare(self, name: str, schema: Mapping[str, Any], source: SourceRef) -> None:
        declared = schema.get('type')

        if declared == JsonType.STRING.value and 'enum' in schema:
            self._declared_enums.append(EnumDef(
                name=capitalize_first(name),
                description=schema.get('description', ''),
                values=tuple(schema['enum']),
                source=source,
            ))
            return

        if declared == JsonType.OBJECT.value and 'properties' in schema:
            self._build_struct(capitalize_first(name), schema, source)
            return

        # Only these two shapes occur across everest-core, and only these two
        # have ever produced anything in a generated header.  Anything else was
        # previously accepted and then silently dropped, leaving a header that
        # referred to a type it did not declare; saying so is better.
        raise UnsupportedDefinition(
            f'{source}: a declared type must be either an object with '
            f'properties or a string with an enum, but this one declares '
            f'type={declared!r}'
        )

    def _build_struct(
        self, name: str, schema: Mapping[str, Any], source: SourceRef
    ) -> StructDef:
        required = set(schema.get('required') or [])
        depends_on: list[str] = []
        fields: list[Field] = []

        for property_name, property_schema in (schema.get('properties') or {}).items():
            if not property_name.isidentifier():
                raise UnsupportedDefinition(
                    f'{source}: property name "{property_name}" is not usable as '
                    f'an identifier in generated code'
                )
            property_source = source.child('properties', property_name)
            fields.append(Field(
                name=property_name,
                description=property_schema.get('description'),
                type=self._type_expr(
                    property_schema, property_source,
                    enclosing_name=property_name, depends_on=depends_on,
                ),
                required=property_name in required,
                source=property_source,
                constraints=Constraints.from_schema(property_schema),
            ))

        struct = StructDef(
            name=name,
            description=schema.get('description', ''),
            fields=order_fields(fields),
            depends_on=tuple(depends_on),
            source=source,
            constraints=Constraints.from_schema(schema),
        )
        self._structs.append(struct)
        return struct

    # -- type expressions ---------------------------------------------------

    def _type_expr(
        self,
        schema: Mapping[str, Any],
        source: SourceRef,
        *,
        enclosing_name: str,
        depends_on: list[str],
    ) -> TypeExpr:
        if '$ref' in schema:
            return self._reference(schema, source)

        declared = schema.get('type')
        if declared is None:
            raise UnsupportedDefinition(
                f'{source}: neither a "type" nor a "$ref"'
            )

        if isinstance(declared, list):
            return Variant(tuple(json_type_of(entry, source) for entry in declared))

        json_type = json_type_of(declared, source)

        if json_type is JsonType.STRING and 'enum' in schema:
            return self._promote_enum(enclosing_name, schema, source)

        if json_type in SCALAR_TYPES:
            return Primitive(json_type)

        if json_type is JsonType.ARRAY:
            items = schema.get('items')
            if items is None:
                raise UnsupportedDefinition(
                    f'{source}: an array must declare its "items"'
                )
            return Array(self._type_expr(
                items, source.child('items'),
                enclosing_name=enclosing_name, depends_on=depends_on,
            ))

        if json_type is JsonType.OBJECT:
            if 'properties' in schema:
                return self._promote_struct(enclosing_name, schema, source, depends_on)
            # An object with nothing said about its contents travels as raw
            # JSON; the framework passes it through untouched.
            return Opaque(JsonType.OBJECT)

        raise UnsupportedDefinition(f'{source}: cannot express type {declared!r}')

    def _reference(self, schema: Mapping[str, Any], source: SourceRef) -> Ref:
        reference = resolve_reference(
            self.loader, self.sink, schema, source, from_unit=self.unit
        )
        self._referenced_units.add(reference.target.unit)
        return reference

    # -- promotion of inline declarations -----------------------------------

    def _promote_enum(
        self, enclosing_name: str, schema: Mapping[str, Any], source: SourceRef
    ) -> LocalEnum:
        name = capitalize_first(enclosing_name)
        values = tuple(schema['enum'])

        existing = next((e for e in self._promoted_enums if e.name == name), None)
        if existing is not None:
            if existing.values != values:
                raise UnsupportedDefinition(
                    f'{source}: an inline enum named "{name}" was already '
                    f'promoted at {existing.source} with different values'
                )
            return LocalEnum(name)

        self._promoted_enums.append(EnumDef(
            name=name,
            description=schema.get('description', ''),
            values=values,
            source=source,
        ))
        return LocalEnum(name)

    def _promote_struct(
        self,
        enclosing_name: str,
        schema: Mapping[str, Any],
        source: SourceRef,
        depends_on: list[str],
    ) -> LocalStruct:
        name = capitalize_first(enclosing_name)
        depends_on.append(name)
        if not any(struct.name == name for struct in self._structs):
            self._build_struct(name, schema, source)
        return LocalStruct(name)


# --------------------------------------------------------------------------
# Interfaces
# --------------------------------------------------------------------------

class InterfaceBuilder:
    """Builds :class:`InterfaceDef` values from the loader's documents."""

    def __init__(self, loader: DefinitionLoader, sink: Sink) -> None:
        self._loader = loader
        self._sink = sink

    def build(self, name: str) -> InterfaceDef:
        document = self._loader.interface_document(name)
        return _InterfaceBuild(
            name, document.data, document.source, self._loader, self._sink
        ).run()


class _InterfaceBuild:
    """The build of a single interface.

    Payload types differ from struct fields in one respect: an interface has
    nowhere to *declare* a type, so anything written inline cannot be promoted.
    An inline enum keeps its values as a constraint and travels as a string; an
    inline object travels as raw JSON, with a diagnostic saying so.
    """

    def __init__(self, name, data, source, loader: DefinitionLoader, sink: Sink) -> None:
        self.name = name
        self.data = data
        self.source = source
        self.loader = loader
        self.sink = sink
        self._referenced_units: set[str] = set()

    def run(self) -> InterfaceDef:
        return InterfaceDef(
            name=self.name,
            description=self.data['description'],
            vars=tuple(
                self._var(name, schema, self.source.child('vars', name))
                for name, schema in (self.data.get('vars') or {}).items()
            ),
            cmds=tuple(
                self._cmd(name, schema, self.source.child('cmds', name))
                for name, schema in (self.data.get('cmds') or {}).items()
            ),
            errors=self._errors(self.data.get('errors') or []),
            referenced_units=frozenset(self._referenced_units),
            source=self.source,
        )

    # -- vars and cmds ------------------------------------------------------

    def _var(self, name: str, schema: Mapping[str, Any], source: SourceRef) -> VarDef:
        return VarDef(
            name=name,
            description=schema['description'],
            type=self._payload(schema, source),
            source=source,
            constraints=Constraints.from_schema(schema),
            qos=schema.get('qos'),
        )

    def _cmd(self, name: str, schema: Mapping[str, Any], source: SourceRef) -> CmdDef:
        arguments = tuple(
            ArgDef(
                name=argument,
                description=argument_schema['description'],
                type=self._payload(
                    argument_schema, source.child('arguments', argument)),
                source=source.child('arguments', argument),
                constraints=Constraints.from_schema(argument_schema),
            )
            # Declaration order is the order of the generated handler's
            # parameters, so it is taken from the document as written.
            for argument, argument_schema in (schema.get('arguments') or {}).items()
        )

        result_schema = schema.get('result')
        result = None
        if result_schema is not None:
            result = self._payload(result_schema, source.child('result'))

        return CmdDef(
            name=name,
            description=schema['description'],
            arguments=arguments,
            source=source,
            result=result,
            result_description=(
                result_schema.get('description') if result_schema else None),
            result_constraints=(
                Constraints.from_schema(result_schema) if result_schema
                else Constraints()
            ),
        )

    # -- payloads -----------------------------------------------------------

    def _payload(self, schema: Mapping[str, Any], source: SourceRef) -> TypeExpr:
        if '$ref' in schema:
            reference = resolve_reference(
                self.loader, self.sink, schema, source, from_unit=None
            )
            self._referenced_units.add(reference.target.unit)
            return reference

        declared = schema.get('type')
        if declared is None:
            raise UnsupportedDefinition(f'{source}: no "type" and no "$ref"')

        if isinstance(declared, list):
            return Variant(tuple(json_type_of(entry, source) for entry in declared))

        json_type = json_type_of(declared, source)

        if json_type is JsonType.ARRAY:
            items = schema.get('items')
            if items is None:
                # Nothing said about the elements: the payload travels as raw
                # JSON rather than as a typed vector.
                return Opaque(JsonType.ARRAY)
            return Array(self._payload(items, source.child('items')))

        if json_type is JsonType.OBJECT:
            if 'properties' in schema:
                self.sink.emit(Diagnostic(
                    category=Category.SCHEMA_WARNING,
                    severity=Severity.WARNING,
                    source=source,
                    message=(
                        'an interface payload declares properties inline. There '
                        'is nowhere in a generated interface header to declare '
                        'the resulting type, so the payload is passed through as '
                        'raw JSON and the properties have no effect.'
                    ),
                    conformant_form=(
                        'declare the object in types/ and reference it with $ref'
                    ),
                ))
            return Opaque(JsonType.OBJECT)

        if json_type is JsonType.STRING and 'enum' in schema:
            # The permitted values survive on the constraints; the payload
            # itself is a string, because there is no enum to name.
            return Primitive(JsonType.STRING)

        if json_type in SCALAR_TYPES:
            return Primitive(json_type)

        raise UnsupportedDefinition(f'{source}: cannot express payload type {declared!r}')

    # -- errors -------------------------------------------------------------

    def _errors(self, entries: Sequence[Mapping[str, Any]]) -> tuple[ErrorDef, ...]:
        """Resolve every ``/errors/<namespace>[#/<Name>]`` reference.

        No template consumes the result today, but resolving is what catches a
        reference to an error that does not exist, or the same error pulled in
        twice by a list reference and a named one.
        """
        root = self.source.child('errors')
        resolved: dict[tuple[str, str], ErrorDef] = {}
        ordered: list[ErrorDef] = []

        for index, entry in enumerate(entries):
            source = root.child(index)
            reference = parse_error_reference(entry['reference'])
            for error in self._namespace_errors(reference, source):
                key = (error.namespace, error.name)
                if key in resolved:
                    raise UnsupportedDefinition(
                        f'{source}: error {error.full_name} is already '
                        f'referenced by this interface'
                    )
                resolved[key] = error
                ordered.append(error)

        return tuple(ordered)

    def _namespace_errors(self, reference, source: SourceRef) -> list[ErrorDef]:
        try:
            document = self.loader.error_document(reference.namespace)
        except DefinitionNotFound as err:
            raise DefinitionNotFound(f'{source}: {err}') from err

        declared = document.data.get('errors') or []
        if reference.is_whole_namespace:
            return [
                ErrorDef(
                    namespace=reference.namespace,
                    name=entry['name'],
                    description=entry['description'],
                    source=document.source.child('errors', index),
                )
                for index, entry in enumerate(declared)
            ]

        for index, entry in enumerate(declared):
            if entry['name'] == reference.name:
                return [ErrorDef(
                    namespace=reference.namespace,
                    name=entry['name'],
                    description=entry['description'],
                    source=document.source.child('errors', index),
                )]

        raise DefinitionNotFound(
            f'{source}: error namespace "{reference.namespace}" declares no '
            f'error named "{reference.name}"'
        )


# --------------------------------------------------------------------------
# Modules
# --------------------------------------------------------------------------

class ModuleBuilder:
    """Builds :class:`ModuleDef` values from a work directory's manifests."""

    def __init__(self, loader: DefinitionLoader, sink: Sink, work_dir: Path) -> None:
        self._loader = loader
        self._sink = sink
        self._work_dir = Path(work_dir)

    def build(self, relative_module_dir: str) -> ModuleDef:
        document = self._loader.module_document(self._work_dir, relative_module_dir)
        data = document.data
        source = document.source

        return ModuleDef(
            # Modules are addressed by a path but named by its last segment.
            name=relative_module_dir.rpartition('/')[2],
            description=data['description'],
            metadata=self._metadata(data['metadata']),
            config=self._config(data.get('config'), source.child('config')),
            provides=tuple(
                self._provides(name, schema, source.child('provides', name))
                for name, schema in (data.get('provides') or {}).items()
            ),
            requires=tuple(
                self._requires(name, schema, source.child('requires', name))
                for name, schema in (data.get('requires') or {}).items()
            ),
            source=source,
            enable_external_mqtt=data.get('enable_external_mqtt', False),
            enable_telemetry=data.get('enable_telemetry', False),
            enable_global_errors=data.get('enable_global_errors', False),
            capabilities=tuple(data.get('capabilities') or ()),
        )

    @staticmethod
    def _metadata(data: Mapping[str, Any]) -> ModuleMetadata:
        known = {'license', 'base_license', 'authors'}
        return ModuleMetadata(
            license=data['license'],
            authors=tuple(data['authors']),
            base_license=data.get('base_license'),
            extra={k: v for k, v in data.items() if k not in known},
        )

    def _config(
        self, entries: Mapping[str, Any] | None, source: SourceRef
    ) -> tuple[ConfigEntry, ...]:
        return tuple(
            ConfigEntry(
                name=name,
                description=schema['description'],
                type=json_type_of(schema['type'], source.child(name)),
                source=source.child(name),
                mutability=self._mutability(schema, source.child(name)),
                constraints=Constraints.from_schema(schema),
            )
            for name, schema in (entries or {}).items()
        )

    def _mutability(self, schema: Mapping[str, Any], source: SourceRef) -> Mutability:
        """Read the declared mutability, defaulting as the meta-schema does.

        A value the manifest spells wrongly is an error rather than silently
        the default.
        """
        declared = schema.get('mutability', Mutability.READ_ONLY.value)
        try:
            return Mutability(declared)
        except ValueError:
            raise UnsupportedDefinition(
                f'{source}: unknown mutability {declared!r}'
            ) from None

    def _provides(
        self, name: str, schema: Mapping[str, Any], source: SourceRef
    ) -> ProvidesDef:
        known = {'description', 'interface', 'config'}
        return ProvidesDef(
            id=name,
            interface=schema['interface'],
            description=schema['description'],
            config=self._config(schema.get('config'), source.child('config')),
            source=source,
            # The manifest allows further primitive properties here, which the
            # framework matches requirements against.  Code generation ignores
            # them; they are carried so the IR does not lose them.
            extra={k: v for k, v in schema.items() if k not in known},
        )

    @staticmethod
    def _requires(
        name: str, schema: Mapping[str, Any], source: SourceRef
    ) -> RequiresDef:
        ignore = schema.get('ignore') or {}
        ignored_vars = ignore.get('vars', ())
        if isinstance(ignored_vars, str):
            ignored_vars = (ignored_vars,)

        return RequiresDef(
            id=name,
            interface=schema['interface'],
            source=source,
            min_connections=schema.get('min_connections', 1),
            max_connections=schema.get('max_connections', 1),
            ignore_vars=tuple(ignored_vars),
            ignore_errors=ignore.get('errors', False),
        )


def build_modules(
    loader: DefinitionLoader,
    sink: Sink,
    work_dir: Path,
    names: Iterable[str],
) -> dict[str, ModuleDef]:
    builder = ModuleBuilder(loader, sink, work_dir)
    return {name: builder.build(name) for name in names}


# --------------------------------------------------------------------------
# Assembling a whole model
# --------------------------------------------------------------------------

def build_type_units(
    loader: DefinitionLoader, sink: Sink, units: Iterable[str] | None = None
) -> dict[str, TypeUnit]:
    """Build the named units, or every unit the source tree offers.

    Callers that only want to *emit* one unit should still load them all: a
    reference into another unit has to be resolvable for a backend to know
    whether it points at a struct or an enum.
    """
    wanted = tuple(units) if units is not None else loader.tree.type_units()
    builder = TypeUnitBuilder(loader, sink)
    return {unit: builder.build(unit) for unit in wanted}


def build_interfaces(
    loader: DefinitionLoader,
    sink: Sink,
    names: Iterable[str],
    *,
    skip_unparsable: bool = False,
) -> dict[str, InterfaceDef]:
    """Build the named interfaces.

    ``skip_unparsable`` is what makes "generate everything" tolerant: asked for
    the whole tree, one unreadable definition should not stop a build, and it is
    reported instead.  Asked for a specific interface, the caller wanted that
    one and gets the error.
    """
    builder = InterfaceBuilder(loader, sink)
    built: dict[str, InterfaceDef] = {}

    for name in names:
        try:
            built[name] = builder.build(name)
        except EvCliError as err:
            if not skip_unparsable:
                raise
            sink.emit(Diagnostic(
                category=Category.INTERFACE_SKIPPED,
                severity=Severity.WARNING,
                source=SourceRef(Path(f'interfaces/{name}.yaml')),
                message=f'skipping interface "{name}": {err}',
            ))
    return built


def build_model(
    loader: DefinitionLoader,
    sink: Sink,
    *,
    units: Iterable[str] | None = None,
    interfaces: Iterable[str] | None = None,
    skip_unparsable_interfaces: bool = False,
) -> Model:
    """Load the definitions once, so that everything else can query them."""
    return Model(
        types=build_type_units(loader, sink, units),
        interfaces=(
            build_interfaces(
                loader, sink, interfaces,
                skip_unparsable=skip_unparsable_interfaces,
            )
            if interfaces is not None else {}
        ),
    )
