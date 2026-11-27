# SPDX-License-Identifier: Apache-2.0
# Copyright Pionix GmbH and Contributors to EVerest
"""The exceptions to the stability guarantees that a release documents.

A release records its deprecated and its experimental public API components in two registry files.
An entry names the parts of the contract it applies to as pointer globs under ``covers``. A change
to such a part is accepted when the release the change is compared with has the entry: an
experimental component may change in any release, a deprecated one from the release its entry
names as the earliest removal. A patch release keeps what its release line deprecated.

The registry of the tree under test decides nothing. The entry of a removed component is deleted
with it, and an entry added after the release announces nothing to the deployments running that
release.
"""

from __future__ import annotations

import re
from pathlib import Path
from typing import Optional

import yaml

from .release import parse_release

REGISTRY_FILES = {
    "deprecations": "docs/source/project/releases/deprecations.yaml",
    "experimental": "docs/source/project/releases/experimental.yaml",
}
# What a snapshot keeps of an entry; the prose is for the rendered index pages.
ENTRY_KEYS = ("id", "covers", "deprecated_in", "earliest_removal", "since")


def glob_to_regex(pattern: str) -> str:
    """``*`` matches within one path segment, ``**`` matches across segments."""
    out = []
    i = 0
    while i < len(pattern):
        if pattern.startswith("**", i):
            out.append(".*")
            i += 2
        elif pattern[i] == "*":
            out.append("[^/]*")
            i += 1
        else:
            out.append(re.escape(pattern[i]))
            i += 1
    return "".join(out)


def covers(pattern: str, pointer: str) -> bool:
    """Whether a ``covers`` path names the pointer or something the pointer is part of."""
    return re.fullmatch(glob_to_regex(pattern) + "(/.*)?", pointer) is not None


def collect(root: Path) -> dict[str, list[dict]]:
    """The registries of a source tree; a tree without the files has none."""
    registry = {}
    for name, relative in REGISTRY_FILES.items():
        path = Path(root) / relative
        entries = (yaml.safe_load(path.read_text()) or []) if path.exists() else []
        registry[name] = [{key: entry[key] for key in ENTRY_KEYS if key in entry} for entry in entries]
    return registry


def _covering(entries: list[dict], pointer: str) -> list[dict]:
    return [entry for entry in entries if any(covers(pattern, pointer) for pattern in entry.get("covers") or [])]


def exemption(
    pointer: str, registry: dict, target_release: Optional[str], patch_release: bool = False
) -> tuple[Optional[str], Optional[str]]:
    """Why a change at ``pointer`` is accepted, or why a registry entry that covers it does not
    apply yet. Both are None when the registry does not cover the pointer.

    ``registry`` is the one of the release that deployments run, ``target_release`` the release the
    changed tree becomes."""
    experimental = _covering(registry.get("experimental") or [], pointer)
    if experimental:
        entry = experimental[0]
        return f"experimental since {entry.get('since')} ({entry['id']})", None

    not_due = None
    for entry in _covering(registry.get("deprecations") or [], pointer):
        earliest = parse_release(str(entry.get("earliest_removal")))
        if patch_release:
            not_due = f"deprecated ({entry['id']}), but a patch release keeps deprecated components"
        elif earliest is None:
            not_due = f"deprecated ({entry['id']}), but the entry states no earliest removal release"
        elif target_release is None or parse_release(target_release) is None:
            raise ValueError(f"cannot tell whether the removal of {entry['id']!r} is due: no release for the checked tree")
        elif earliest > parse_release(target_release):
            not_due = (
                f"deprecated ({entry['id']}), may be removed in {entry['earliest_removal']} at the earliest, "
                f"not in {target_release}"
            )
        else:
            return (
                f"deprecated in {entry.get('deprecated_in')}, removal announced for {entry['earliest_removal']} "
                f"({entry['id']})",
                None,
            )
    return None, not_due
