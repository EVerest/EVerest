#!/usr/bin/env python3
# -*- coding: utf-8 -*-
#
# SPDX-License-Identifier: Apache-2.0
# Copyright Pionix GmbH and Contributors to EVerest
#
"""
Converts a trailbook metadata YAML file to a versions JSON file.
"""

import argparse
import json
from pathlib import Path
import yaml


def main():
    parser = argparse.ArgumentParser(description='Converts trailbook metadata YAML to versions JSON')
    parser.add_argument(
        '--metadata-yaml-path',
        type=Path,
        dest='metadata_yaml_path',
        required=True,
        help='Path to the trailbook metadata YAML file'
    )
    parser.add_argument(
        '--output-path',
        type=Path,
        dest='output_path',
        required=True,
        help='Path where the versions JSON file will be created'
    )
    args = parser.parse_args()

    if not args.metadata_yaml_path.is_absolute():
        raise ValueError("metadata-yaml-path must be an absolute path")
    if not args.metadata_yaml_path.is_file():
        raise FileNotFoundError(f"metadata YAML file not found: {args.metadata_yaml_path}")
    if not args.output_path.is_absolute():
        raise ValueError("output-path must be an absolute path")
    if args.output_path.exists():
        raise FileExistsError(f"output path already exists: {args.output_path}")

    with args.metadata_yaml_path.open('r') as f:
        data = yaml.safe_load(f)

    with args.output_path.open('w') as f:
        json.dump(data, f, indent=2)


if __name__ == "__main__":
    try:
        main()
    except Exception as e:
        print(f"Error: {e}")
        exit(1)

