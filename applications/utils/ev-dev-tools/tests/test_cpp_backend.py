# SPDX-License-Identifier: Apache-2.0
# Copyright Pionix GmbH and Contributors to EVerest
"""The C++ backend: the names it generates, the types it maps to, its output.

Everything asserted here appears in a header that everest-core's modules
include, so it is the generated API rather than an internal detail.  The golden
files at the end are the coarse net: they catch any change to the emitted code,
including ones nobody thought to write a case for.
"""

from __future__ import annotations

import os
from pathlib import Path

import pytest

from ev_cli import blocks
from ev_cli.backends.cpp import naming
from ev_cli.backends.cpp.backend import CppBackend
from ev_cli.backends.cpp.mapping import CppMapping, Flavour
from ev_cli.backends.cpp.order import order_fields, order_structs
from ev_cli.backends.cpp.view import InterfaceView, PayloadKind, TypeUnitView
from ev_cli.errors import UnsupportedDefinition
from ev_cli.ir.model import Model
from ev_cli.ir.types import Array, Field, JsonType, LocalEnum, Opaque, Primitive, Ref, StructDef, TypeUnit, Variant
from ev_cli.schema.references import TypeRef
from ev_cli.schema.source import SourceRef
from conftest import GOLDEN_DIR

SOURCE = SourceRef(__file__)


def field(name, required=False, type_=None):
    return Field(
        name=name, description=None, type=type_ or Primitive(JsonType.STRING),
        required=required, source=SOURCE,
    )


def struct(name, depends_on=()):
    return StructDef(
        name=name, description='', fields=(), depends_on=tuple(depends_on),
        source=SOURCE,
    )


class TestFieldOrder:
    """Required first, source order kept within each group.

    Generated types are positionally aggregate initialised in everest-core, so
    this ordering is behaviour rather than presentation.
    """

    def test_required_come_first(self):
        ordered = order_fields([
            field('a'), field('b', required=True), field('c'), field('d', required=True),
        ])
        assert [f.name for f in ordered] == ['b', 'd', 'a', 'c']

    def test_partition_is_stable(self):
        ordered = order_fields([field(n) for n in 'zyxw'])
        assert [f.name for f in ordered] == list('zyxw')

    def test_all_required(self):
        ordered = order_fields([field(n, required=True) for n in 'abc'])
        assert [f.name for f in ordered] == list('abc')

    def test_empty(self):
        assert order_fields([]) == ()

    def test_the_type_view_applies_it(self):
        pair = StructDef(
            name='Pair', description='', fields=(field('a'), field('b', required=True)),
            depends_on=(), source=SOURCE,
        )
        assert [f.name for f in TypeUnitView.fields(pair)] == ['b', 'a']


class TestStructOrder:
    """A struct is emitted after anything it embeds, else in source order."""

    def test_no_dependencies_keeps_source_order(self):
        structs = [struct(n) for n in ('First', 'Second', 'Third')]
        assert [s.name for s in order_structs(structs)] == ['First', 'Second', 'Third']

    def test_embedded_struct_comes_first(self):
        structs = [struct('Outer', depends_on=['Inner']), struct('Inner')]
        assert [s.name for s in order_structs(structs)] == ['Inner', 'Outer']

    def test_transitive(self):
        structs = [
            struct('A', depends_on=['B']),
            struct('B', depends_on=['C']),
            struct('C'),
        ]
        assert [s.name for s in order_structs(structs)] == ['C', 'B', 'A']

    def test_a_cycle_does_not_hang(self):
        """C++ cannot express it, but the generator must still terminate."""
        structs = [struct('A', depends_on=['B']), struct('B', depends_on=['A'])]
        assert len(order_structs(structs)) == 2

    def test_unknown_dependency_is_ignored(self):
        """A reference to another unit is not a local ordering constraint."""
        structs = [struct('A', depends_on=['Elsewhere'])]
        assert [s.name for s in order_structs(structs)] == ['A']

    def test_the_type_view_applies_it(self):
        unit = TypeUnit(
            unit='u', description='', enums=(),
            structs=(struct('Outer', depends_on=['Inner']), struct('Inner')),
            referenced_type_units=frozenset(), source=SOURCE,
        )
        assert [s.name for s in TypeUnitView(unit=unit, model=Model()).structs] == ['Inner', 'Outer']


