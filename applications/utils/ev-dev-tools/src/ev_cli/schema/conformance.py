# SPDX-License-Identifier: Apache-2.0
# Copyright Pionix GmbH and Contributors to EVerest
"""Reporting what in a reference would not conform to JSON Schema 2020-12.

Under JSON Schema draft-07 every keyword sitting beside a ``$ref`` is
*ignored*.  Under 2020-12 -- and therefore under OpenAPI 3.1 -- the same
keywords are *applied*, as an intersection with the referenced schema.  The
definitions in everest-core lean on the draft-07 reading heavily: nearly every
reference carries a redundant ``type``, and a handful carry keywords that would
change meaning outright if they were ever honoured.

Every generator in the tree already ignores those siblings, and the IR does
not carry them: a reference there is a reference plus a description, and
nothing else.  Each sibling is reported here, as is a reference written in the
legacy form, so that the sources can be cleaned up and so that the two
genuinely dangerous shapes are visible rather than latent.

Findings are kept apart from the IR: whether a definition was written in
EVerest's historical dialect is a fact about the source text, and no backend
should be able to reach it.  They come from a pass of their own over every
definition document -- type units, interfaces, error lists and module manifests
-- so they do not depend on what a generator makes of the definitions.
"""

from __future__ import annotations

from collections import Counter
from dataclasses import dataclass
from enum import Enum
from typing import Any, Iterator, Mapping, Sequence

from .loader import DefinitionLoader, Document
from .references import ParsedReference, TypeRef, format_type_reference, parse_type_reference
from .source import SourceRef

#: Keys that carry meaning of their own beside a ``$ref`` and are kept.
KEPT_BESIDE_REF = frozenset({'$ref', 'description'})


class Category(Enum):
    """What kind of non-conformance a finding is."""

    LEGACY_REF = (
        'legacy-ref',
        'reference written as /<unit>#/<Type>, which resolves only through '
        'EVerest\'s own loader',
    )
    REF_SIBLING_TYPE = (
        'ref-sibling-type',
        'redundant "type" beside a "$ref"; ignored under draft-07, and must be '
        'kept in step with the referenced type by hand',
    )
    REF_SIBLING_CONTRADICTS = (
        'ref-sibling-contradicts',
        '"type" beside a "$ref" contradicts the referenced type; ignored today, '
        'but an unsatisfiable schema under OpenAPI semantics',
    )
    REF_SIBLING_CONSTRAINT = (
        'ref-sibling-constraint',
        'validation keyword beside a "$ref"; ignored today, but effective under '
        'OpenAPI semantics',
    )
    UNDECLARED_DIALECT = (
        'undeclared-dialect',
        'document declares no $schema, so its dialect is implied by the '
        'framework meta-schema',
    )

    def __init__(self, slug: str, explanation: str) -> None:
        self.slug = slug
        self.explanation = explanation


@dataclass(frozen=True)
class Finding:
    """One place where a definition relies on EVerest's own reading of JSON Schema."""

    category: Category
    source: SourceRef
    message: str
    #: How to write the same thing conformantly.
    conformant_form: str


def format_report(findings: Sequence[Finding]) -> str:
    """Every finding, grouped by category."""
    if not findings:
        return 'Every definition read is already conformant.'

    counts = Counter(finding.category for finding in findings)
    lines = ['Conformance report', '==================', '']
    for category in Category:
        if category not in counts:
            continue
        lines.append(f'{category.slug}: {counts[category]}')
        lines.append(f'    {category.explanation}')
        for finding in findings:
            if finding.category is category:
                lines.append(f'      {finding.source}: {finding.message}')
                lines.append(f'          -> {finding.conformant_form}')
        lines.append('')

    lines.append(f'total: {len(findings)}')
    return '\n'.join(lines)


def format_summary(findings: Sequence[Finding]) -> str:
    """One line per category, without the individual sites."""
    if not findings:
        return 'Every definition read is already conformant.'

    counts = Counter(finding.category for finding in findings)
    width = max(len(category.slug) for category in counts)
    lines = [
        f'{category.slug:<{width}}  {counts[category]:>5}'
        for category in Category if category in counts
    ]
    lines.append(f'{"total":<{width}}  {len(findings):>5}')
    return '\n'.join(lines)


def collect_findings(loader: DefinitionLoader) -> list[Finding]:
    """Every place where the definitions rely on EVerest's own reading of JSON Schema."""
    tree = loader.tree
    findings: list[Finding] = []
    for unit in tree.type_units():
        findings.extend(_find_in_document(loader, loader.type_document(unit), from_unit=unit))
    for name in tree.interface_names():
        findings.extend(_find_in_document(loader, loader.interface_document(name), from_unit=None))
    for namespace in tree.error_namespaces():
        findings.extend(_find_in_document(loader, loader.error_document(namespace), from_unit=None))
    for module in tree.module_names():
        findings.extend(_find_in_document(loader, loader.module_document(module), from_unit=None))
    return findings


