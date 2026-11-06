# SPDX-License-Identifier: Apache-2.0
# Copyright Pionix GmbH and Contributors to EVerest
"""Both reference dialects must parse to the same thing.

That is what lets the definitions migrate one file at a time: a consumer
cannot tell which spelling a file used, so no consumer has to change when one
does.
"""

from __future__ import annotations

import pytest

from ev_cli.errors import MalformedReference
from ev_cli.ir.types import TypeRef
from ev_cli.schema.references import (
    Dialect,
    conformant_form,
    parse_error_reference,
    parse_type_reference,
)


class TestLegacyDialect:
    """``/energy#/Power`` -- what every definition in everest-core uses today."""

    def test_same_unit(self):
        parsed = parse_type_reference('/energy#/Power', from_unit='energy')
        assert parsed.target == TypeRef('energy', 'Power')
        assert parsed.dialect is Dialect.LEGACY
        assert parsed.is_legacy

    def test_other_unit(self):
        parsed = parse_type_reference('/units#/Power', from_unit='energy')
        assert parsed.target == TypeRef('units', 'Power')

    def test_nested_unit(self):
        parsed = parse_type_reference('/nested/deep#/Thing', from_unit='energy')
        assert parsed.target == TypeRef('nested/deep', 'Thing')

    def test_from_a_non_type_document(self):
        """An interface refers to types the same way a type file does."""
        parsed = parse_type_reference('/units#/Power', from_unit=None)
        assert parsed.target == TypeRef('units', 'Power')

    @pytest.mark.parametrize('raw', [
        'energy#/Power',        # no leading slash and no .yaml
        '/energy',              # no fragment
        '/energy#Power',        # fragment is not a pointer
        '/energy#/a/b',         # fragment names more than a type
        '/#/Power',             # no unit
        '',
    ])
    def test_rejects_nonsense(self, raw):
        with pytest.raises(MalformedReference):
            parse_type_reference(raw, from_unit='energy')

    def test_rejects_a_non_string(self):
        with pytest.raises(MalformedReference):
            parse_type_reference(None, from_unit='energy')


class TestConformantDialect:
    """``energy.yaml#/types/Power`` -- resolvable by any JSON Schema tool."""

    def test_same_document(self):
        parsed = parse_type_reference('#/types/Power', from_unit='energy')
        assert parsed.target == TypeRef('energy', 'Power')
        assert parsed.dialect is Dialect.CONFORMANT
        assert not parsed.is_legacy

    def test_other_file(self):
        parsed = parse_type_reference('units.yaml#/types/Power', from_unit='energy')
        assert parsed.target == TypeRef('units', 'Power')

    def test_relative_to_the_referring_file(self):
        parsed = parse_type_reference(
            '../units.yaml#/types/Power', from_unit='nested/energy')
        assert parsed.target == TypeRef('units', 'Power')

    def test_into_a_subdirectory(self):
        parsed = parse_type_reference('nested/deep.yaml#/types/Thing', from_unit='energy')
        assert parsed.target == TypeRef('nested/deep', 'Thing')

    def test_from_a_non_type_document_is_relative_to_the_types_root(self):
        parsed = parse_type_reference('units.yaml#/types/Power', from_unit=None)
        assert parsed.target == TypeRef('units', 'Power')

    def test_a_non_type_document_cannot_refer_to_itself(self):
        with pytest.raises(MalformedReference, match='not a type unit'):
            parse_type_reference('#/types/Power', from_unit=None)

    def test_rejects_a_path_without_a_suffix(self):
        """Without the extension no standard tool could follow it."""
        with pytest.raises(MalformedReference, match=r'\.yaml'):
            parse_type_reference('units#/types/Power', from_unit='energy')

    def test_rejects_escaping_the_types_directory(self):
        with pytest.raises(MalformedReference, match='outside'):
            parse_type_reference('../../elsewhere.yaml#/types/Power', from_unit='energy')


class TestBothDialectsAgree:

    @pytest.mark.parametrize('legacy, conformant, from_unit', [
        ('/energy#/Power', '#/types/Power', 'energy'),
        ('/units#/Power', 'units.yaml#/types/Power', 'energy'),
        ('/nested/deep#/Thing', 'nested/deep.yaml#/types/Thing', 'energy'),
        ('/units#/Power', '../units.yaml#/types/Power', 'nested/energy'),
    ])
    def test_resolve_to_the_same_target(self, legacy, conformant, from_unit):
        assert (
            parse_type_reference(legacy, from_unit=from_unit).target
            == parse_type_reference(conformant, from_unit=from_unit).target
        )


class TestConformantForm:
    """What the conformance report tells you to write instead."""

    @pytest.mark.parametrize('target, from_unit, expected', [
        (TypeRef('energy', 'Power'), 'energy', '#/types/Power'),
        (TypeRef('units', 'Power'), 'energy', 'units.yaml#/types/Power'),
        (TypeRef('units', 'Power'), 'nested/energy', '../units.yaml#/types/Power'),
        (TypeRef('units', 'Power'), None, 'units.yaml#/types/Power'),
    ])
    def test_suggestion(self, target, from_unit, expected):
        assert conformant_form(target, from_unit=from_unit) == expected

    @pytest.mark.parametrize('from_unit', ['energy', 'nested/energy', None])
    def test_suggestion_round_trips(self, from_unit):
        """The advice has to be advice that works."""
        target = TypeRef('units', 'Power')
        suggested = conformant_form(target, from_unit=from_unit)
        assert parse_type_reference(suggested, from_unit=from_unit).target == target


class TestErrorReferences:
    """Not a JSON Schema construct at all, so there is nothing to migrate."""

    def test_whole_namespace(self):
        parsed = parse_error_reference('/errors/example')
        assert parsed.namespace == 'example'
        assert parsed.name is None
        assert parsed.is_whole_namespace

    def test_single_error(self):
        parsed = parse_error_reference('/errors/example#/ExampleErrorA')
        assert (parsed.namespace, parsed.name) == ('example', 'ExampleErrorA')
        assert not parsed.is_whole_namespace

    @pytest.mark.parametrize('raw', [
        '/error/example',                 # wrong prefix
        'errors/example',                 # no leading slash
        '/errors/Example',                # namespace must start lower case
        '/errors/example#/lowercase',     # error names are upper camel
        '/errors/example#/',
    ])
    def test_rejects_nonsense(self, raw):
        with pytest.raises(MalformedReference):
            parse_error_reference(raw)
