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
    ) -> None:
        """Write ``file`` if ``strategy`` allows it."""
        if self.diff_only:
            self._show_diff(file)
            return

        action = self._choose_action(file, strategy)
        if action is None:
            return

        print(f'{action} file {file.printable_name}{reason}', file=self.stream)

        file.path.parent.mkdir(parents=True, exist_ok=True)
        content = file.content
        if preserve_license_header:
            content = self._keep_existing_license_header(file, content)
        file.path.write_text(content)

    def write_checking_templates(self, file: GeneratedFile, strategy: Strategy) -> None:
        """As :meth:`write`, but a newer template forces the write.

        Without this a template change would leave every already-generated file
        in place, because the definitions it was built from had not changed.
        """
        reason = self._reason_to_force(file)
        if reason is None:
            self.write(file, strategy)
        else:
            self.write(file, Strategy.FORCE_UPDATE, reason=reason)

    def _choose_action(self, file: GeneratedFile, strategy: Strategy) -> str | None:
        """How the write is logged, or ``None`` once the skip has been reported."""
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

        if exists:
            print(
                f'Skipping {file.printable_name} (use create --force to recreate)',
                file=self.stream,
            )
            return None
        return 'Creating'

    @staticmethod
    def _reason_to_force(file: GeneratedFile) -> str | None:
        """Why ``file`` has to be written whatever the strategy says, if it has to."""
        if not file.path.exists():
            return ' (Generated file did not exist)'
        generated_at = file.path.stat().st_mtime
        for template in file.template_paths:
            if template.exists() and template.stat().st_mtime > generated_at:
                return ' (Template file has changed since last generation)'
        return None

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

        print(f'Keeping the existing license header:\n{existing}', file=self.stream)
        return content.replace(header, existing.strip())

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
            check=False,
        )
        if completed.stdout:
            print(completed.stdout, file=self.stream)
