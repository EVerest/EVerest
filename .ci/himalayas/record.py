#
# SPDX-License-Identifier: Apache-2.0
# Copyright Pionix GmbH and Contributors to EVerest
"""Build a Himalayas CTRF record, its path and its artifact name.

The record follows section 3.1 of EVerest/Himalayas CONTRIBUTING.md.
"""

import datetime
import os
import re

SHA_PATTERN = re.compile(r"[0-9a-f]{40}")
POINTER_PATTERN = re.compile(r"testing/(main|stable-[0-9]{4}\.[0-9]{2})")
MODULE_PATTERN = re.compile(r"[A-Z][A-Za-z0-9_]{1,49}")
TRIPLE_PATTERN = re.compile(r"[a-z0-9_]+-[a-z0-9]+-[a-z0-9]+")
TRIPLE_LIMIT = 100
TOOLCHAIN_LIMIT = 60
TOOLCHAIN_PATTERN = re.compile(r"(gcc|clang|msvc|icc) [0-9]+(\.[0-9]+){0,2}")
BUILD_STATUSES = ("passed", "failed")
TARGET_STATUSES = ("passed", "failed", "skipped")
TARGET_KEYS = {"target", "status", "toolchain"}


def _check_sha(sha: str) -> None:
    if not isinstance(sha, str) or not SHA_PATTERN.fullmatch(sha):
        raise ValueError(f"not a full lowercase commit SHA: {sha!r}")


def _check_pointer(pointer: str) -> str:
    match = isinstance(pointer, str) and POINTER_PATTERN.fullmatch(pointer)
    if not match:
        raise ValueError(f"pointer must be testing/main or testing/stable-YYYY.MM, got {pointer!r}")
    return match.group(1).replace("-", "/")


def _utc_date(moved_at: str) -> datetime.date:
    return datetime.datetime.strptime(moved_at, "%Y-%m-%dT%H:%M:%SZ").date()


def modules_from_dist(dist_dir: str) -> list[str]:
    modules_dir = os.path.join(dist_dir, "libexec", "everest", "modules")
    try:
        entries = os.listdir(modules_dir)
    except FileNotFoundError:
        entries = []
    modules = sorted(entry for entry in entries if os.path.isdir(os.path.join(modules_dir, entry)))
    if not modules:
        raise ValueError(f"no modules under {modules_dir}")
    return modules


def artifact_name(pointer: str, sha: str) -> str:
    _check_pointer(pointer)
    _check_sha(sha)
    return f"himalayas-record_{pointer.replace('/', '-')}_{sha}"


def find_move(pointers_json: dict, pointer: str, sha: str) -> tuple[str, int]:
    log = pointers_json["log"]
    for index in range(len(log) - 1, -1, -1):
        entry = log[index]
        if entry["pointer"] == pointer and entry["sha"] == sha:
            date = _utc_date(entry["moved_at"])
            earlier_moves = sum(
                1 for move in log[:index] if move["pointer"] == pointer and _utc_date(move["moved_at"]) == date
            )
            return entry["moved_at"], earlier_moves + 1
    raise LookupError(f"{sha} is not a move of {pointer}")


def record_path(pointer: str, moved_at: str, n: int = 1) -> str:
    _check_pointer(pointer)
    if n < 1:
        raise ValueError(f"n must be at least 1, got {n}")
    day = _utc_date(moved_at).strftime("%Y%m%d")
    return f"{pointer}/{day}.json" if n == 1 else f"{pointer}/{day}-{n}.json"


def _check_targets(targets: list) -> None:
    for target in targets:
        if not isinstance(target, dict) or set(target) != TARGET_KEYS:
            raise ValueError(f"a target needs exactly {sorted(TARGET_KEYS)}: {target!r}")
        triple = target["target"]
        if not isinstance(triple, str) or len(triple) > TRIPLE_LIMIT or not TRIPLE_PATTERN.fullmatch(triple):
            raise ValueError(f"target must be an <arch>-<os>-<abi> triple of at most {TRIPLE_LIMIT}: {target!r}")
        if target["status"] not in TARGET_STATUSES:
            raise ValueError(f"target status must be one of {TARGET_STATUSES}: {target!r}")
        toolchain = target["toolchain"]
        if (
            not isinstance(toolchain, str)
            or len(toolchain) > TOOLCHAIN_LIMIT
            or not TOOLCHAIN_PATTERN.fullmatch(toolchain)
        ):
            raise ValueError(f"toolchain must be '<compiler> <version>' of at most {TOOLCHAIN_LIMIT}: {target!r}")


def build_record(*, pointer, commit, start_ms, stop_ms, build_url, build_status, modules, targets, tests) -> dict:
    branch_name = _check_pointer(pointer)
    if build_status not in BUILD_STATUSES:
        raise ValueError(f"build_status must be one of {BUILD_STATUSES}, got {build_status!r}")
    if build_status == "failed" and (modules or tests):
        raise ValueError("a failed build carries no modules and no tests")
    if build_status == "passed" and not modules:
        raise ValueError("a passed build needs its modules")
    for module in modules:
        if not isinstance(module, str) or not MODULE_PATTERN.fullmatch(module):
            raise ValueError(f"not a module name: {module!r}")
    if len(set(modules)) != len(modules):
        raise ValueError(f"duplicate module names: {modules!r}")
    for ms in (start_ms, stop_ms):
        if not isinstance(ms, int) or isinstance(ms, bool):
            raise ValueError(f"start_ms and stop_ms must be int, got {ms!r}")
    if not 1 <= start_ms <= stop_ms:
        raise ValueError(f"need 1 <= start_ms <= stop_ms, got {start_ms} and {stop_ms}")
    if not isinstance(build_url, str) or not build_url.startswith("https://"):
        raise ValueError(f"build_url must be an https URL, got {build_url!r}")
    _check_sha(commit)
    _check_targets(targets)

    build = {"status": build_status}
    if build_status == "passed":
        build["modules"] = list(modules)
    if targets:
        build["targets"] = list(targets)

    statuses = [test["status"] for test in tests]
    return {
        "reportFormat": "CTRF",
        "specVersion": "1.0.0",
        "results": {
            "tool": {"name": "everest-ci", "version": "1"},
            "summary": {
                "tests": len(tests),
                "passed": statuses.count("passed"),
                "failed": statuses.count("failed"),
                "skipped": statuses.count("skipped"),
                "pending": statuses.count("pending"),
                "other": statuses.count("other"),
                "start": start_ms,
                "stop": stop_ms,
            },
            "environment": {
                "appName": "EVerest",
                "appVersion": pointer,
                "commit": commit,
                "branchName": branch_name,
                "testEnvironment": "SIL",
                "osPlatform": "linux",
                "healthy": build_status == "passed",
                "buildUrl": build_url,
            },
            "extra": {
                "build": build,
                "integrator": {"id": "everest"},
            },
            "tests": list(tests),
        },
    }