class TestSnakeCase:
    """Enum helper names are built from this, so its output is the API."""

    @pytest.mark.parametrize('word, expected', [
        ('ChargingPhase', 'charging_phase'),
        ('EvseState', 'evse_state'),
        ('Mode', 'mode'),
        ('power_supply_DC', 'power_supply_dc'),
        ('OCSPRequestData', 'ocsprequest_data'),
        ('ISO15118', 'iso15118'),
        ('Power', 'power'),
        ('a', 'a'),
        ('', ''),
        ('with-dash', 'with_dash'),
        ('trailing_', 'trailing_'),
    ])
    def test_conversion(self, word, expected):
        assert naming.snake_case(word) == expected

    def test_rejects_a_name_it_cannot_start_from(self):
        with pytest.raises(UnsupportedDefinition):
            naming.snake_case('-leading')


class TestGuardsAndNames:

    @pytest.mark.parametrize('unit, expected', [
        ('energy', 'TYPES_ENERGY_TYPES_HPP'),
        ('power_supply_DC', 'TYPES_POWER_SUPPLY_DC_TYPES_HPP'),
        ('units_signed', 'TYPES_UNITS_SIGNED_TYPES_HPP'),
    ])
    def test_type_header_guard(self, unit, expected):
        assert naming.type_header_guard(unit) == expected

    @pytest.mark.parametrize('header, expected', [
        (naming.InterfaceHeaderFile.IMPLEMENTATION_HPP, 'ISOLATION_MONITOR_IMPLEMENTATION_HPP'),
        (naming.InterfaceHeaderFile.INTERFACE_HPP, 'ISOLATION_MONITOR_INTERFACE_HPP'),
        (naming.InterfaceHeaderFile.TYPES_HPP, 'ISOLATION_MONITOR_TYPES_HPP'),
    ])
    def test_interface_header_guard(self, header, expected):
        assert naming.interface_header_guard('isolation_monitor', header) == expected

    def test_implementation_guard(self):
        assert naming.implementation_header_guard('main', 'evse_manager') == \
            'MAIN_EVSE_MANAGER_IMPL_HPP'

    def test_module_guard(self):
        assert naming.module_header_guard('EvseManager') == 'EVSE_MANAGER_HPP'

    def test_namespaces(self):
        assert naming.unit_namespaces('energy') == ['types', 'energy']

    def test_qualified_type(self):
        assert naming.qualified_type('energy', 'Power') == 'types::energy::Power'

    @pytest.mark.parametrize('spelling, to_string, from_string', [
        ('EvseState', 'evse_state_to_string', 'string_to_evse_state'),
        ('types::energy::EvseState',
         'types::energy::evse_state_to_string',
         'types::energy::string_to_evse_state'),
    ])
    def test_enum_helper_names(self, spelling, to_string, from_string):
        assert naming.enum_to_string(spelling) == to_string
        assert naming.enum_to_string_view(spelling) == f'{to_string}_view'
        assert naming.string_to_enum(spelling) == from_string

    def test_rejects_a_cpp_keyword_as_an_identifier(self):
        """The previous implementation checked Python's keywords here."""
        with pytest.raises(UnsupportedDefinition):
            naming.check_identifier('class', 'somewhere')
        naming.check_identifier('lambda', 'somewhere')  # legal in C++


