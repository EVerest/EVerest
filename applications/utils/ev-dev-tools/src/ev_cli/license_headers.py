# SPDX-License-Identifier: Apache-2.0
# Copyright Pionix GmbH and Contributors to EVerest
"""Finding the license header a generated module source should carry.

A module's manifest names its license by URI.  The text for that URI is looked
up on disk, under a path derived from the URI, so that a project can supply its
own texts without ev-cli knowing anything about them.
"""

from __future__ import annotations

from pathlib import Path
from typing import Sequence

from .errors import LicenseNotFound

#: Stripped from a license URI to get the path to look for.
URL_SCHEMES = ('http://', 'https://')

#: Texts shipped with ev-cli itself.
BUNDLED_DIR = Path(__file__).parent / 'licenses'


def license_path_fragment(license_url: str) -> str:
    for scheme in URL_SCHEMES:
        if license_url.startswith(scheme):
            return license_url[len(scheme):]
    return license_url


def search_dirs(work_dir: Path, additional: Path | None) -> list[Path]:
    """Where license texts are looked for, in increasing precedence.

    Later entries win, so a project's own directory overrides the bundled text.
    """
    dirs = [BUNDLED_DIR, Path(work_dir) / 'licenses']
    if additional is not None:
        dirs.append(Path(additional))
    return dirs


def find_header(dirs: Sequence[Path], license_url: str) -> str:
    """The license text for ``license_url``, from the last directory that has it."""
    fragment = license_path_fragment(license_url)

    found: Path | None = None
    for directory in dirs:
        candidate = Path(directory) / fragment
        if candidate.exists():
            found = candidate

    if found is None:
        raise LicenseNotFound(
            f'could not find the license "{license_url}" in any of '
            f'{", ".join(str(d) for d in dirs)}. Pass --licenses pointing at a '
            f'directory containing "{fragment}".'
        )

    return found.read_text().strip()
