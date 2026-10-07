# SPDX-License-Identifier: Apache-2.0
# Copyright Pionix GmbH and Contributors to EVerest
"""The IR's own guarantees: order, completeness and round-tripping."""

from __future__ import annotations

import pytest

from ev_cli.ir import json_io
from ev_cli.ir.common import Constraints
from ev_cli.ir.modules import Mutability
from ev_cli.ir.order import order_fields, order_structs
from ev_cli.ir.types import (
    Array,
    EnumDef,
    Field,
    JsonType,
    LocalEnum,
    Primitive,
    Ref,
    SourceRef,
    StructDef,
    TypeRef,
    Variant,
)

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


class TestConstraints:

    def test_reads_every_keyword_it_claims_to(self):
        constraints = Constraints.from_schema({
            'minimum': 0, 'maximum': 10, 'minLength': 1, 'maxLength': 2,
            'minItems': 3, 'maxItems': 4, 'pattern': '^a$', 'format': 'date-time',
            'additionalProperties': False, 'enum': ['a', 'b'], 'default': 'a',
        })
        assert constraints.minimum == 0
        assert constraints.maximum == 10
        assert constraints.min_length == 1
        assert constraints.max_length == 2
        assert constraints.min_items == 3
        assert constraints.max_items == 4
        assert constraints.pattern == '^a$'
        assert constraints.format == 'date-time'
        assert constraints.additional_properties is False
        assert constraints.enum_values == ('a', 'b')
        assert constraints.default == 'a'
        assert constraints.has_default

    def test_empty_schema(self):
        assert Constraints.from_schema({}).is_empty

    def test_a_null_default_is_not_the_same_as_no_default(self):
        assert Constraints.from_schema({'default': None}).has_default
        assert not Constraints.from_schema({}).has_default

    def test_is_hashable(self):
        """The dataclasses carrying it are frozen, so a list would not do."""
        hash(Constraints.from_schema({'enum': ['a', 'b']}))


class TestRoundTrip:
    """Every node has to be fully described by its fields.

    A round trip through JSON is a compact way to say that: anything living in
    a cache or a closure instead of a field would not survive it.
    """

    @pytest.mark.parametrize('value', [
        Primitive(JsonType.NUMBER),
        Array(Primitive(JsonType.STRING)),
        Ref(TypeRef('energy', 'Power')),
        LocalEnum('Mood'),
        Variant((JsonType.NULL, JsonType.STRING)),
        Constraints.from_schema({'minimum': 1, 'enum': ['a']}),
        EnumDef(name='Mood', description='d', values=('A', 'B'), source=SOURCE),
    ])
    def test_type_expressions(self, value):
        assert json_io.round_trip(value) == value

    def test_a_whole_type_unit(self, fixture_model):
        for unit in fixture_model.types.values():
            assert json_io.round_trip(unit) == unit

    def test_a_whole_interface(self, fixture_model):
        for interface in fixture_model.interfaces.values():
            assert json_io.round_trip(interface) == interface

    def test_a_whole_module(self, fixture_model):
        for module in fixture_model.modules.values():
            assert json_io.round_trip(module) == module

    def test_json_is_stable(self, fixture_model):
        """Serialising twice gives the same bytes, so it can be diffed."""
        once = json_io.dumps(fixture_model.types['sample'])
        assert once == json_io.dumps(json_io.loads(once))


class TestFixtureTypes:
    """What the fixture tree should have produced."""

    def test_declared_enums_come_before_promoted_ones(self, fixture_model):
        unit = fixture_model.types['sample']
        # Mood is declared; Kind is an enum written inline on a property.
        assert [e.name for e in unit.enums] == ['Mood', 'Kind']

    def test_inline_enum_is_named_after_its_property(self, fixture_model):
        kind = fixture_model.types['sample'].enum('Kind')
        assert kind.values == ('First', 'Second')

    def test_required_fields_first(self, fixture_model):
        reading = fixture_model.types['sample'].struct('Reading')
        names = [f.name for f in reading.fields]
        assert names[:2] == ['taken_at', 'mood']
        assert set(names[2:]) == {'power', 'history', 'label', 'kind'}

    def test_constraints_survive(self, fixture_model):
        reading = fixture_model.types['sample'].struct('Reading')
        by_name = {f.name: f for f in reading.fields}
        assert by_name['taken_at'].constraints.format == 'date-time'
        assert by_name['label'].constraints.max_length == 32

        power = fixture_model.types['units'].struct('Power')
        l1 = next(f for f in power.fields if f.name == 'L1')
        assert (l1.constraints.minimum, l1.constraints.maximum) == (0, 100000)

    def test_cross_unit_reference_is_recorded(self, fixture_model):
        assert 'units' in fixture_model.types['sample'].referenced_units

    def test_a_reference_resolves_to_its_declaration(self, fixture_model):
        assert fixture_model.is_enum(TypeRef('sample', 'Mood'))
        assert not fixture_model.is_enum(TypeRef('units', 'Power'))


class TestFixtureInterface:

    def test_payload_shapes(self, fixture_model):
        interface = fixture_model.interfaces['sample_source']
        by_name = {v.name: v for v in interface.vars}
        assert by_name['latest'].type == Ref(TypeRef('sample', 'Reading'))
        assert by_name['nothing_happened'].type == Primitive(JsonType.NULL)
        assert by_name['moods_seen'].type == Array(Ref(TypeRef('sample', 'Mood')))
        assert by_name['count'].type == Primitive(JsonType.INTEGER)

    def test_argument_order_is_the_declared_order(self, fixture_model):
        """It becomes the generated handler's parameter order."""
        configure = fixture_model.interfaces['sample_source'].cmd('configure')
        assert [a.name for a in configure.arguments] == [
            'mood', 'interval', 'enabled', 'allowed']

    def test_a_command_without_a_result(self, fixture_model):
        assert fixture_model.interfaces['sample_source'].cmd('reset').result is None

    def test_a_variant_payload(self, fixture_model):
        store = fixture_model.interfaces['sample_source'].cmd('store')
        value = store.arguments[0].type
        assert isinstance(value, Variant)
        assert value.nullable
        assert len(value.non_null_options) == 6

    def test_errors_are_resolved(self, fixture_model):
        errors = fixture_model.interfaces['sample_source'].errors
        assert [e.full_name for e in errors] == [
            'sample/SomethingBroke', 'sample/SomethingElseBroke']


class TestFixtureModule:

    def test_mutability_is_a_field_not_an_exception(self, fixture_model):
        module = fixture_model.modules['Sample']
        by_name = {c.name: c for c in module.config}
        assert by_name['read_only_setting'].mutability is Mutability.READ_ONLY
        assert by_name['changeable_number'].mutability is Mutability.READ_WRITE

    def test_defaults_survive(self, fixture_model):
        by_name = {c.name: c for c in fixture_model.modules['Sample'].config}
        assert by_name['read_only_setting'].default == 'hello'
        assert by_name['changeable_number'].default == 3

    def test_is_vector_is_derived_from_the_connection_bounds(self, fixture_model):
        by_id = {r.id: r for r in fixture_model.modules['Sample'].requires}
        assert not by_id['one_source'].is_vector
        assert by_id['many_sources'].is_vector

    def test_flags(self, fixture_model):
        module = fixture_model.modules['Sample']
        assert module.enable_external_mqtt
        assert module.enable_telemetry
        assert module.enable_global_errors
