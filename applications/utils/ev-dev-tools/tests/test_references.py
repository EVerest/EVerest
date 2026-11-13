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
from ev_cli.schema.references import (
    Dialect,
    TypeRef,
    format_type_reference,
    parse_error_reference,
    parse_type_reference,
)


def accepted(parse, candidates):
    """The candidates ``parse`` takes without raising MalformedReference."""
    taken = []
    for raw in candidates:
        try:
            parse(raw)
        except MalformedReference:
            continue
        taken.append(raw)
    return taken


class TestLegacyDialect:
    """``/energy#/Power``, what every definition in everest-core uses."""

    def test_same_unit(self):
        parsed = parse_type_reference('/energy#/Power', from_unit='energy')
        assert parsed.target == TypeRef('energy', 'Power')
        assert parsed.dialect is Dialect.LEGACY
        assert parsed.is_legacy

    def test_other_unit(self):
        parsed = parse_type_reference('/units#/Power', from_unit='energy')
        assert parsed.target == TypeRef('units', 'Power')

    def test_rejects_a_unit_in_a_subdirectory(self):
        with pytest.raises(MalformedReference, match='subdirectory'):
            parse_type_reference('/nested/deep#/Thing', from_unit='energy')

    def test_from_a_non_type_document(self):
        """An interface refers to types the same way a type file does."""
        parsed = parse_type_reference('/units#/Power', from_unit=None)
        assert parsed.target == TypeRef('units', 'Power')

    def test_rejects_nonsense(self):
        assert accepted(lambda raw: parse_type_reference(raw, from_unit='energy'), [
            'energy#/Power',        # no leading slash and no .yaml
            '/energy',              # no fragment
            '/energy#Power',        # fragment is not a pointer
            '/energy#/a/b',         # fragment names more than a type
            '/#/Power',             # no unit
            '',
        ]) == []

    def test_rejects_a_non_string(self):
        with pytest.raises(MalformedReference):
            parse_type_reference(None, from_unit='energy')


class TestConformantDialect:
    """``energy.yaml#/types/Power``, resolvable by any JSON Schema tool."""

    def test_same_document(self):
        parsed = parse_type_reference('#/types/Power', from_unit='energy')
        assert parsed.target == TypeRef('energy', 'Power')
        assert parsed.dialect is Dialect.CONFORMANT
        assert not parsed.is_legacy

    def test_other_file(self):
        parsed = parse_type_reference('units.yaml#/types/Power', from_unit='energy')
        assert parsed.target == TypeRef('units', 'Power')

    def test_normalises_the_path(self):
        parsed = parse_type_reference('./units.yaml#/types/Power', from_unit='energy')
        assert parsed.target == TypeRef('units', 'Power')

    def test_rejects_a_subdirectory(self):
        with pytest.raises(MalformedReference, match='subdirectory'):
            parse_type_reference('nested/deep.yaml#/types/Thing', from_unit='energy')

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

    def test_resolve_to_the_same_target(self):
        for legacy, conformant in (('/energy#/Power', '#/types/Power'),
                                   ('/units#/Power', 'units.yaml#/types/Power')):
            assert (
                parse_type_reference(legacy, from_unit='energy').target
                == parse_type_reference(conformant, from_unit='energy').target
            )


class TestFormatTypeReference:
    """What the conformance report tells you to write instead."""

    def test_suggestion(self):
        assert format_type_reference(TypeRef('energy', 'Power'), from_unit='energy') == '#/types/Power'
        assert format_type_reference(TypeRef('units', 'Power'), from_unit='energy') == 'units.yaml#/types/Power'
        assert format_type_reference(TypeRef('units', 'Power'), from_unit=None) == 'units.yaml#/types/Power'

    def test_suggestion_round_trips(self):
        """The advice has to be advice that works."""
        target = TypeRef('units', 'Power')
        for from_unit in ('energy', None):
            suggested = format_type_reference(target, from_unit=from_unit)
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

    def test_rejects_nonsense(self):
        assert accepted(parse_error_reference, [
            '/error/example',                 # wrong prefix
            'errors/example',                 # no leading slash
            '/errors/Example',                # namespace must start lower case
            '/errors/example#/lowercase',     # error names are upper camel
            '/errors/example#/',
        ]) == []
