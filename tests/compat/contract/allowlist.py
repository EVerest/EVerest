# SPDX-License-Identifier: Apache-2.0
# Copyright Pionix GmbH and Contributors to EVerest
"""Accepted contract changes.

Most accepted changes need no entry here: a release documents its deprecated and experimental
components in its registry, see registry.py. An entry in ``allowlist.yaml`` accepts a finding that
the rules classify as breaking although no deployment is affected, for example a constraint that
states what the implementation always enforced. Its reason says why. The allowlist does not apply
to a patch release.
"""

from __future__ import annotations

import re
from dataclasses import dataclass, field
from pathlib import Path
from typing import Iterable, Optional

import yaml

from . import registry as registry_module
from .rules import BREAKING, NON_BREAKING, Finding

ENTRY_KEYS = {"pointer", "reason", "kinds", "classifications"}


@dataclass
class AllowlistEntry:
    pointer: str
    reason: str
    kinds: Optional[list[str]] = None
    classifications: list[str] = field(default_factory=lambda: [BREAKING])
    _regex: re.Pattern = field(init=False, repr=False)

    def __post_init__(self) -> None:
        self._regex = re.compile(registry_module.glob_to_regex(self.pointer))

    def matches(self, finding: Finding) -> bool:
        if finding.classification not in self.classifications:
            return False
        if self.kinds is not None and finding.kind not in self.kinds:
            return False
        return self._regex.fullmatch(finding.pointer) is not None


def load_allowlist(path: Path) -> list[AllowlistEntry]:
    doc = yaml.safe_load(Path(path).read_text()) or []
    if not isinstance(doc, list):
        raise ValueError(f"{path}: allowlist must be a list")
    entries = []
    for raw in doc:
        if not isinstance(raw, dict) or "pointer" not in raw or "reason" not in raw:
            raise ValueError(f"{path}: every entry needs 'pointer' and 'reason': {raw!r}")
        if raw.keys() - ENTRY_KEYS:
            raise ValueError(f"{path}: entry {raw['pointer']!r} has unknown keys {sorted(raw.keys() - ENTRY_KEYS)}")
        entries.append(
            AllowlistEntry(
                pointer=str(raw["pointer"]),
                reason=str(raw["reason"]),
                kinds=list(raw["kinds"]) if raw.get("kinds") else None,
                classifications=list(raw.get("classifications") or [BREAKING]),
            )
        )
    return entries


@dataclass
class Exceptions:
    # Findings that are accepted, each with the reason.
    accepted: list[tuple[Finding, str]]
    remaining: list[Finding]
    # Findings a registry entry covers without applying yet, each with the reason. They remain findings.
    not_due: list[tuple[Finding, str]]
    # Allowlist entries that accepted no finding.
    unused: list[AllowlistEntry] = field(default_factory=list)


def apply_exceptions(
    findings: Iterable[Finding],
    registry: dict,
    entries: list[AllowlistEntry],
    target_release: Optional[str] = None,
    patch_release: bool = False,
) -> Exceptions:
    """Split findings into accepted and remaining ones. ``registry`` is the one of the release
    that deployments run, see registry.py."""
    result = Exceptions(accepted=[], remaining=[], not_due=[])
    used: set[int] = set()
    for finding in findings:
        if finding.classification == NON_BREAKING:
            result.remaining.append(finding)
            continue
        accepted, not_due = registry_module.exemption(finding.pointer, registry, target_release, patch_release)
        if accepted is None and not patch_release:
            index = next((index for index, entry in enumerate(entries) if entry.matches(finding)), None)
            if index is not None:
                used.add(index)
                accepted = entries[index].reason
        if accepted is not None:
            result.accepted.append((finding, accepted))
            continue
        result.remaining.append(finding)
        if not_due is not None:
            result.not_due.append((finding, not_due))
    result.unused = [entry for index, entry in enumerate(entries) if index not in used]
    return result
