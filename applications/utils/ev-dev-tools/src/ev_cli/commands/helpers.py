# SPDX-License-Identifier: Apache-2.0
# Copyright Pionix GmbH and Contributors to EVerest
"""``ev-cli helpers ...`` -- small conveniences, unrelated to code generation."""

from __future__ import annotations

import json
from pathlib import Path
from uuid import uuid4

import yaml

from ..errors import EvCliError


def generate_uuids(args) -> int:
    """Print fresh uuids, for pasting into an ``ev@<uuid>`` block marker."""
    if args.count <= 0:
        raise EvCliError(f'cannot generate {args.count} uuids')
    for _ in range(args.count):
        print(uuid4())
    return 0


def yaml2json(args) -> int:
    source = Path(args.input).resolve()
    if not source.exists():
        raise EvCliError(f'the input file "{source}" does not exist')

    content = yaml.safe_load(source.read_text())
    Path(args.output).resolve().write_text(json.dumps(content, indent=2))
    return 0


def json2yaml(args) -> int:
    source = Path(args.input).resolve()
    if not source.exists():
        raise EvCliError(f'the input file "{source}" does not exist')

    content = json.loads(source.read_text())
    Path(args.output).resolve().write_text(
        yaml.safe_dump(content, indent=2, sort_keys=False, width=120)
    )
    return 0
