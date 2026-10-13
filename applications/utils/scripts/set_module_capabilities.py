#!/usr/bin/env python3
# -*- coding: utf-8 -*-
#
# SPDX-License-Identifier: Apache-2.0
# Copyright Pionix GmbH and Contributors to EVerest
#
"""
Parse the manifest.yaml of every installed EVerest module and grant the Linux capabilities
listed there as file capabilities on the module binary using setcap
"""
import argparse
import re
import subprocess
import sys

from pathlib import Path

import yaml

CAPABILITY_PATTERN = re.compile(r'^CAP_[A-Z_]+$')


def get_capabilities(manifest_path: Path) -> list:
    manifest = yaml.safe_load(manifest_path.read_text()) or {}
    capabilities = manifest.get('capabilities') or []
    for capability in capabilities:
        if not isinstance(capability, str) or not CAPABILITY_PATTERN.match(capability):
            raise ValueError(f'Invalid capability "{capability}" in {manifest_path}')
    return capabilities


def set_capabilities(module_dir: Path, capabilities: list, setcap: str, dry_run: bool) -> bool:
    binary = module_dir / module_dir.name
    if not binary.is_file():
        # file capabilities on an interpreter would apply to every script it runs
        print(f'WARNING: {module_dir.name} requires {", ".join(capabilities)} but has no module binary '
              f'({binary}), skipping', file=sys.stderr)
        return True

    capability_text = ','.join(capability.lower() for capability in capabilities) + '+ep'
    command = [setcap, capability_text, str(binary)]
    print(' '.join(command), flush=True)
    if dry_run:
        return True

    result = subprocess.run(command, check=False)
    if result.returncode != 0:
        print(f'ERROR: setcap failed for {binary} with exit code {result.returncode}', file=sys.stderr)
        return False
    return True


def main() -> int:
    parser = argparse.ArgumentParser(
        description='set file capabilities on EVerest module binaries as declared in their manifest.yaml')
    parser.add_argument('--modules-dir', type=Path, required=True,
                        help='Directory containing the installed modules, e.g. <prefix>/libexec/everest/modules')
    parser.add_argument('--setcap', default='setcap',
                        help='setcap executable to use (default: %(default)s)')
    parser.add_argument('--dry-run', action='store_true',
                        help='Only print the setcap commands')
    args = parser.parse_args()

    if not args.modules_dir.is_dir():
        print(f'ERROR: modules directory {args.modules_dir} does not exist', file=sys.stderr)
        return 1

    success = True
    for manifest_path in sorted(args.modules_dir.glob('*/manifest.yaml')):
        try:
            capabilities = get_capabilities(manifest_path)
        except (yaml.YAMLError, ValueError) as err:
            print(f'ERROR: {err}', file=sys.stderr)
            success = False
            continue

        if capabilities and not set_capabilities(manifest_path.parent, capabilities, args.setcap, args.dry_run):
            success = False

    return 0 if success else 1


if __name__ == '__main__':
    sys.exit(main())
