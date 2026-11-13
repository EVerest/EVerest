#!/usr/bin/env python3
#
# SPDX-License-Identifier: Apache-2.0
# Copyright Pionix GmbH and Contributors to EVerest
"""Write the Himalayas record of one pointer move and print its artifact name."""

import argparse
import json
import os
import sys
import xml.etree.ElementTree as ET

# -I leaves the script's own directory off the path, and the library lives beside it.
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from junit_to_ctrf import convert
from record import artifact_name, build_record, find_move, modules_from_dist, record_path

SUITES = ("unit", "integration", "ocpp")


def _junit(value: str) -> tuple[str, str]:
    suite, separator, path = value.partition("=")
    if not separator or suite not in SUITES or not path:
        raise argparse.ArgumentTypeError(f"expected <{'|'.join(SUITES)}>=FILE, got {value!r}")
    return suite, path


def _tests(junits: list[tuple[str, str]]) -> list[dict]:
    tests = []
    for suite, path in junits:
        if not os.path.exists(path):
            print(f"::warning::no {suite} JUnit report at {path}, its tests are left out")
            continue
        tests += convert(path, suite)
    return tests


def collect(args: argparse.Namespace) -> str:
    with open(args.pointers_json, encoding="utf-8") as f:
        moved_at, move_number = find_move(json.load(f), args.pointer, args.sha)
    with open(args.targets, encoding="utf-8") as f:
        targets = json.load(f)
    if args.build_status == "passed":
        if args.dist is None:
            raise ValueError("a passed build needs --dist")
        modules = modules_from_dist(args.dist)
        tests = _tests(args.junit)
    else:
        modules = []
        tests = []
    record = build_record(
        pointer=args.pointer,
        commit=args.sha,
        start_ms=args.start_ms,
        stop_ms=args.stop_ms,
        build_url=args.build_url,
        build_status=args.build_status,
        modules=modules,
        targets=targets,
        tests=tests,
    )
    path = os.path.join(args.out, record_path(args.pointer, moved_at, move_number))
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w", encoding="utf-8") as f:
        json.dump(record, f, indent=2)
        f.write("\n")
    return artifact_name(args.pointer, args.sha)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--pointer", required=True)
    parser.add_argument("--pointers-json", required=True, metavar="FILE")
    parser.add_argument("--sha", required=True)
    parser.add_argument("--build-url", required=True)
    parser.add_argument("--build-status", required=True)
    parser.add_argument("--targets", required=True, metavar="FILE", help="build targets as a JSON list")
    parser.add_argument(
        "--junit", type=_junit, action="append", default=[], metavar="SUITE=FILE", help="a JUnit report, skipped if absent"
    )
    parser.add_argument("--dist", metavar="DIR", help="the installed prefix, required for a passed build")
    parser.add_argument("--start-ms", required=True, type=int)
    parser.add_argument("--stop-ms", required=True, type=int)
    parser.add_argument("--out", required=True, metavar="DIR")
    args = parser.parse_args()

    try:
        name = collect(args)
    except (ValueError, LookupError, ET.ParseError) as error:
        print(f"collect.py: {error}", file=sys.stderr)
        return 2
    print(f"artifact={name}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
