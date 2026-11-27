# SPDX-License-Identifier: Apache-2.0
# Copyright Pionix GmbH and Contributors to EVerest
"""Render diff results as Markdown and JSON."""

from __future__ import annotations

import json
from dataclasses import asdict
from typing import Optional

from .rules import BREAKING, NON_BREAKING, UNCLASSIFIED, Finding, by_classification


def render_markdown(
    old_label: str,
    new_label: str,
    remaining: list[Finding],
    accepted: list[tuple[Finding, str]],
    show_non_breaking: bool = False,
    not_due: Optional[list[tuple[Finding, str]]] = None,
    unreleased: Optional[list[Finding]] = None,
    unused_allowlist: Optional[list[str]] = None,
) -> str:
    groups = by_classification(remaining)
    lines = [f"## Contract diff: `{old_label}` -> `{new_label}`", ""]
    lines.append(
        f"{len(groups[BREAKING])} breaking, {len(groups[UNCLASSIFIED])} unclassified, "
        f"{len(groups[NON_BREAKING])} non-breaking, {len(accepted)} accepted"
    )
    lines.append("")

    def section(title: str, findings: list[Finding]) -> None:
        if not findings:
            return
        lines.append(f"### {title} ({len(findings)})")
        lines.append("")
        for finding in findings:
            lines.append(f"- `{finding.pointer}` ({finding.kind}): {finding.message}")
        lines.append("")

    section("Breaking", groups[BREAKING])
    section("Unclassified, no rule covers the changed construct", groups[UNCLASSIFIED])
    if show_non_breaking:
        section("Non-breaking", groups[NON_BREAKING])
    if not_due:
        lines.append(f"### Covered by the release's registry, but not accepted ({len(not_due)})")
        lines.append("")
        for finding, why in not_due:
            lines.append(f"- `{finding.pointer}`: {why}")
        lines.append("")
    section("Changes to what no release ships yet, not counted", unreleased or [])
    if accepted:
        lines.append(f"### Accepted ({len(accepted)})")
        lines.append("")
        for finding, why in accepted:
            lines.append(f"- `{finding.pointer}` ({finding.kind}): {finding.message}; {why}")
        lines.append("")
    if unused_allowlist:
        lines.append(f"### Allowlist entries that accept nothing in this comparison ({len(unused_allowlist)})")
        lines.append("")
        lines.extend(f"- `{pointer}`" for pointer in unused_allowlist)
        lines.append("")
    return "\n".join(lines)


def render_json(
    old_label: str,
    new_label: str,
    remaining: list[Finding],
    accepted: list[tuple[Finding, str]],
    not_due: Optional[list[tuple[Finding, str]]] = None,
    unreleased: Optional[list[Finding]] = None,
) -> str:
    return json.dumps(
        {
            "old": old_label,
            "new": new_label,
            "findings": [asdict(f) for f in remaining],
            "accepted": [{**asdict(f), "why": why} for f, why in accepted],
            "not_due": [{**asdict(f), "why": why} for f, why in not_due or []],
            "unreleased": [asdict(f) for f in unreleased or []],
        },
        indent=1,
    )


def summary_line(remaining: list[Finding], accepted_count: int) -> str:
    groups = by_classification(remaining)
    return (
        f"breaking={len(groups[BREAKING])} unclassified={len(groups[UNCLASSIFIED])} "
        f"non-breaking={len(groups[NON_BREAKING])} accepted={accepted_count}"
    )
