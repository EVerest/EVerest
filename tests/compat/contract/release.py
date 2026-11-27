# SPDX-License-Identifier: Apache-2.0
# Copyright Pionix GmbH and Contributors to EVerest
"""Releases, release lines and the release a tree is compared with.

A release is ``<year>.<month>.<patch>``, its release line ``<year>.<month>``. The line is
maintained on the branch ``stable/<line>``, where every release after ``<line>.0`` is a patch
release: nothing in the public API may break there. Development towards the next line happens on
main, where a break is accepted for what the latest release documents as deprecated or
experimental.
"""

from __future__ import annotations

import re
import subprocess
from dataclasses import dataclass
from pathlib import Path
from typing import Iterable, Optional

Release = tuple[int, int, int]

RELEASE_RE = re.compile(r"\b(\d{4})\.(\d{2})\.(\d+)\b")
FINAL_RELEASE_TAG_RE = re.compile(r"\d{4}\.\d{2}\.\d+")
PROJECT_VERSION_RE = re.compile(r"project\s*\(\s*everest-core\s+VERSION\s+(\d{4}\.\d{2}\.\d+)")
STABLE_BRANCH_PREFIX = "stable/"


def parse_release(text: str) -> Optional[Release]:
    match = RELEASE_RE.search(text or "")
    return tuple(int(part) for part in match.groups()) if match else None


def format_release(release: Release) -> str:
    return "{}.{:02d}.{}".format(*release)


def release_line(release: str) -> str:
    """``2026.10.0`` and ``2026.10`` both belong to the release line ``2026.10``."""
    return ".".join(release.split(".")[:2])


def project_version(root: Path) -> Optional[str]:
    """The release a source tree is developed towards: its CMake project version."""
    cmake_lists = Path(root) / "CMakeLists.txt"
    if not cmake_lists.exists():
        return None
    match = PROJECT_VERSION_RE.search(cmake_lists.read_text())
    return match.group(1) if match else None


def final_releases(tags: Iterable[str]) -> list[str]:
    """The tags of final releases, oldest first. Release candidates are not baselines."""
    return sorted((tag for tag in tags if FINAL_RELEASE_TAG_RE.fullmatch(tag)), key=parse_release)


def git_tags(repo: Path) -> list[str]:
    result = subprocess.run(["git", "-C", str(repo), "tag", "--list"], capture_output=True, text=True, check=True)
    return result.stdout.split()


@dataclass(frozen=True)
class ReleaseBaseline:
    tag: str
    # The tree continues the release line of ``tag``: only experimental components may change.
    patch_release: bool


def release_baseline(
    tags: Iterable[str], target_branch: Optional[str] = None, new_tag: Optional[str] = None
) -> Optional[ReleaseBaseline]:
    """The release whose deployments upgrade to the tree, None when no release exists yet.

    ``new_tag`` is the release the tree is tagged as, ``target_branch`` the branch it is developed
    on. A patch release, and the stable branch it is developed on, follow the latest release of
    their line. The first release of a line, and the development towards it, follow the latest
    release of the lines before."""
    releases = final_releases(tags)
    if new_tag is not None:
        new_release = parse_release(new_tag)
        if new_release is None:
            raise ValueError(f"not a release tag: {new_tag!r}")
        earlier = [tag for tag in releases if parse_release(tag) < new_release]
        if not earlier:
            return None
        return ReleaseBaseline(earlier[-1], patch_release=release_line(earlier[-1]) == release_line(new_tag))
    if target_branch and target_branch.startswith(STABLE_BRANCH_PREFIX):
        line = target_branch[len(STABLE_BRANCH_PREFIX):]
        of_line = [tag for tag in releases if release_line(tag) == line]
        if of_line:
            # Deployments of the line run its latest release, which may ship more than the first.
            return ReleaseBaseline(of_line[-1], patch_release=True)
        releases = [tag for tag in releases if release_line(tag) < line]
    return ReleaseBaseline(releases[-1], patch_release=False) if releases else None
