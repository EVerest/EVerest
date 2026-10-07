#!/usr/bin/env python3
# -*- coding: utf-8 -*-
#
# SPDX-License-Identifier: Apache-2.0
# Copyright Pionix GmbH and Contributors to EVerest
#
"""
author: kai-uwe.hermann@pionix.de
Replace licenses with Apache 2.0
"""

import argparse
import fnmatch
import re
import subprocess
from pathlib import Path

PIONIX_NOTICE = 'Copyright Pionix GmbH and Contributors to EVerest'

PIONIX_NOTICE_WITH_YEARS = re.compile(
    r'^(?P<prefix>\s*(?://|##?|\*)\s*)Copyright\s+(?:\(C\)\s+)?'
    r'(?:[0-9]{3,4}|\{\{\s*year\s*\}\})(?:\s*-\s*(?:[0-9]{4}|\{\{\s*year\s*\}\})?)?\s+'
    r'Pionix GmbH and Contributors to EVerest',
    re.IGNORECASE)

HEADER_LINES = 20

# Third-party and generated content keeps its original notices.
STRIP_YEARS_EXCLUDES = [
    'applications/cbv2g_json_wrapper/third_party/*',
    'lib/3rd_party/*',
    'lib/everest/*/3rd_party/*',
    'lib/everest/cbv2g/*',
    'lib/everest/ocpp/tests/lib/ocpp/v2/json/external/*',
    'third-party/*',
    'yocto/*.patch',
    'docs/source/_ext/staticpages/*',
    '*/grpc_libs/generated/*',
    'modules/EVSE/Auth/tests/stubs/generated/*',
]


def strip_years(working_dir: Path) -> int:
    """Remove the years from Pionix copyright notices in all files tracked by git."""
    tracked = subprocess.run(['git', 'ls-files', '-z'], cwd=working_dir, check=True,
                             capture_output=True).stdout.decode().split('\0')
    changed = 0
    for name in filter(None, tracked):
        if any(fnmatch.fnmatch(name, pattern) for pattern in STRIP_YEARS_EXCLUDES):
            continue
        file = working_dir / name
        if file.is_symlink() or not file.is_file():
            continue
        try:
            lines = file.read_bytes().decode('utf-8').splitlines(keepends=True)
        except UnicodeDecodeError:
            continue
        modified = False
        for i, line in enumerate(lines[:HEADER_LINES]):
            new_line = PIONIX_NOTICE_WITH_YEARS.sub(lambda m: m.group('prefix') + PIONIX_NOTICE, line, count=1)
            if new_line != line:
                lines[i] = new_line
                modified = True
        if modified:
            file.write_bytes(''.join(lines).encode('utf-8'))
            changed += 1
    print(f'Removed years from the Pionix copyright notice in {changed} files')
    return changed


def main():
    parser = argparse.ArgumentParser(
        description='replaces licenses with Apache 2.0')

    parser.add_argument('--working-dir', '-wd', type=str,
                        help='Working directory (default: .)', default=str(Path.cwd()))
    parser.add_argument('--no-year', action='store_true',
                        help='Deprecated, has no effect: the license header never includes years')
    parser.add_argument('--strip-years', action='store_true',
                        help='Only remove the years from existing Pionix copyright notices in all tracked files, '
                        'leaving every other header line untouched')

    args = parser.parse_args()

    working_dir = Path(args.working_dir).expanduser().resolve()

    if args.strip_years:
        strip_years(working_dir)
        return

    files = [file for file in working_dir.rglob('*') if file.suffix in ['.cpp', '.hpp']]

    license_text = f"""// SPDX-License-Identifier: Apache-2.0
// {PIONIX_NOTICE}
"""

    success = 0
    failure = 0
    count = len(files)

    for file in files:
        content = file.read_text()
        if content.startswith('/*'):
            needle = '*/\n'
            end = content.find(needle) + len(needle)
            new_content = license_text + content[end:]
            file.write_text(new_content)
            print(f'Modified {file} with new license header')
            success += 1
        else:
            content_lines = content.splitlines()
            end = 0
            for line in content_lines:
                if line.startswith('//'):
                    end += 1
                else:
                    break
            new_content = license_text + '\n'.join(content_lines[end:]) + '\n'
            file.write_text(new_content)
            success += 1
    
    if success != count:
        print('ERROR during license replacement')
    else:
        print('Everything went well')

    manifest_files = [file for file in working_dir.rglob('*') if file.name == 'manifest.yaml']
    for file in manifest_files:
        manifest = file.read_text()
        needle = 'license:'
        start = manifest.find(needle)
        end = manifest.find('\n', start+len(needle))
        new_manifest = manifest[:start] + 'license: https://opensource.org/licenses/Apache-2.0' + manifest[end:]
        file.write_text(new_manifest)


if __name__ == '__main__':
    main()
