#!/usr/bin/env python3
# -*- coding: utf-8 -*-
#
# SPDX-License-Identifier: Apache-2.0
# Copyright Pionix GmbH and Contributors to EVerest
#
"""Delete a deployed docs instance and regenerate versions.json.

Driven by .github/workflows/on_workflow_dispatch_delete.yaml after the
deployed docs repo has been cloned to a local path. Performs three
operations as one cohesive unit:

  1. Determine is_release from the instance's info.json (with legacy-prefix
     fallback for entries deployed before info.json was introduced).
  2. Remove the instance directory (gated on --allow-release for releases).
  3. Regenerate versions.json so the deletion is reflected in the version
     switcher and the multiversion landing page.

Steps 1 and 3 reuse load_instance_info / load_versions_data from
create_metadata_file.py so the legacy heuristic and sort order stay in
sync with the build path.
"""

import argparse
import json
import re
import shutil
import sys
from pathlib import Path

from create_metadata_file import load_instance_info, load_versions_data


INSTANCE_NAME_RE = re.compile(r'^[a-z0-9_]+$')


def main():
    parser = argparse.ArgumentParser(
        description='Delete a deployed docs instance and regenerate versions.json'
    )
    parser.add_argument(
        '--multiversion-root-directory',
        type=Path,
        dest='multiversion_root_dir',
        required=True,
        help='Path to the multiversion docs root (the cloned docs repo\'s "docs/" directory)',
    )
    parser.add_argument(
        '--instance-name',
        type=str,
        dest='instance_name',
        required=True,
        help='Sanitized instance directory name to delete',
    )
    parser.add_argument(
        '--allow-release',
        action='store_true',
        dest='allow_release',
        help='Permit deletion even if the instance is a release',
    )
    parser.add_argument(
        '--legacy-release-prefix',
        type=str,
        dest='legacy_release_prefix',
        default='release_',
        help='Directories with no info.json starting with this prefix are treated as releases',
    )
    args = parser.parse_args()

    if not args.multiversion_root_dir.is_absolute():
        parser.error('--multiversion-root-directory must be absolute')
    if not args.multiversion_root_dir.is_dir():
        parser.error(f'{args.multiversion_root_dir} is not a directory')
    if not INSTANCE_NAME_RE.match(args.instance_name):
        parser.error(
            f'--instance-name must match [a-z0-9_]+ (got: {args.instance_name!r})'
        )

    instance_dir = args.multiversion_root_dir / args.instance_name
    if not instance_dir.is_dir():
        print(
            f"::error::Instance directory not found in the docs repo: {instance_dir}",
            file=sys.stderr,
        )
        sys.exit(1)

    info = load_instance_info(instance_dir, args.legacy_release_prefix)
    source = 'info.json' if (instance_dir / 'info.json').is_file() else 'legacy-prefix-heuristic'
    print(f"is_release={info['is_release']} (source: {source})")

    if info['is_release'] and not args.allow_release:
        print(
            f"::error::Instance '{args.instance_name}' is a release. "
            "Pass --allow-release to delete it.",
            file=sys.stderr,
        )
        sys.exit(1)

    shutil.rmtree(instance_dir)
    print(f"Removed {instance_dir}")

    data = load_versions_data(
        multiversion_root_dir=args.multiversion_root_dir,
        legacy_release_prefix=args.legacy_release_prefix,
    )
    versions_json = args.multiversion_root_dir / 'versions.json'
    with versions_json.open('w') as f:
        json.dump(data, f, indent=2)
    print(f"--- regenerated {versions_json} ---")
    print(json.dumps(data, indent=2))


if __name__ == '__main__':
    try:
        main()
    except Exception as e:
        print(f"Error: {e}", file=sys.stderr)
        sys.exit(1)
