# SPDX-License-Identifier: Apache-2.0
# Copyright Pionix GmbH and Contributors to EVerest
"""Deciding whether and how a generated file reaches the disk.

EVerest's CMake build leans on these rules: it pairs ev-cli's own
modification-time comparison with a stamp file, so a strategy that wrote too
eagerly would rebuild the world and one that wrote too rarely would ship stale
headers.  ``module create`` and ``module update`` lean on them too, for the
much stronger reason that a wrong answer overwrites someone's code.
"""

from __future__ import annotations

import shutil
import subprocess
import sys
from dataclasses import dataclass
from enum import Enum
from pathlib import Path

from .backends.base import GeneratedFile
from .errors import EvCliError


class Strategy(Enum):
    """When an existing file may be replaced."""

    #: Replace it, unless it is newer than the definitions it came from.
    UPDATE = 'update'
    #: Replace it regardless of modification times.
    FORCE_UPDATE = 'force-update'
    #: Only ever create; never touch a file that exists.  This is what
    #: protects the ``.cpp`` files a human owns.
    UPDATE_IF_MISSING = 'update-if-non-existent'
    #: As above, with the message ``module create`` prints.
    CREATE = 'create'
    #: Replace it, even though creating was what was asked for.
    FORCE_CREATE = 'force-create'


#: Comment syntax to ignore when diffing, so that a changed timestamp or
#: license comment does not show up as a change.
_DIFF_IGNORE = {
    '.hpp': '^//.*',
    '.cpp': '^//.*',
}
_DIFF_IGNORE_BY_NAME = {
    'CMakeLists.txt': '^#.*',
}

_LICENSE_HEADER_TERMINATORS = ('#ifndef', '#pragma once', '#include')


@dataclass
class FileWriter:
    """Applies the strategies, or renders a diff instead."""

    diff_only: bool = False
    stream = sys.stdout

    def write(
        self,
        file: GeneratedFile,
        strategy: Strategy,
        *,
        reason: str = '',
        preserve_license_header: bool = False,
    ) -> bool:
        """Write ``file`` if ``strategy`` allows it.  Returns whether it did."""
        if self.diff_only:
            self._show_diff(file)
            return False

        method = self._method(file, strategy)
        if method is None:
            return False

        print(f'{method} file {file.printable_name}{reason}', file=self.stream)

        file.path.parent.mkdir(parents=True, exist_ok=True)
        content = file.content
        if preserve_license_header:
            content = self._keep_existing_license_header(file, content)
        file.path.write_text(content)
        return True

    def write_checking_templates(
        self, file: GeneratedFile, strategy: Strategy
    ) -> bool:
        """As :meth:`write`, but a newer template forces the write.

        Without this a template change would leave every already-generated file
        in place, because the definitions it was built from had not changed.
        """
        newer, reason = self._template_is_newer(file)
        return self.write(file, Strategy.FORCE_UPDATE if newer else strategy, reason=reason)

    # -- strategy -----------------------------------------------------------

    def _method(self, file: GeneratedFile, strategy: Strategy) -> str | None:
        exists = file.path.exists()

        if strategy is Strategy.UPDATE:
            if exists and file.path.stat().st_mtime > file.source_mtime:
                print(f'Skipping {file.printable_name} (up-to-date)', file=self.stream)
                return None
            return 'Updating'

        if strategy is Strategy.FORCE_UPDATE:
            return 'Force-updating' if exists else 'Creating'

        if strategy is Strategy.FORCE_CREATE:
            return 'Overwriting' if exists else 'Creating'

        if strategy in (Strategy.CREATE, Strategy.UPDATE_IF_MISSING):
            if exists:
                print(
                    f'Skipping {file.printable_name} (use create --force to recreate)',
                    file=self.stream,
                )
                return None
            return 'Creating'

        raise EvCliError(f'unknown write strategy {strategy!r}')

    @staticmethod
    def _template_is_newer(file: GeneratedFile) -> tuple[bool, str]:
        if not file.path.exists():
            return True, ' (Generated file did not exist)'
        generated_at = file.path.stat().st_mtime
        for template in file.template_paths:
            if template.exists() and template.stat().st_mtime > generated_at:
                return True, ' (Template file has changed since last generation)'
        return False, ''

    # -- license headers ----------------------------------------------------

    def _keep_existing_license_header(self, file: GeneratedFile, content: str) -> str:
        """Leave a license header a human replaced alone.

        Modules are allowed to carry a license other than the one the manifest
        names, and regenerating must not quietly relicense them.
        """
        header = file.license_header
        if not header or not file.path.exists():
            return content

        original = file.path.read_text()
        if original.startswith(header):
            return content

        existing = ''
        for terminator in _LICENSE_HEADER_TERMINATORS:
            index = original.find(terminator)
            if index >= 0:
                existing = original[:index]
                break
        if not existing:
            return content

        print(f'Keeping the existing licence header:\n{existing}', file=self.stream)
        return content.replace(header, existing.strip())

    # -- diffing ------------------------------------------------------------

    def _show_diff(self, file: GeneratedFile) -> None:
        diff = shutil.which('diff')
        if diff is None:
            raise EvCliError('cannot show a diff: no "diff" executable found')

        ignore = _DIFF_IGNORE_BY_NAME.get(file.path.name) or _DIFF_IGNORE.get(file.path.suffix)
        ignore_args = ['-I', ignore] if ignore else []

        completed = subprocess.run(
            [
                diff, '-ruN', *ignore_args,
                '--label', str(file.printable_name),
                '--color=always',
                str(file.path), '-',
            ],
            input=file.content,
            capture_output=True,
            encoding='utf-8',
        )
        if completed.stdout:
            print(completed.stdout, file=self.stream)


def clang_format(config_dir: Path, file: GeneratedFile) -> None:
    """Format C++ in place, using the ``.clang-format`` in ``config_dir``."""
    if file.path.suffix not in ('.hpp', '.cpp'):
        return

    executable = shutil.which('clang-format')
    if executable is None:
        raise EvCliError(
            'could not find clang-format; pass --disable-clang-format to skip '
            'formatting generated sources'
        )

    config_dir = Path(config_dir)
    if not config_dir.is_dir():
        raise EvCliError(f'clang-format directory {config_dir} does not exist')
    if not (config_dir / '.clang-format').exists():
        raise EvCliError(
            f'clang-format directory {config_dir} contains no .clang-format file'
        )

    completed = subprocess.run(
        [executable, '--style=file'],
        input=file.content,
        capture_output=True,
        cwd=config_dir,
        encoding='utf-8',
    )
    if completed.returncode != 0:
        raise EvCliError(f'clang-format failed:\n{completed.stderr}')
    file.content = completed.stdout
