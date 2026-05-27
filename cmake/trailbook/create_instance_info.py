#!/usr/bin/env python3
# -*- coding: utf-8 -*-
#
# SPDX-License-Identifier: Apache-2.0
# Copyright Pionix GmbH and Contributors to EVerest
#
"""
author: christoph.burandt@pionix.de
This script creates a json file with th current instance info.

The format is:
    {
        "name": "<sanitized instance name, matches directory>",
        "display": "<human-readable label>",
        "is_release": true|false
    }
"""


import argparse
import json
from pathlib import Path


def main():
    parser = argparse.ArgumentParser(description='Creates instance info json file')

    parser.add_argument(
        '--json-output-path',
        type=Path,
        dest='json_output_path',
        action='store',
        required=True,
        help='Path where the trailbook_metadata.json file will be created'
    )
    parser.add_argument(
        '--instance-name',
        type=str,
        dest='instance_name',
        action='store',
        required=True,
        help='Directory name for the instance'
    )
    parser.add_argument(
        '--display-name',
        type=str,
        dest='display_name',
        action='store',
        required=True,
        help='Display name for the instance'
    )
    parser.add_argument(
        '--is-release',
        type=bool,
        dest='is_release',
        required=True,
        help='If this instance is a release'
    )
    args = parser.parse_args()

    if not args.json_output_path.is_absolute():
        raise ValueError("JSON output path must be absolute")
    if args.json_output_path.exists():
        raise FileExistsError("JSON output path already exists")

    data = {'name': args.instance_name, 'display': args.display_name, 'is_release': args.is_release}

    with args.json_output_path.open('w') as f:
        json.dump(data, f, indent=2)


if __name__ == "__main__":
    try:
        main()
    except Exception as e:
        print(f"Error: {e}")
        exit(1)
