#
# SPDX-License-Identifier: Apache-2.0
# Copyright Pionix GmbH and Contributors to EVerest
import json
import os
import subprocess
import sys
import unittest

from runs import build_outcome, cross_targets, native_targets, pick_run

HERE = os.path.dirname(os.path.abspath(__file__))
TESTDATA = os.path.join(HERE, "testdata")
SHA = "0bc286811e1279adc4711b6c5e6d9d141660277f"
RERUN_SHA = "bddf6860ff5e2aa1817fb546c00f59948ae66add"
TOOLCHAIN = "gcc 13.2.0"


def load(name):
    with open(os.path.join(TESTDATA, name), encoding="utf-8") as f:
        return json.load(f)


def without_jobs(jobs_json, prefix):
    jobs = [job for job in jobs_json["jobs"] if not job["name"].startswith(prefix)]
    return {"total_count": len(jobs), "jobs": jobs}


def cross_jobs(*jobs):
    return {"total_count": len(jobs), "jobs": list(jobs)}


def with_compile(conclusion):
    jobs_json = load("jobs-main-push.json")
    for job in jobs_json["jobs"]:
        if job["name"].startswith("CMake GCC /"):
            for step in job["steps"]:
                if step["name"] == "Compile":
                    step["conclusion"] = conclusion
    return jobs_json


class PickRunTest(unittest.TestCase):
    def test_picks_run_by_event(self):
        runs = load("runs-0bc2868.json")
        self.assertEqual(pick_run(runs, SHA, "push")["id"], 40690172036)
        self.assertEqual(pick_run(runs, SHA, "merge_group")["id"], 40679629465)

    def test_none_for_unknown_sha_or_event(self):
        runs = load("runs-0bc2868.json")
        self.assertIsNone(pick_run(runs, "f" * 40, "push"))
        self.assertIsNone(pick_run(runs, SHA, "pull_request"))
        self.assertIsNone(pick_run({"workflow_runs": []}, SHA, "push"))

    def test_newer_first_attempt_beats_older_rerun(self):
        self.assertEqual(pick_run(load("runs-rerun.json"), RERUN_SHA, "pull_request")["id"], 40500000001)

    def test_latest_started_beats_later_created(self):
        runs = {
            "workflow_runs": [
                {
                    "id": 1,
                    "head_sha": SHA,
                    "event": "push",
                    "run_attempt": 1,
                    "run_started_at": "2026-10-28T15:00:00Z",
                    "created_at": "2026-10-28T12:00:00Z",
                },
                {
                    "id": 2,
                    "head_sha": SHA,
                    "event": "push",
                    "run_attempt": 2,
                    "run_started_at": "2026-10-28T14:00:00Z",
                    "created_at": "2026-10-28T13:00:00Z",
                },
            ]
        }
        self.assertEqual(pick_run(runs, SHA, "push")["id"], 1)

    def test_newest_created_among_equal_starts(self):
        run = {"head_sha": SHA, "event": "push", "run_attempt": 1, "run_started_at": "2026-10-28T14:00:00Z"}
        runs = {
            "workflow_runs": [
                {**run, "id": 1, "created_at": "2026-10-28T12:00:00Z"},
                {**run, "id": 2, "created_at": "2026-10-28T13:00:00Z"},
                {**run, "id": 3, "created_at": "2026-10-28T11:00:00Z"},
            ]
        }
        self.assertEqual(pick_run(runs, SHA, "push")["id"], 2)

    def test_run_without_start_is_older_than_any_started_run(self):
        run = {"head_sha": SHA, "event": "push", "run_attempt": 1}
        runs = {
            "workflow_runs": [
                {**run, "id": 1, "run_started_at": None, "created_at": "2026-10-28T15:00:00Z"},
                {**run, "id": 2, "run_started_at": "2026-10-28T12:00:00Z", "created_at": "2026-10-28T11:00:00Z"},
                {**run, "id": 3, "created_at": "2026-10-28T16:00:00Z"},
            ]
        }
        self.assertEqual(pick_run(runs, SHA, "push")["id"], 2)

    def test_runs_without_start_pick_newest_created(self):
        run = {"head_sha": SHA, "event": "push", "run_attempt": 1}
        runs = {
            "workflow_runs": [
                {**run, "id": 1, "run_started_at": None, "created_at": "2026-10-28T12:00:00Z"},
                {**run, "id": 2, "created_at": "2026-10-28T13:00:00Z"},
                {**run, "id": 3, "run_started_at": None, "created_at": "2026-10-28T11:00:00Z"},
            ]
        }
        self.assertEqual(pick_run(runs, SHA, "push")["id"], 2)


