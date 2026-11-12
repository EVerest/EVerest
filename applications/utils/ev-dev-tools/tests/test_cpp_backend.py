# SPDX-License-Identifier: Apache-2.0
# Copyright Pionix GmbH and Contributors to EVerest
"""The C++ backend: the names it generates, the types it maps to, its output.

Everything asserted here appears in a header that everest-core's modules
include, so it is the generated API rather than an internal detail.  The golden
files at the end are the coarse net: they catch any change to the emitted code,
including ones nobody thought to write a case for.
"""

from __future__ import annotations

import argparse
import os
from pathlib import Path

import pytest

from ev_cli import license_headers
from ev_cli.backends.cpp import naming
from ev_cli.backends.base import OutputKind
from ev_cli.backends.cpp.backend import TEMPLATE_DIR, CppBackend, Template
from ev_cli.backends.cpp.cpp_type import Flavour, to_cpp_type
from ev_cli.backends.cpp.views import InterfaceView, PayloadKind, TypeUnitView
from ev_cli.errors import UnsupportedDefinition
from ev_cli.ir.types import Array, Field, JsonType, LocalEnum, Opaque, Primitive, Ref, StructDef, Variant
from ev_cli.schema.references import TypeRef
from ev_cli.schema.source import SourceRef
from conftest import GOLDEN_DIR

SOURCE = SourceRef(__file__)


def field(name, required=False, type_=None):
    return Field(
        name=name, description=None, type=type_ or Primitive(JsonType.STRING),
        required=required, source=SOURCE,
    )


class TestFieldOrder:
    """Required first, source order kept within each group.

    Generated types are positionally aggregate initialised in everest-core, so
    this ordering is behaviour rather than presentation.
    """

    @staticmethod
    def ordered(*fields):
        holder = StructDef(name='Holder', description='', fields=fields, source=SOURCE)
        return [f.name for f in TypeUnitView.fields(holder)]

    def test_required_come_first_and_each_group_keeps_source_order(self):
        assert self.ordered(
            field('a'), field('b', required=True), field('c'), field('d', required=True),
        ) == ['b', 'd', 'a', 'c']
        assert self.ordered(*(field(n) for n in 'zyxw')) == list('zyxw')
        assert self.ordered(*(field(n, required=True) for n in 'abc')) == list('abc')
        assert self.ordered() == []


class TestTemplates:
    """``Template`` is the one list of templates; get-templates and rendering use it."""

    def test_every_template_on_disk_is_declared_and_every_declared_one_exists(self):
        on_disk = {path.relative_to(TEMPLATE_DIR).as_posix() for path in TEMPLATE_DIR.rglob('*.j2')}
        assert on_disk == {template.value for template in Template}

    def test_every_output_kind_has_templates(self):
        assert {template.kind for template in Template} == set(OutputKind)


class TestSnakeCase:
    """Enum helper names are built from this, so its output is the API."""

    def test_conversion(self):
        expected = {
            'ChargingPhase': 'charging_phase',
            'EvseState': 'evse_state',
            'Mode': 'mode',
            'power_supply_DC': 'power_supply_dc',
            'OCSPRequestData': 'ocsprequest_data',
            'ISO15118': 'iso15118',
            'Power': 'power',
            'a': 'a',
            '': '',
            'with-dash': 'with_dash',
            'trailing_': 'trailing_',
        }
        assert {word: naming.snake_case(word) for word in expected} == expected

    def test_rejects_a_name_it_cannot_start_from(self):
        with pytest.raises(UnsupportedDefinition):
            naming.snake_case('-leading')


class TestGuardsAndNames:

    def test_type_header_guard(self):
        expected = {
            'energy': 'TYPES_ENERGY_TYPES_HPP',
            'power_supply_DC': 'TYPES_POWER_SUPPLY_DC_TYPES_HPP',
            'units_signed': 'TYPES_UNITS_SIGNED_TYPES_HPP',
        }
        assert {unit: naming.type_header_guard(unit) for unit in expected} == expected

    def test_interface_header_guard(self):
        expected = {
            'Implementation.hpp': 'ISOLATION_MONITOR_IMPLEMENTATION_HPP',
            'Interface.hpp': 'ISOLATION_MONITOR_INTERFACE_HPP',
            'Types.hpp': 'ISOLATION_MONITOR_TYPES_HPP',
        }
        assert {
            filename: naming.interface_header_guard('isolation_monitor', filename) for filename in expected
        } == expected

    def test_implementation_guard(self):
        assert naming.implementation_header_guard('main', 'evse_manager') == \
            'MAIN_EVSE_MANAGER_IMPL_HPP'

    def test_module_guard(self):
        assert naming.module_header_guard('EvseManager') == 'EVSE_MANAGER_HPP'

    def test_namespaces(self):
        assert naming.unit_namespaces('energy') == ['types', 'energy']

    def test_qualified_type(self):
        assert naming.qualified_type('energy', 'Power') == 'types::energy::Power'

    def test_rejects_a_cpp_keyword_as_an_identifier(self):
        """C++ keywords, not Python's, are what an identifier has to avoid."""
        with pytest.raises(UnsupportedDefinition):
            naming.check_identifier('class', 'somewhere')
        naming.check_identifier('lambda', 'somewhere')  # legal in C++


