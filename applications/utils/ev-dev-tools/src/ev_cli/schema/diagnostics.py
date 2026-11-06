# SPDX-License-Identifier: Apache-2.0
# Copyright Pionix GmbH and Contributors to EVerest
"""What the loader has to say about the source text it read.

Diagnostics are the loader's output, never the model's content.  The IR
describes what a definition *means*; whether it was *written* in EVerest's
historical dialect or in an OpenAPI conformant one is a fact about the source
text, and no backend should be able to reach it.  Keeping the two apart is also
what makes the eventual source cleanup a deletion rather than an audit: when a
category stops being reported, the rule that reported it can go.
"""

from __future__ import annotations

from collections import Counter
from dataclasses import dataclass
from enum import Enum
from typing import Iterable, Iterator, Protocol

from ..ir.common import SourceRef


class Severity(Enum):
    INFO = 'info'
    WARNING = 'warning'
    ERROR = 'error'


class Category(Enum):
    """Why a diagnostic was raised.

    The ``reference`` values point at the inventory in the design notes, so a
    report can be traced back to the plan that scheduled each cleanup.
    """

    LEGACY_REF = (
        'legacy-ref', 'Q1',
        'reference written as /<unit>#/<Type>, which resolves only through '
        'EVerest\'s own loader',
    )
    REF_SIBLING_TYPE = (
        'ref-sibling-type', 'Q2d',
        'redundant "type" beside a "$ref"; ignored under draft-07, and must be '
        'kept in step with the referenced type by hand',
    )
    REF_SIBLING_CONTRADICTS = (
        'ref-sibling-contradicts', 'Q2a',
        '"type" beside a "$ref" contradicts the referenced type; ignored today, '
        'but an unsatisfiable schema under OpenAPI semantics',
    )
    REF_SIBLING_CONSTRAINT = (
        'ref-sibling-constraint', 'Q2b/Q2c',
        'validation keyword beside a "$ref"; ignored today, but effective under '
        'OpenAPI semantics',
    )
    UNDECLARED_DIALECT = (
        'undeclared-dialect', 'Q3',
        'document declares no $schema, so its dialect is implied by the '
        'framework meta-schema',
    )
    INTERFACE_SKIPPED = (
        'interface-skipped', '-',
        'interface could not be parsed and was skipped, because no specific '
        'interface was requested',
    )
    SCHEMA_WARNING = (
        'schema-warning', '-',
        'the definition validates, but something about it looks wrong',
    )

    def __init__(self, slug: str, reference: str, explanation: str) -> None:
        self.slug = slug
        self.reference = reference
        self.explanation = explanation

    def __str__(self) -> str:
        return self.slug


@dataclass(frozen=True)
class Diagnostic:
    category: Category
    severity: Severity
    source: SourceRef
    message: str
    #: How the same thing would be written conformantly, when that is known.
    conformant_form: str | None = None

    def format(self) -> str:
        line = f'{self.severity.value}: {self.source}: {self.message}'
        if self.conformant_form:
            line += f'\n    conformant form: {self.conformant_form}'
        return line


class Sink(Protocol):
    """Where diagnostics go."""

    def emit(self, diagnostic: Diagnostic) -> None: ...


class NullSink:
    """Discards everything.  Used when only the model is wanted."""

    def emit(self, diagnostic: Diagnostic) -> None:  # noqa: D102 - trivial
        pass


class CollectingSink:
    """Keeps everything, for the conformance report and for tests."""

    def __init__(self) -> None:
        self._diagnostics: list[Diagnostic] = []

    def emit(self, diagnostic: Diagnostic) -> None:
        self._diagnostics.append(diagnostic)

    def __iter__(self) -> Iterator[Diagnostic]:
        return iter(self._diagnostics)

    def __len__(self) -> int:
        return len(self._diagnostics)

    @property
    def diagnostics(self) -> tuple[Diagnostic, ...]:
        return tuple(self._diagnostics)

    def counts(self) -> Counter[Category]:
        return Counter(d.category for d in self._diagnostics)

    def of(self, *categories: Category) -> tuple[Diagnostic, ...]:
        wanted = set(categories)
        return tuple(d for d in self._diagnostics if d.category in wanted)

    def worst_severity(self) -> Severity | None:
        order = [Severity.INFO, Severity.WARNING, Severity.ERROR]
        present = [d.severity for d in self._diagnostics]
        return max(present, key=order.index) if present else None


class PrintingSink:
    """Writes warnings and errors out as they happen.

    Used during normal generation: the legacy dialect is expected and stays
    quiet at INFO, but anything the loader considers suspicious is worth seeing
    in the build log.
    """

    def __init__(self, stream, minimum: Severity = Severity.WARNING) -> None:
        self._stream = stream
        self._order = [Severity.INFO, Severity.WARNING, Severity.ERROR]
        self._minimum = minimum

    def emit(self, diagnostic: Diagnostic) -> None:
        if self._order.index(diagnostic.severity) >= self._order.index(self._minimum):
            print(diagnostic.format(), file=self._stream)


class TeeSink:
    """Feeds several sinks at once."""

    def __init__(self, *sinks: Sink) -> None:
        self._sinks = sinks

    def emit(self, diagnostic: Diagnostic) -> None:
        for sink in self._sinks:
            sink.emit(diagnostic)


def format_report(sink: CollectingSink, *, categories: Iterable[Category] | None = None) -> str:
    """Render a grouped, counted report -- the shape ``conformance-report`` prints."""
    wanted = set(categories) if categories is not None else set(Category)
    selected = [d for d in sink if d.category in wanted]
    if not selected:
        return 'No diagnostics: every definition read is already conformant.'

    counts = Counter(d.category for d in selected)
    lines = ['Conformance report', '==================', '']
    for category in Category:
        if category not in counts:
            continue
        lines.append(f'{category.slug} ({category.reference}): {counts[category]}')
        lines.append(f'    {category.explanation}')
        for diagnostic in selected:
            if diagnostic.category is not category:
                continue
            lines.append(f'      {diagnostic.source}')
            if diagnostic.conformant_form:
                lines.append(f'          -> {diagnostic.conformant_form}')
        lines.append('')

    lines.append(f'total: {len(selected)}')
    return '\n'.join(lines)


def format_summary(sink: CollectingSink) -> str:
    """One line per category, without the individual sites."""
    counts = sink.counts()
    if not counts:
        return 'No diagnostics.'
    width = max(len(c.slug) for c in counts)
    lines = [
        f'{category.slug:<{width}}  {counts[category]:>5}  ({category.reference})'
        for category in Category if category in counts
    ]
    lines.append(f'{"total":<{width}}  {sum(counts.values()):>5}')
    return '\n'.join(lines)
