# SPDX-License-Identifier: Apache-2.0
# Copyright Pionix GmbH and Contributors to EVerest
"""The IR's own guarantees, and what the fixture tree builds into."""

from __future__ import annotations

import pytest

from ev_cli.ir.common import Constraints
from ev_cli.ir.modules import Mutability
from ev_cli.ir.types import (
    SCALAR_TYPES,
    Array,
    JsonType,
    Opaque,
    Primitive,
    Ref,
    Variant,
    is_enum,
)
from ev_cli.schema.references import TypeRef


class TestTypeExpressions:
    """``Primitive`` and ``Opaque`` divide the JSON types between them."""

    @pytest.mark.parametrize('json_type', [JsonType.OBJECT, JsonType.ARRAY])
    def test_a_primitive_holds_only_a_scalar(self, json_type):
        with pytest.raises(ValueError):
            Primitive(json_type)

    @pytest.mark.parametrize('json_type', [t for t in JsonType if t in SCALAR_TYPES])
    def test_an_opaque_holds_only_an_object_or_an_array(self, json_type):
        with pytest.raises(ValueError):
            Opaque(json_type)


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


class TestFixtureTypes:
    """What the fixture tree should have produced."""

    def test_declared_enums_come_before_promoted_ones(self, fixture_model):
        unit = fixture_model.types['sample']
        # Mood is declared; Kind is an enum written inline on a property.
        assert [e.name for e in unit.enums] == ['Mood', 'Kind']

    def test_inline_enum_is_named_after_its_property(self, fixture_model):
        kind = fixture_model.types['sample'].enum('Kind')
        assert kind.values == ('First', 'Second')

    def test_fields_keep_source_order(self, tmp_path):
        """Ordering for a target language is the backend's business."""
        from conftest import make_loader
        from ev_cli.ir.build import build_type_unit

        (tmp_path / 'types').mkdir()
        (tmp_path / 'types' / 'order.yaml').write_text(
            'description: Field order\n'
            'types:\n'
            '  Pair:\n'
            '    description: Optional before required\n'
            '    type: object\n'
            '    additionalProperties: false\n'
            '    required: [second]\n'
            '    properties:\n'
            '      first: {description: Optional, type: string}\n'
            '      second: {description: Required, type: string}\n'
        )
        pair = build_type_unit(make_loader(tmp_path), 'order').struct('Pair')
        assert [f.name for f in pair.fields] == ['first', 'second']

    def test_constraints_survive(self, fixture_model):
        reading = fixture_model.types['sample'].struct('Reading')
        by_name = {f.name: f for f in reading.fields}
        assert by_name['taken_at'].constraints.format == 'date-time'
        assert by_name['label'].constraints.max_length == 32

        power = fixture_model.types['units'].struct('Power')
        l1 = next(f for f in power.fields if f.name == 'L1')
        assert (l1.constraints.minimum, l1.constraints.maximum) == (0, 100000)

    def test_cross_unit_reference_is_recorded(self, fixture_model):
        assert 'units' in fixture_model.types['sample'].referenced_type_units

    def test_a_reference_resolves_to_its_declaration(self, fixture_model):
        assert is_enum(fixture_model.resolve_type(TypeRef('sample', 'Mood')))
        assert not is_enum(fixture_model.resolve_type(TypeRef('units', 'Power')))


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
