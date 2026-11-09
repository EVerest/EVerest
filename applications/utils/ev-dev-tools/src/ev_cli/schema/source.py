# SPDX-License-Identifier: Apache-2.0
# Copyright Pionix GmbH and Contributors to EVerest
"""Positions in definition documents."""

from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path


def escape_pointer_segment(segment: str) -> str:
    """Escape one segment of a JSON pointer as RFC 6901 requires."""
    return str(segment).replace('~', '~0').replace('/', '~1')


@dataclass(frozen=True)
class SourceRef:
    """Where a node came from.

    This is provenance, not judgement.  It records which file and which
    position within it produced a node so that a backend can raise a useful
    error, and says nothing about whether that position was written well.
    Opinions about the source text are conformance findings and live in
    :mod:`ev_cli.schema.conformance` instead, so that nothing a backend can
    reach depends on how the source happened to be spelled.
    """

    file: Path
    pointer: str = ''

    def child(self, *segments: str | int) -> SourceRef:
        """Return a reference to a position nested below this one."""
        pointer = self.pointer
        for segment in segments:
            pointer += '/' + escape_pointer_segment(segment)
        return SourceRef(self.file, pointer)

    def __str__(self) -> str:
        return f'{self.file}#{self.pointer}' if self.pointer else str(self.file)
