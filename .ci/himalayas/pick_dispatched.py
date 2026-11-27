#!/usr/bin/env python3
#
# SPDX-License-Identifier: Apache-2.0
# Copyright Pionix GmbH and Contributors to EVerest

import argparse
import datetime
import json
import sys


def pick_dispatched(runs_json: dict, sha: str, after: str) -> dict | None:
    after_time = datetime.datetime.fromisoformat(after)
    runs = [
        run
        for run in runs_json["workflow_runs"]
        if run["event"] == "workflow_dispatch" and datetime.datetime.fromisoformat(run["created_at"]) >= after_time
    ]
    run = min(runs, key=lambda run: datetime.datetime.fromisoformat(run["created_at"]), default=None)
    if run is not None and run["head_sha"] != sha:
        raise ValueError(f"the run dispatched since {after} is at {run['head_sha']}, not at {sha}")
    return run


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Print the id of the oldest workflow_dispatch run in `gh api` runs JSON created at or after a time;"
        " exit 3 when there is none and 4 when it is at another SHA."
    )
    parser.add_argument("--sha", required=True)
    parser.add_argument("--after", required=True, help="ISO-8601 time the run was dispatched at")
    args = parser.parse_args()

    try:
        run = pick_dispatched(json.load(sys.stdin), args.sha, args.after)
    except ValueError as error:
        print(error, file=sys.stderr)
        return 4
    if run is None:
        print(f"no workflow_dispatch run for {args.sha} since {args.after}", file=sys.stderr)
        return 3
    print(run["id"])
    return 0


if __name__ == "__main__":
    sys.exit(main())