class TestTypeMapping:
    """The generated ABI.  A change here relinks every module."""

    @pytest.fixture
    def field_mapping(self, fixture_model):
        return CppMapping(fixture_model, Flavour.FIELD)

    @pytest.fixture
    def payload_mapping(self, fixture_model):
        return CppMapping(fixture_model, Flavour.PAYLOAD)

    @pytest.mark.parametrize('json_type, expected', [
        (JsonType.BOOLEAN, 'bool'),
        (JsonType.INTEGER, 'int32_t'),
        (JsonType.NUMBER, 'float'),
        (JsonType.STRING, 'std::string'),
    ])
    def test_struct_members(self, field_mapping, json_type, expected):
        assert field_mapping.spell(Primitive(json_type)).text == expected

    @pytest.mark.parametrize('json_type, expected', [
        (JsonType.BOOLEAN, 'bool'),
        (JsonType.INTEGER, 'int'),
        (JsonType.NUMBER, 'double'),
        (JsonType.STRING, 'std::string'),
        (JsonType.NULL, 'std::nullptr_t'),
    ])
    def test_interface_payloads(self, payload_mapping, json_type, expected):
        assert payload_mapping.spell(Primitive(json_type)).text == expected

    def test_the_two_mappings_disagree_and_that_is_deliberate(
            self, field_mapping, payload_mapping):
        """Reconciling this changes struct layout and precision.

        A struct member declared ``number`` is a ``float`` while a command
        argument declared ``number`` is a ``double``.  Nothing justifies it,
        but changing it is ABI-visible and belongs in its own change; this test
        exists so the difference cannot be lost by accident.
        """
        for json_type in (JsonType.NUMBER, JsonType.INTEGER):
            assert (field_mapping.spell(Primitive(json_type)).text
                    != payload_mapping.spell(Primitive(json_type)).text)

    def test_format_is_not_honoured_yet(self, fixture_model):
        """Properties saying ``format: date-time`` are all plain std::string."""
        from ev_cli.backends.cpp.mapping import FORMAT_TYPES
        assert FORMAT_TYPES == {}

        reading = fixture_model.types['sample'].struct('Reading')
        taken_at = next(f for f in reading.fields if f.name == 'taken_at')
        assert taken_at.constraints.format == 'date-time'
        assert CppMapping(fixture_model, Flavour.FIELD).spell(
            taken_at.type).text == 'std::string'

    def test_references(self, field_mapping):
        enum = field_mapping.spell(Ref(TypeRef('sample', 'Mood')))
        assert enum.text == 'types::sample::Mood'
        assert enum.is_enum

        struct = field_mapping.spell(Ref(TypeRef('units', 'Power')))
        assert struct.text == 'types::units::Power'
        assert not struct.is_enum

    def test_local_enum(self, field_mapping):
        spelling = field_mapping.spell(LocalEnum('Kind'))
        assert (spelling.text, spelling.is_enum) == ('Kind', True)

    def test_struct_member_arrays_are_always_typed(self, field_mapping):
        spelling = field_mapping.spell(Array(Primitive(JsonType.STRING)))
        assert spelling.text == 'std::vector<std::string>'
        assert spelling.is_vector

    def test_payload_arrays_are_typed_only_when_the_items_are_declared(
            self, payload_mapping):
        """``handle_get_configuration_key(Array&)`` is the visible consequence."""
        assert payload_mapping.spell(Array(Primitive(JsonType.STRING))).text == 'Array'
        assert payload_mapping.spell(
            Array(Ref(TypeRef('sample', 'Mood')))
        ).text == 'std::vector<types::sample::Mood>'

    def test_array_of_enums_is_flagged(self, payload_mapping):
        spelling = payload_mapping.spell(Array(Ref(TypeRef('sample', 'Mood'))))
        assert spelling.element_is_enum
        assert not payload_mapping.spell(
            Array(Ref(TypeRef('units', 'Power')))).element_is_enum

    def test_opaque_payloads(self, payload_mapping):
        assert payload_mapping.spell(Opaque(JsonType.OBJECT)).text == 'Object'
        assert payload_mapping.spell(Opaque(JsonType.ARRAY)).text == 'Array'

    def test_variant_alternatives_are_sorted_with_null_first(self, payload_mapping):
        """So that the variant default-constructs to the empty alternative."""
        variant = Variant((
            JsonType.NULL, JsonType.STRING, JsonType.NUMBER, JsonType.INTEGER,
            JsonType.BOOLEAN, JsonType.ARRAY, JsonType.OBJECT,
        ))
        assert payload_mapping.spell(variant).text == (
            'std::variant<std::nullptr_t, Array, Object, bool, double, int, '
            'std::string>'
        )

    def test_variant_ordering_is_independent_of_the_declared_order(
            self, payload_mapping):
        a = Variant((JsonType.STRING, JsonType.INTEGER))
        b = Variant((JsonType.INTEGER, JsonType.STRING))
        assert payload_mapping.spell(a).text == payload_mapping.spell(b).text