class BuildOutcomeTest(unittest.TestCase):
    def test_compile_succeeded(self):
        self.assertEqual(build_outcome(load("jobs-main-push.json")), "passed")
        self.assertEqual(build_outcome(load("jobs-on-pr.json")), "passed")

    def test_compile_failed(self):
        self.assertEqual(build_outcome(load("jobs-compile-failed.json")), "failed")

    def test_compile_never_ran(self):
        for conclusion in ("skipped", "cancelled", None):
            with self.subTest(conclusion=conclusion):
                self.assertEqual(build_outcome(with_compile(conclusion)), "failed")

    def test_compile_neither_success_nor_failure(self):
        for conclusion in ("timed_out", "neutral", "action_required", "stale"):
            with self.subTest(conclusion=conclusion):
                self.assertEqual(build_outcome(with_compile(conclusion)), "failed")
                self.assertEqual(native_targets(with_compile(conclusion), TOOLCHAIN)[0]["status"], "failed")

    def test_compile_step_absent(self):
        jobs_json = load("jobs-main-push.json")
        for job in jobs_json["jobs"]:
            job["steps"] = [s for s in job["steps"] if s["name"] != "Compile"]
        self.assertEqual(build_outcome(jobs_json), "failed")

    def test_no_cmake_job(self):
        self.assertEqual(build_outcome(without_jobs(load("jobs-main-push.json"), "CMake GCC /")), "failed")


class NativeTargetsTest(unittest.TestCase):
    def test_one_target_from_cmake_compile(self):
        self.assertEqual(
            native_targets(load("jobs-main-push.json"), TOOLCHAIN),
            [{"target": "x86_64-linux-gnu", "status": "passed", "toolchain": TOOLCHAIN}],
        )

    def test_compile_failed(self):
        self.assertEqual(
            native_targets(load("jobs-compile-failed.json"), TOOLCHAIN),
            [{"target": "x86_64-linux-gnu", "status": "failed", "toolchain": TOOLCHAIN}],
        )

    def test_bazel_job_ignored(self):
        jobs_json = load("jobs-main-push.json")
        self.assertTrue(any(job["name"].startswith("Bazel /") for job in jobs_json["jobs"]))
        for job in jobs_json["jobs"]:
            if job["name"].startswith("Bazel /"):
                job["conclusion"] = "failure"
                for step in job["steps"]:
                    step["conclusion"] = "failure"
        self.assertEqual(
            native_targets(jobs_json, TOOLCHAIN),
            [{"target": "x86_64-linux-gnu", "status": "passed", "toolchain": TOOLCHAIN}],
        )

    def test_missing_cmake_job(self):
        self.assertEqual(native_targets(without_jobs(load("jobs-main-push.json"), "CMake GCC /"), TOOLCHAIN), [])


class CrossTargetsTest(unittest.TestCase):
    def test_on_pr_cross_jobs(self):
        self.assertEqual(
            cross_targets(load("jobs-on-pr.json"), TOOLCHAIN),
            [
                {"target": "armv7-linux-gnueabihf", "status": "passed", "toolchain": TOOLCHAIN},
                {"target": "aarch64-linux-musl", "status": "passed", "toolchain": TOOLCHAIN},
                {"target": "armv7-linux-musleabihf", "status": "passed", "toolchain": TOOLCHAIN},
                {"target": "aarch64-linux-gnu", "status": "passed", "toolchain": TOOLCHAIN},
            ],
        )

    def test_unsuccessful_conclusion_is_failed(self):
        conclusions = ("failure", "cancelled", None, "neutral", "timed_out")
        jobs_json = cross_jobs(
            *({"name": "Bazel Cross (aarch64-linux-gnu) / build", "conclusion": c, "steps": []} for c in conclusions)
        )
        self.assertEqual([t["status"] for t in cross_targets(jobs_json, TOOLCHAIN)], ["failed"] * len(conclusions))

    def test_skipped_job_is_skipped(self):
        jobs_json = cross_jobs(
            {"name": "Bazel Cross (aarch64-linux-gnu) / build", "conclusion": "skipped", "steps": []}
        )
        self.assertEqual(
            cross_targets(jobs_json, TOOLCHAIN),
            [{"target": "aarch64-linux-gnu", "status": "skipped", "toolchain": TOOLCHAIN}],
        )

    def test_rejects_platform_that_is_not_a_triple(self):
        bad = ("aarch64-linux", "aarch64-linux-gnu-x", "AArch64-linux-gnu", "../x-y-z", "aarch64-linux-gnu ", "a-b-")
        for platform in bad:
            job = {"name": f"Bazel Cross ({platform}) / build", "conclusion": "success", "steps": []}
            with self.subTest(platform=platform), self.assertRaises(ValueError):
                cross_targets(cross_jobs(job), TOOLCHAIN)

    def test_no_cross_jobs(self):
        self.assertEqual(cross_targets(load("jobs-main-push.json"), TOOLCHAIN), [])