class TestCppKeywords:
    """C++ reserves a word only where generated code uses a name on its own."""

    @staticmethod
    def emit(tmp_path, cmds_yaml):
        from conftest import make_loader
        from ev_cli.ir.build import build_interface
        from ev_cli.ir.model import Model

        (tmp_path / 'interfaces').mkdir()
        (tmp_path / 'interfaces' / 'odd.yaml').write_text('description: Odd names\ncmds:\n' + cmds_yaml)
        model = Model(interfaces={'odd': build_interface(make_loader(tmp_path), 'odd')})
        backend = CppBackend(argparse.Namespace(disable_clang_format=True))
        return backend.emit_interface(model, 'odd', tmp_path / 'out')

    def test_an_argument_named_after_a_keyword_is_rejected(self, tmp_path):
        """Arguments are the handler's parameter names."""
        with pytest.raises(UnsupportedDefinition, match='"delete" is a C\\+\\+ keyword'):
            self.emit(tmp_path, (
                '  remove:\n'
                '    description: Takes a keyword argument\n'
                '    arguments:\n'
                '      delete: {description: A keyword, type: boolean}\n'
            ))

    def test_a_command_named_after_a_keyword_is_accepted(self, tmp_path):
        """A command name is always part of a longer one, such as handle_delete."""
        assert self.emit(tmp_path, '  delete: {description: A keyword command}\n')


class TestTypeMapping:
    """The generated ABI.  A change here relinks every module."""

    def test_struct_members(self):
        expected = {
            JsonType.BOOLEAN: 'bool',
            JsonType.INTEGER: 'int32_t',
            JsonType.NUMBER: 'float',
            JsonType.STRING: 'std::string',
        }
        assert {t: to_cpp_type(Primitive(t), Flavour.FIELD) for t in expected} == expected

    def test_interface_payloads(self):
        expected = {
            JsonType.BOOLEAN: 'bool',
            JsonType.INTEGER: 'int',
            JsonType.NUMBER: 'double',
            JsonType.STRING: 'std::string',
            JsonType.NULL: 'std::nullptr_t',
        }
        assert {t: to_cpp_type(Primitive(t), Flavour.PAYLOAD) for t in expected} == expected

    def test_the_two_mappings_disagree_and_that_is_deliberate(self):
        """Reconciling this changes struct layout and precision.

        A struct member declared ``number`` is a ``float`` while a command
        argument declared ``number`` is a ``double``.  Nothing justifies it,
        but changing it is ABI-visible and belongs in its own change; this test
        exists so the difference cannot be lost by accident.
        """
        for json_type in (JsonType.NUMBER, JsonType.INTEGER):
            assert (to_cpp_type(Primitive(json_type), Flavour.FIELD)
                    != to_cpp_type(Primitive(json_type), Flavour.PAYLOAD))

    def test_format_does_not_change_the_type(self, fixture_model):
        """A property saying ``format: date-time`` is a plain std::string."""
        reading = fixture_model.types['sample'].struct('Reading')
        taken_at = next(f for f in reading.fields if f.name == 'taken_at')
        assert taken_at.constraints.format == 'date-time'
        assert to_cpp_type(taken_at.type, Flavour.FIELD) == 'std::string'

    def test_references(self):
        assert to_cpp_type(Ref(TypeRef('sample', 'Mood')), Flavour.FIELD) == 'types::sample::Mood'
        assert to_cpp_type(Ref(TypeRef('units', 'Power')), Flavour.FIELD) == 'types::units::Power'

    def test_local_enum(self):
        assert to_cpp_type(LocalEnum('Kind'), Flavour.FIELD) == 'Kind'

    def test_struct_member_arrays_are_always_typed(self):
        assert to_cpp_type(Array(Primitive(JsonType.STRING)), Flavour.FIELD) == 'std::vector<std::string>'

    def test_payload_arrays_are_typed_only_when_the_items_are_declared(self):
        """``handle_get_configuration_key(Array&)`` is the visible consequence."""
        assert to_cpp_type(Array(Primitive(JsonType.STRING)), Flavour.PAYLOAD) == 'Array'
        assert to_cpp_type(
            Array(Ref(TypeRef('sample', 'Mood'))), Flavour.PAYLOAD) == 'std::vector<types::sample::Mood>'

    def test_opaque_payloads(self):
        assert to_cpp_type(Opaque(JsonType.OBJECT), Flavour.PAYLOAD) == 'Object'
        assert to_cpp_type(Opaque(JsonType.ARRAY), Flavour.PAYLOAD) == 'Array'

    def test_variant_alternatives_are_sorted_with_null_first(self):
        """So that the variant default-constructs to the empty alternative."""
        variant = Variant((
            JsonType.NULL, JsonType.STRING, JsonType.NUMBER, JsonType.INTEGER,
            JsonType.BOOLEAN, JsonType.ARRAY, JsonType.OBJECT,
        ))
        assert to_cpp_type(variant, Flavour.PAYLOAD) == (
            'std::variant<std::nullptr_t, Array, Object, bool, double, int, '
            'std::string>'
        )

    def test_variant_ordering_is_independent_of_the_declared_order(self):
        a = Variant((JsonType.STRING, JsonType.INTEGER))
        b = Variant((JsonType.INTEGER, JsonType.STRING))
        assert to_cpp_type(a, Flavour.PAYLOAD) == to_cpp_type(b, Flavour.PAYLOAD)