class TestPayloadKinds:

    @pytest.fixture
    def view(self, fixture_model):
        return InterfaceView(
            interface=fixture_model.interfaces['sample_source'],
            model=fixture_model,
        )

    @pytest.mark.parametrize('var_name, kind', [
        ('latest', PayloadKind.OBJECT),
        ('nothing_happened', PayloadKind.NULL),
        ('moods_seen', PayloadKind.ARRAY),
        ('count', PayloadKind.PLAIN),
    ])
    def test_variables(self, view, var_name, kind):
        assert view.kind(view.interface.var(var_name).type) is kind

    def test_an_enum_argument(self, view):
        configure = view.interface.cmd('configure')
        assert view.kind(configure.arguments[0].type) is PayloadKind.ENUM

    def test_json_types_come_from_the_target_not_the_sibling(self, view):
        """``mood`` writes ``type: object`` beside a reference to a string enum."""
        configure = view.interface.cmd('configure')
        assert view.json_types(configure.arguments[0].type) == '{"string"}'

    def test_a_variant_lists_every_option(self, view):
        store = view.interface.cmd('store')
        assert view.json_types(store.arguments[0].type) == (
            '{"null", "string", "number", "integer", "boolean", "array", "object"}'
        )

    def test_result_type_is_void_only_without_a_result(self, view):
        assert view.result_type(view.interface.cmd('reset')) == 'void'
        assert view.result_type(view.interface.cmd('configure')) == \
            'types::sample::Reading'

    def test_a_variant_argument_is_passed_by_value_in_call(self, view):
        """Reproduced from the previous implementation; see call_declaration."""
        store = view.interface.cmd('store')
        assert view.call_declaration(store.arguments[0]).startswith('std::variant<')
        configure = view.interface.cmd('configure')
        assert view.call_declaration(configure.arguments[0]).startswith('const ')


# --------------------------------------------------------------------------
# Golden output
# --------------------------------------------------------------------------

def generate_everything(model, out_dir: Path) -> dict[str, str]:
    """Every file the backend can produce for the fixture tree."""
    backend = CppBackend()
    produced: dict[str, str] = {}

    for unit in sorted(model.types):
        for file in backend.emit_type_unit(model, unit, out_dir / 'types'):
            produced[f'types/{unit}.hpp'] = file.content

    for interface in sorted(model.interfaces):
        for file in backend.emit_interface(model, interface, out_dir / 'interfaces'):
            produced[f'interfaces/{interface}/{file.path.name}'] = file.content

    for module in sorted(model.modules):
        for file in backend.emit_module_loader(model, module, out_dir / 'modules'):
            produced[f'modules/{module}/{file.path.name}'] = file.content
        for file in backend.emit_module_files(
            model, module, out_dir / 'module-files',
            license_header='// SPDX-License-Identifier: Apache-2.0',
            read_blocks=lambda block_set, path: blocks.placeholders(block_set),
        ):
            relative = file.path.relative_to(out_dir / 'module-files').as_posix()
            produced[f'module-files/{module}/{relative}'] = file.content

    return produced


class TestGoldenOutput:
    """The whole emitted surface, pinned.

    Set ``EV_CLI_UPDATE_GOLDEN=1`` to rewrite these after a change that is
    meant to alter the output -- and then read the diff, because it is the
    generated API of every module.
    """

    def test_matches(self, fixture_model, tmp_path):
        produced = generate_everything(fixture_model, tmp_path)

        if os.environ.get('EV_CLI_UPDATE_GOLDEN'):
            for name, content in produced.items():
                path = GOLDEN_DIR / name
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text(content)
            pytest.skip(f'rewrote {len(produced)} golden files')

        assert GOLDEN_DIR.is_dir(), (
            f'no golden files at {GOLDEN_DIR}; run with EV_CLI_UPDATE_GOLDEN=1'
        )
        expected = {
            p.relative_to(GOLDEN_DIR).as_posix(): p.read_text()
            for p in sorted(GOLDEN_DIR.rglob('*')) if p.is_file()
        }

        assert sorted(produced) == sorted(expected)
        for name in sorted(expected):
            assert produced[name] == expected[name], f'{name} changed'

    def test_covers_every_kind_of_file(self, fixture_model, tmp_path):
        produced = generate_everything(fixture_model, tmp_path)
        names = set(produced)
        assert any(n.startswith('types/') for n in names)
        assert 'interfaces/sample_source/Implementation.hpp' in names
        assert 'interfaces/sample_source/Interface.hpp' in names
        assert 'interfaces/sample_source/Types.hpp' in names
        assert 'modules/Sample/ld-ev.hpp' in names
        assert 'modules/Sample/ld-ev.cpp' in names
        assert 'module-files/Sample/CMakeLists.txt' in names
        assert 'module-files/Sample/Sample.hpp' in names
        assert 'module-files/Sample/main/sample_sourceImpl.hpp' in names
        assert 'module-files/Sample/docs/index.rst' in names