def _find_in_document(
    loader: DefinitionLoader, document: Document, *, from_unit: str | None
) -> Iterator[Finding]:
    if '$schema' not in document.data:
        yield Finding(
            category=Category.UNDECLARED_DIALECT,
            source=document.source,
            message='no $schema; the dialect is implied by the framework meta-schema rather than declared',
            conformant_form='$schema: https://json-schema.org/draft/2020-12/schema',
        )
    yield from _find_in_node(loader, document.data, document.source, from_unit=from_unit)


def _find_in_node(
    loader: DefinitionLoader, node: Any, source: SourceRef, *, from_unit: str | None
) -> Iterator[Finding]:
    """Every finding about a ``$ref`` at or below ``node``, wherever in the document it sits."""
    if isinstance(node, Mapping):
        if isinstance(node.get('$ref'), str):
            reference = parse_type_reference(node['$ref'], from_unit=from_unit)
            yield from _find_in_reference(
                node,
                reference=reference,
                source=source,
                from_unit=from_unit,
                target_json_type=loader.type_declaration(reference.target, source).get('type'),
            )
        for key, value in node.items():
            yield from _find_in_node(loader, value, source.child(key), from_unit=from_unit)
    elif isinstance(node, list):
        for index, value in enumerate(node):
            yield from _find_in_node(loader, value, source.child(index), from_unit=from_unit)


def _find_in_reference(
    schema: Mapping[str, Any],
    *,
    reference: ParsedReference,
    source: SourceRef,
    from_unit: str | None,
    target_json_type: str | None,
) -> Iterator[Finding]:
    """Everything non-conformant about one reference node.

    ``target_json_type`` is the ``type`` the referenced definition declares, or
    ``None`` when it could not be determined; it is what makes a redundant
    ``type`` distinguishable from a contradictory one.
    """
    if reference.is_legacy:
        yield Finding(
            category=Category.LEGACY_REF,
            source=source,
            message=f'reference {reference.raw!r} uses the legacy EVerest form',
            conformant_form=format_type_reference(reference.target, from_unit=from_unit),
        )

    siblings = {key: value for key, value in schema.items() if key not in KEPT_BESIDE_REF}
    declared_type = siblings.pop('type', None)

    if declared_type is not None:
        yield _classify_declared_type(
            declared_type,
            target_json_type=target_json_type,
            target=reference.target,
            source=source,
        )

    for keyword, value in siblings.items():
        yield _classify_constraint(
            keyword, value,
            target_json_type=target_json_type,
            target=reference.target,
            source=source,
        )


def _classify_declared_type(
    declared_type: Any,
    *,
    target_json_type: str | None,
    target: TypeRef,
    source: SourceRef,
) -> Finding:
    if target_json_type is not None and declared_type != target_json_type:
        return Finding(
            category=Category.REF_SIBLING_CONTRADICTS,
            source=source,
            message=(
                f'"type: {declared_type}" sits beside a reference to {target}, '
                f'which declares "type: {target_json_type}". Draft-07 ignores '
                f'this, so it has no effect today; read with OpenAPI semantics '
                f'the two intersect and no value can satisfy the result.'
            ),
            conformant_form='remove the "type" key',
        )

    return Finding(
        category=Category.REF_SIBLING_TYPE,
        source=source,
        message=(
            f'"type: {declared_type}" beside a reference to {target} is '
            f'redundant; the referenced definition already declares it'
        ),
        conformant_form='remove the "type" key',
    )


def _classify_constraint(
    keyword: str,
    value: Any,
    *,
    target_json_type: str | None,
    target: TypeRef,
    source: SourceRef,
) -> Finding:
    # additionalProperties does not see through a $ref under 2020-12, so
    # `additionalProperties: false` beside a reference to an object would
    # reject every property that object declares.
    harmful = (
        keyword == 'additionalProperties'
        and value is False
        and target_json_type in (None, 'object')
    )
    if harmful:
        message = (
            f'"additionalProperties: false" sits beside a reference to {target}. '
            f'Draft-07 ignores it; read with OpenAPI semantics it does not see '
            f'through the reference and would reject every property the '
            f'referenced type declares.'
        )
    else:
        message = (
            f'"{keyword}" beside a reference to {target} is ignored today, but '
            f'would take effect when read with OpenAPI semantics'
        )

    return Finding(
        category=Category.REF_SIBLING_CONSTRAINT,
        source=source,
        message=message,
        conformant_form=(
            f'move "{keyword}" into the referenced definition, or drop it'
        ),
    )