class TestPayloadKinds:

    @pytest.fixture
    def view(self, fixture_model):
        return InterfaceView(
            interface=fixture_model.interfaces['sample_source'],
            model=fixture_model,
        )

    def test_variables(self, view):
        expected = {
            'latest': PayloadKind.REF,
            'nothing_happened': PayloadKind.NULL,
            'moods_seen': PayloadKind.ARRAY,
            'count': PayloadKind.PLAIN,
        }
        assert {name: view.kind(view.interface.var(name).type) for name in expected} == expected

    def test_arrays(self, view):
        expected = {
            Array(Ref(TypeRef('sample', 'Mood'))): PayloadKind.ARRAY,
            Array(Ref(TypeRef('units', 'Power'))): PayloadKind.ARRAY,
            Array(Primitive(JsonType.STRING)): PayloadKind.PLAIN,
        }
        assert {payload: view.kind(payload) for payload in expected} == expected

    def test_an_enum_argument(self, view):
        configure = view.interface.cmd('configure')
        assert view.kind(configure.arguments[0].type) is PayloadKind.REF

    def test_json_types_come_from_the_target_not_the_sibling(self, view):
        """``mood`` writes ``type: object`` beside a reference to a string enum."""
        configure = view.interface.cmd('configure')
        assert view.json_types_initializer(configure.arguments[0].type) == '{"string"}'

    def test_a_variant_lists_every_option(self, view):
        store = view.interface.cmd('store')
        assert view.json_types_initializer(store.arguments[0].type) == (
            '{"null", "string", "number", "integer", "boolean", "array", "object"}'
        )

    def test_result_type_is_void_only_without_a_result(self, view):
        assert view.result_type(view.interface.cmd('reset')) == 'void'
        assert view.result_type(view.interface.cmd('configure')) == \
            'types::sample::Reading'

    def test_a_variant_argument_is_passed_by_value_in_call(self, view):
        """Part of the generated signature; see call_declaration."""
        store = view.interface.cmd('store')
        assert view.call_declaration(store.arguments[0]).startswith('std::variant<')
        configure = view.interface.cmd('configure')
        assert view.call_declaration(configure.arguments[0]).startswith('const ')


# --------------------------------------------------------------------------
# Golden output
# --------------------------------------------------------------------------

def generate_everything(model, out_dir: Path) -> dict[str, str]:
    """Every file the backend can produce for the fixture tree, unformatted."""
    backend = CppBackend(argparse.Namespace(disable_clang_format=True))
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
            license_header=license_headers.find_header(
                [license_headers.BUNDLED_DIR], model.modules[module].metadata.license
            ),
            preserve_regions=False,
        ):
            relative = file.path.relative_to(out_dir / 'module-files').as_posix()
            produced[f'module-files/{module}/{relative}'] = file.content

    return produced


class TestGoldenOutput:
    """The whole emitted surface, pinned.

    Set ``EV_CLI_UPDATE_GOLDEN=1`` to rewrite these after a change that is
    meant to alter the output, and then read the diff, because it is the
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
