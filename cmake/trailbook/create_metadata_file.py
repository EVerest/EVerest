#!/usr/bin/env python3
# -*- coding: utf-8 -*-
#
# SPDX-License-Identifier: Apache-2.0
# Copyright Pionix GmbH and Contributors to EVerest
#
"""
author: andreas.heinrich@pionix.de
This script creates a metadata_everest.json file
based on the versions found in the multiversion root directory.

Each instance directory may contain an info.json with at least:
    {
        "name": "<sanitized instance name, matches directory>",
        "display": "<human-readable label>",
        "is_release": true|false
    }
If info.json is missing or unreadable (LEGACY entries, deployed before
info.json was introduced):
  - name and display fall back to the directory name
  - is_release is inferred from the configured legacy prefix
    (--legacy-release-prefix, default 'old-documentation-2025')

Legacy entries get rewritten with a proper info.json the next time their
instance is rebuilt, so this fallback is only relevant during migration.
"""


import argparse
import json
from pathlib import Path


def load_instance_info(instance_dir: Path, legacy_release_prefix: str) -> dict:
    """Read info.json from an instance directory, falling back to dir name.

    For legacy entries (no/unreadable info.json), is_release is inferred from
    `legacy_release_prefix` so legacy `old-documentation-2025*` dirs still sort with the
    other releases in the version switcher.
    """
    info_file = instance_dir / 'info.json'
    if info_file.is_file():
        try:
            with info_file.open('r') as f:
                info = json.load(f)
            info.setdefault('name', instance_dir.name)
            info.setdefault('display', info['name'])
            info.setdefault('is_release', False)
            return info
        except (json.JSONDecodeError, OSError) as e:
            print(f"\033[33mWarning: failed to read {info_file}: {e}; falling back to directory name\033[0m")
    return {
        'name': instance_dir.name,
        'display': instance_dir.name,
        'is_release': (
            bool(legacy_release_prefix)
            and instance_dir.name.startswith(legacy_release_prefix)
        ),
    }


def load_versions_data(
    multiversion_root_dir: Path,
    current_instance_info_path: Path = None,
    legacy_release_prefix: str = 'old-documentation-2025',
) -> dict:
    """Aggregate per-instance metadata into the versions.json payload.

    Returns {'versions': [...]} with releases sorted newest-first by name,
    then non-releases ascending. If ``current_instance_info_path`` is given,
    that entry takes precedence over any same-named deployed copy.

    Returns {'versions': []} if no instances are found; callers decide
    whether that is an error (it is during a build; it isn't for the delete
    flow when the last instance gets removed).
    """
    versions_by_name = {}
    if multiversion_root_dir.is_dir():
        for instance_dir in multiversion_root_dir.iterdir():
            if not instance_dir.is_dir():
                continue
            if not (instance_dir / 'index.html').is_file():
                continue
            info = load_instance_info(instance_dir, legacy_release_prefix)
            versions_by_name[info['name']] = info

    if current_instance_info_path is not None:
        with current_instance_info_path.open('r') as f:
            current_info = json.load(f)
        current_info.setdefault('display', current_info.get('name', ''))
        current_info.setdefault('is_release', False)
        # Current build's info wins over a stale deployed copy with the same name.
        versions_by_name[current_info['name']] = current_info

    # Releases first (newest-named on top), then non-releases.
    releases = sorted(
        (v for v in versions_by_name.values() if v.get('is_release')),
        key=lambda v: v['name'],
        reverse=True,
    )
    non_releases = sorted(
        (v for v in versions_by_name.values() if not v.get('is_release')),
        key=lambda v: v['name'],
    )
    return {'versions': releases + non_releases}


def main():
    parser = argparse.ArgumentParser(description='Creates metadata json file')

    parser.add_argument(
        '--multiversion-root-directory',
        type=Path,
        dest='multiversion_root_dir',
        action='store',
        required=True,
        help='Path to the root directory of the multiversion documentation'
    )
    parser.add_argument(
        '--json-output-path',
        type=Path,
        dest='json_output_path',
        action='store',
        required=True,
        help='Path where the trailbook_metadata.json file will be created'
    )
    parser.add_argument(
        '--current-instance-info',
        type=Path,
        dest='current_instance_info',
        action='store',
        default=None,
        help='Path to info.json for the current instance being built '
             '(its directory may not yet exist in the multiversion root)'
    )
    parser.add_argument(
        '--legacy-release-prefix',
        type=str,
        dest='legacy_release_prefix',
        default='old-documentation-',
        help='For instance directories with no info.json, treat them as '
             'releases if their directory name starts with this prefix. '
    )
    args = parser.parse_args()

    if not args.multiversion_root_dir.is_absolute():
        raise ValueError("Multiversion root directory must be absolute")
    if not args.multiversion_root_dir.is_dir():
        print(f"\033[33mWarning: {args.multiversion_root_dir} does not exist or is not a directory, it is treated as an empty multiversion root dir\033[0m")
    if not args.json_output_path.is_absolute():
        raise ValueError("JSON output path must be absolute")
    if args.json_output_path.exists():
        raise FileExistsError("JSON output path already exists")

    data = load_versions_data(
        multiversion_root_dir=args.multiversion_root_dir,
        current_instance_info_path=args.current_instance_info,
        legacy_release_prefix=args.legacy_release_prefix,
    )

    if not data['versions']:
        raise ValueError("No versions found in the specified multiversion root directory")

    with args.json_output_path.open('w') as f:
        json.dump(data, f, indent=2)


if __name__ == "__main__":
    try:
        main()
    except Exception as e:
        print(f"Error: {e}")
        exit(1)
