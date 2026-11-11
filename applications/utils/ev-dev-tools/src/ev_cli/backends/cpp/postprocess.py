# SPDX-License-Identifier: Apache-2.0
# Copyright Pionix GmbH and Contributors to EVerest
"""Formatting the generated C++ sources with clang-format."""

from __future__ import annotations

import shutil
import subprocess
from pathlib import Path

from ...errors import EvCliError
from ..base import GeneratedFile


def format_sources(files: list[GeneratedFile], config_dir: Path) -> None:
    """Format the ``.hpp`` and ``.cpp`` files among ``files`` in place.

    The style is the ``.clang-format`` in ``config_dir``.
    """
    sources = [file for file in files if file.path.suffix in ('.hpp', '.cpp')]
    if not sources:
        return
    executable = _check_clang_format_setup(config_dir)
    for file in sources:
        file.content = _format(executable, config_dir, file.content)


def _check_clang_format_setup(config_dir: Path) -> str:
    """The clang-format executable, once ``config_dir`` is known to configure it."""
    executable = shutil.which('clang-format')
    if executable is None:
        raise EvCliError(
            'could not find clang-format; pass --disable-clang-format to skip '
            'formatting generated sources'
        )
    if not config_dir.is_dir():
        raise EvCliError(f'clang-format directory {config_dir} does not exist')
    if not (config_dir / '.clang-format').exists():
        raise EvCliError(f'clang-format directory {config_dir} contains no .clang-format file')
    return executable


def _format(executable: str, config_dir: Path, source: str) -> str:
    completed = subprocess.run(
        [executable, '--style=file'],
        input=source,
        capture_output=True,
        cwd=config_dir,
        encoding='utf-8',
        check=False,
    )
    if completed.returncode != 0:
        raise EvCliError(f'clang-format failed:\n{completed.stderr}')
    return completed.stdout
