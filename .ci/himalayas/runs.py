#!/usr/bin/env python3
#
# SPDX-License-Identifier: Apache-2.0
# Copyright Pionix GmbH and Contributors to EVerest
"""Pick workflow runs and build outcomes from `gh api` JSON."""

import argparse
import json
import re
import sys

NATIVE_TARGET = "x86_64-linux-gnu"
CROSS_JOB = re.compile(r"Bazel Cross \(([^)]+)\) / ")
PLATFORM_PATTERN = re.compile(r"[a-z0-9_]+-[a-z0-9]+-[a-z0-9]+")
CROSS_STATUSES = {"success": "passed", "skipped": "skipped"}


def pick_run(runs_json: dict, sha: str, event: str) -> dict | None:
    runs = [run for run in runs_json["workflow_runs"] if run["head_sha"] == sha and run["event"] == event]
    return max(runs, key=lambda run: (run.get("run_started_at") or "", run["created_at"]), default=None)


def _jobs(jobs_json: dict) -> list[dict]:
    jobs = jobs_json["jobs"]
    if jobs_json["total_count"] > len(jobs):
        raise ValueError(f"jobs page holds {len(jobs)} of {jobs_json['total_count']} jobs")
    return jobs


def _job(jobs_json: dict, prefix: str) -> dict | None:
    return next((job for job in _jobs(jobs_json) if job["name"].startswith(prefix)), None)


def _step_status(job: dict, step_name: str) -> str:
    step = next((step for step in job.get("steps", []) if step["name"] == step_name), None)
    return "passed" if step is not None and step["conclusion"] == "success" else "failed"


def build_outcome(jobs_json: dict) -> str:
    job = _job(jobs_json, "CMake GCC /")
    return "failed" if job is None else _step_status(job, "Compile")


def native_targets(jobs_json: dict, toolchain: str) -> list[dict]:
    job = _job(jobs_json, "CMake GCC /")
    if job is None:
        return []
    return [{"target": NATIVE_TARGET, "status": _step_status(job, "Compile"), "toolchain": toolchain}]


def cross_targets(jobs_json: dict, toolchain: str) -> list[dict]:
    targets = []
    for job in _jobs(jobs_json):
        match = CROSS_JOB.match(job["name"])
        if not match:
            continue
        platform = match.group(1)
        if not PLATFORM_PATTERN.fullmatch(platform):
            raise ValueError(f"cross job platform is not an <arch>-<os>-<abi> triple: {platform!r}")
        status = CROSS_STATUSES.get(job["conclusion"], "failed")
        targets.append({"target": platform, "status": status, "toolchain": toolchain})
    return targets


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="command", required=True)
    pick = commands.add_parser("pick-run", help="print the id of the newest run of a SHA and event")
    pick.add_argument("--sha", required=True)
    pick.add_argument("--event", required=True)
    commands.add_parser("build-outcome", help="print passed or failed")
    targets = commands.add_parser("targets", help="print the build targets as a JSON list")
    targets.add_argument("--toolchain", required=True, help='compiler and version, such as "gcc 13.2.0"')
    targets.add_argument("--cross-jobs", metavar="FILE", help="jobs JSON of the run holding the cross builds")
    args = parser.parse_args()

    data = json.load(sys.stdin)
    if args.command == "pick-run":
        run = pick_run(data, args.sha, args.event)
        if run is None:
            print(f"no {args.event} run for {args.sha}", file=sys.stderr)
            return 3
        print(run["id"])
    elif args.command == "build-outcome":
        print(build_outcome(data))
    else:
        result = native_targets(data, args.toolchain)
        if args.cross_jobs:
            with open(args.cross_jobs, encoding="utf-8") as f:
                result += cross_targets(json.load(f), args.toolchain)
        print(json.dumps(result))
    return 0


if __name__ == "__main__":
    sys.exit(main())
