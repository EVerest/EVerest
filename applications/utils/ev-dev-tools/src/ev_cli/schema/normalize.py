# SPDX-License-Identifier: Apache-2.0
# Copyright Pionix GmbH and Contributors to EVerest
"""Absorbing the legacy dialect in one place.

Under JSON Schema draft-07 every keyword sitting beside a ``$ref`` is
*ignored*.  Under 2020-12 -- and therefore under OpenAPI 3.1 -- the same
keywords are *applied*, as an intersection with the referenced schema.  The
definitions in everest-core lean on the draft-07 reading heavily: nearly every
reference carries a redundant ``type``, and a handful carry keywords that would
change meaning outright if they were ever honoured.

Every generator in the tree already ignores those siblings, so dropping them
here changes no output.  What it does change is that the IR handed to a backend
is conformant: a reference is a reference plus a description, and nothing else.
Each dropped keyword is reported so that the sources can be cleaned up later as
their own change, and so that the two genuinely dangerous shapes are visible
rather than latent.
"""

from __future__ import annotations

from typing import Any

from ..ir.common import SourceRef
from ..ir.types import TypeRef
from .diagnostics import Category, Diagnostic, Severity, Sink
from .references import ParsedReference, conformant_form

#: Keys that carry meaning of their own beside a ``$ref`` and are kept.
KEPT_BESIDE_REF = frozenset({'$ref', 'description'})


def ref_siblings(schema: dict[str, Any]) -> dict[str, Any]:
    """The keys beside a ``$ref`` that the IR does not carry over."""
    return {key: value for key, value in schema.items() if key not in KEPT_BESIDE_REF}


def report_reference(
    schema: dict[str, Any],
    *,
    reference: ParsedReference,
    source: SourceRef,
    from_unit: str,
    target_json_type: str | None,
    sink: Sink,
) -> None:
    """Report everything non-conformant about one reference node.

    ``target_json_type`` is the ``type`` the referenced definition declares, or
    ``None`` when it could not be determined; it is what makes a redundant
    ``type`` distinguishable from a contradictory one.
    """
    if reference.is_legacy:
        sink.emit(Diagnostic(
            category=Category.LEGACY_REF,
            severity=Severity.INFO,
            source=source,
            message=f'reference {reference.raw!r} uses the legacy EVerest form',
            conformant_form=conformant_form(reference.target, from_unit=from_unit),
        ))

    siblings = ref_siblings(schema)
    declared_type = siblings.pop('type', None)

    if declared_type is not None:
        _report_declared_type(
            declared_type,
            target_json_type=target_json_type,
            target=reference.target,
            source=source,
            sink=sink,
        )

    for keyword, value in siblings.items():
        _report_constraint(
            keyword, value,
            target_json_type=target_json_type,
            target=reference.target,
            source=source,
            sink=sink,
        )


def _report_declared_type(
    declared_type: Any,
    *,
    target_json_type: str | None,
    target: TypeRef,
    source: SourceRef,
    sink: Sink,
) -> None:
    if target_json_type is not None and declared_type != target_json_type:
        sink.emit(Diagnostic(
            category=Category.REF_SIBLING_CONTRADICTS,
            severity=Severity.WARNING,
            source=source,
            message=(
                f'"type: {declared_type}" sits beside a reference to {target}, '
                f'which declares "type: {target_json_type}". Draft-07 ignores '
                f'this, so it has no effect today; read with OpenAPI semantics '
                f'the two intersect and no value can satisfy the result.'
            ),
            conformant_form='remove the "type" key',
        ))
        return

    sink.emit(Diagnostic(
        category=Category.REF_SIBLING_TYPE,
        severity=Severity.INFO,
        source=source,
        message=(
            f'"type: {declared_type}" beside a reference to {target} is '
            f'redundant; the referenced definition already declares it'
        ),
        conformant_form='remove the "type" key',
    ))


def _report_constraint(
    keyword: str,
    value: Any,
    *,
    target_json_type: str | None,
    target: TypeRef,
    source: SourceRef,
    sink: Sink,
) -> None:
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
        severity = Severity.WARNING
    else:
        message = (
            f'"{keyword}" beside a reference to {target} is ignored today, but '
            f'would take effect when read with OpenAPI semantics'
        )
        severity = Severity.INFO

    sink.emit(Diagnostic(
        category=Category.REF_SIBLING_CONSTRAINT,
        severity=severity,
        source=source,
        message=message,
        conformant_form=(
            f'move "{keyword}" into the referenced definition, or drop it'
        ),
    ))