class TruncatedJobsTest(unittest.TestCase):
    def test_truncated_page_raises(self):
        for name in ("jobs-main-push.json", "jobs-on-pr.json"):
            jobs_json = load(name)
            jobs_json["jobs"].pop()
            calls = {
                "build_outcome": lambda: build_outcome(jobs_json),
                "native_targets": lambda: native_targets(jobs_json, TOOLCHAIN),
                "cross_targets": lambda: cross_targets(jobs_json, TOOLCHAIN),
            }
            for function, call in calls.items():
                with self.subTest(name=name, function=function), self.assertRaises(ValueError):
                    call()


class CliTest(unittest.TestCase):
    def run_cli(self, *args, stdin_name):
        with open(os.path.join(TESTDATA, stdin_name), encoding="utf-8") as stdin:
            return subprocess.run(
                [sys.executable, "-I", os.path.join(HERE, "runs.py"), *args],
                stdin=stdin,
                capture_output=True,
                text=True,
                check=False,
            )

    def test_pick_run_prints_id(self):
        result = self.run_cli("pick-run", "--sha", SHA, "--event", "push", stdin_name="runs-0bc2868.json")
        self.assertEqual((result.returncode, result.stdout), (0, "40690172036\n"))

    def test_pick_run_exits_3_when_none(self):
        result = self.run_cli("pick-run", "--sha", "f" * 40, "--event", "push", stdin_name="runs-0bc2868.json")
        self.assertEqual((result.returncode, result.stdout), (3, ""))

    def test_build_outcome(self):
        result = self.run_cli("build-outcome", stdin_name="jobs-main-push.json")
        self.assertEqual((result.returncode, result.stdout), (0, "passed\n"))
        result = self.run_cli("build-outcome", stdin_name="jobs-compile-failed.json")
        self.assertEqual((result.returncode, result.stdout), (0, "failed\n"))

    def test_targets_native_only(self):
        result = self.run_cli("targets", "--toolchain", TOOLCHAIN, stdin_name="jobs-main-push.json")
        self.assertEqual(result.returncode, 0)
        self.assertEqual(
            json.loads(result.stdout), [{"target": "x86_64-linux-gnu", "status": "passed", "toolchain": TOOLCHAIN}]
        )

    def test_targets_with_cross_jobs(self):
        result = self.run_cli(
            "targets",
            "--toolchain",
            TOOLCHAIN,
            "--cross-jobs",
            os.path.join(TESTDATA, "jobs-on-pr.json"),
            stdin_name="jobs-main-push.json",
        )
        self.assertEqual(result.returncode, 0)
        self.assertEqual(
            json.loads(result.stdout),
            native_targets(load("jobs-main-push.json"), TOOLCHAIN) + cross_targets(load("jobs-on-pr.json"), TOOLCHAIN),
        )

    def test_targets_needs_toolchain(self):
        result = self.run_cli("targets", stdin_name="jobs-main-push.json")
        self.assertEqual((result.returncode, result.stdout), (2, ""))
        self.assertIn("--toolchain", result.stderr)

    def test_unknown_command_is_a_usage_error(self):
        result = self.run_cli("frobnicate", stdin_name="jobs-main-push.json")
        self.assertEqual(result.returncode, 2)


if __name__ == "__main__":
    unittest.main()
