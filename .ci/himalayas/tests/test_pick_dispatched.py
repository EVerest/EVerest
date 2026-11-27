#
# SPDX-License-Identifier: Apache-2.0
# Copyright Pionix GmbH and Contributors to EVerest
import json
import os
import subprocess
import sys
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
PICK_DISPATCHED_PY = os.path.join(os.path.dirname(HERE), "pick_dispatched.py")
SHA = "0bc286811e1279adc4711b6c5e6d9d141660277f"
OTHER_SHA = "f" * 40
AFTER = "2026-10-29T03:20:00Z"


def dispatched(run_id, sha, created_at, event="workflow_dispatch"):
    return {"id": run_id, "head_sha": sha, "event": event, "status": "completed", "created_at": created_at}


def pick(runs, after=AFTER):
    return subprocess.run(
        [sys.executable, "-I", PICK_DISPATCHED_PY, "--sha", SHA, "--after", after],
        input=json.dumps({"workflow_runs": runs}),
        capture_output=True,
        text=True,
        check=False,
    )


class PickDispatchedTest(unittest.TestCase):
    def assert_picked(self, result, run_id):
        self.assertEqual((result.returncode, result.stdout), (0, f"{run_id}\n"), result.stderr)

    def assert_none(self, result):
        self.assertEqual((result.returncode, result.stdout), (3, ""))

    def test_oldest_run_at_or_after_the_dispatch(self):
        runs = [
            dispatched(3, SHA, "2026-10-29T03:40:00Z"),
            dispatched(2, SHA, "2026-10-29T03:20:00Z"),
            dispatched(1, SHA, "2026-10-29T03:19:59Z"),
        ]
        self.assert_picked(pick(runs), 2)

    def test_run_at_another_sha_exits_4_naming_both(self):
        result = pick([dispatched(1, OTHER_SHA, "2026-10-29T03:21:00Z")])
        self.assertEqual((result.returncode, result.stdout), (4, ""))
        self.assertIn(OTHER_SHA, result.stderr)
        self.assertIn(SHA, result.stderr)

    def test_oldest_run_decides_whatever_its_sha(self):
        runs = [dispatched(2, SHA, "2026-10-29T03:22:00Z"), dispatched(1, OTHER_SHA, "2026-10-29T03:21:00Z")]
        self.assertEqual(pick(runs).returncode, 4)

    def test_other_sha_before_after_is_ignored(self):
        runs = [dispatched(2, SHA, "2026-10-29T03:22:00Z"), dispatched(1, OTHER_SHA, "2026-10-29T03:19:00Z")]
        self.assert_picked(pick(runs), 2)

    def test_run_created_before_after_exits_3(self):
        self.assert_none(pick([dispatched(1, SHA, "2026-10-29T03:19:59Z")]))

    def test_other_events_exit_3(self):
        self.assert_none(pick([dispatched(1, SHA, "2026-10-29T03:21:00Z", event="schedule")]))

    def test_after_with_offset(self):
        runs = [dispatched(1, SHA, "2026-10-29T03:21:00Z")]
        self.assert_picked(pick(runs, "2026-10-29T05:20:00+02:00"), 1)
        self.assert_none(pick(runs, "2026-10-29T05:22:00+02:00"))


if __name__ == "__main__":
    unittest.main()
