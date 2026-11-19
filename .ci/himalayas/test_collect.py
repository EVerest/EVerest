#
# SPDX-License-Identifier: Apache-2.0
# Copyright Pionix GmbH and Contributors to EVerest
import json
import os
import subprocess
import sys
import tempfile
import unittest

from junit_to_ctrf import convert
from runs import cross_targets, native_targets

HERE = os.path.dirname(os.path.abspath(__file__))
TESTDATA = os.path.join(HERE, "testdata")
SHA = "0bc286811e1279adc4711b6c5e6d9d141660277f"
FIRST_SHA = "0fa14b932e1774f354b283c596d3c9687bc0cc3a"
BUILD_URL = "https://github.com/EVerest/EVerest/actions/runs/40700000000"
UNIT_JUNIT = os.path.join(TESTDATA, "ctest-junit.xml")
OCPP_JUNIT = os.path.join(TESTDATA, "pytest-junit.xml")


def load(name):
    with open(os.path.join(TESTDATA, name), encoding="utf-8") as f:
        return json.load(f)


class CollectTest(unittest.TestCase):
    def setUp(self):
        self.tmp = self.enterContext(tempfile.TemporaryDirectory())
        self.out = os.path.join(self.tmp, "out")
        self.dist = os.path.join(self.tmp, "dist")
        for module in ("EvseManager", "OCPP201"):
            os.makedirs(os.path.join(self.dist, "libexec", "everest", "modules", module))
        self.targets = native_targets(load("jobs-main-push.json"), "gcc 13.2.0") + cross_targets(
            load("jobs-on-pr.json"), None
        )
        self.targets_path = self.write("targets.json", self.targets)

    def write(self, name, data):
        path = os.path.join(self.tmp, name)
        with open(path, "w", encoding="utf-8") as f:
            json.dump(data, f)
        return path

    def collect(self, *extra, sha=SHA, build_status="passed", pointers=None):
        args = [
            "--pointer",
            "testing/main",
            "--pointers-json",
            pointers or os.path.join(TESTDATA, "pointers.json"),
            "--sha",
            sha,
            "--build-url",
            BUILD_URL,
            "--build-status",
            build_status,
            "--targets",
            self.targets_path,
            "--start-ms",
            "1793156400000",
            "--stop-ms",
            "1793160000000",
            "--out",
            self.out,
            *extra,
        ]
        return subprocess.run(
            [sys.executable, "-I", os.path.join(HERE, "collect.py"), *args],
            capture_output=True,
            text=True,
            check=False,
        )

    def written(self):
        files = [os.path.relpath(os.path.join(d, f), self.out) for d, _, fs in os.walk(self.out) for f in fs]
        self.assertEqual(len(files), 1, files)
        with open(os.path.join(self.out, files[0]), encoding="utf-8") as f:
            return files[0], json.load(f)

    def test_passed_record(self):
        result = self.collect("--junit", f"unit={UNIT_JUNIT}", "--junit", f"ocpp={OCPP_JUNIT}", "--dist", self.dist)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn(f"artifact=himalayas-record_testing-main_{SHA}\n", result.stdout)
        path, record = self.written()
        self.assertEqual(path, "testing/main/20261028-2.json")
        results = record["results"]
        self.assertEqual(results["environment"]["commit"], SHA)
        self.assertEqual(results["environment"]["buildUrl"], BUILD_URL)
        self.assertEqual(
            results["extra"]["build"],
            {"status": "passed", "modules": ["EvseManager", "OCPP201"], "targets": self.targets},
        )
        self.assertEqual(results["tests"], convert(UNIT_JUNIT, "unit") + convert(OCPP_JUNIT, "ocpp"))
        self.assertEqual((results["summary"]["start"], results["summary"]["stop"]), (1793156400000, 1793160000000))

    def test_first_move_of_the_day_has_no_suffix(self):
        result = self.collect("--dist", self.dist, sha=FIRST_SHA)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(self.written()[0], "testing/main/20261028.json")

    def test_failed_build_carries_no_tests_and_no_modules(self):
        result = self.collect("--junit", f"unit={UNIT_JUNIT}", "--dist", self.tmp + "/nowhere", build_status="failed")
        self.assertEqual(result.returncode, 0, result.stderr)
        results = self.written()[1]["results"]
        self.assertEqual(results["extra"]["build"], {"status": "failed", "targets": self.targets})
        self.assertEqual(results["tests"], [])

    def test_failed_build_without_junit_or_dist(self):
        result = self.collect(build_status="failed")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(self.written()[1]["results"]["tests"], [])

    def test_missing_junit_warns_and_is_skipped(self):
        missing = os.path.join(self.tmp, "ctest-junit.xml")
        result = self.collect("--junit", f"unit={missing}", "--junit", f"ocpp={OCPP_JUNIT}", "--dist", self.dist)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn(f"::warning::", result.stdout)
        self.assertIn(missing, result.stdout)
        self.assertEqual(self.written()[1]["results"]["tests"], convert(OCPP_JUNIT, "ocpp"))

    def test_malformed_junit_fails(self):
        bad = os.path.join(self.tmp, "bad.xml")
        with open(bad, "w", encoding="utf-8") as f:
            f.write("<testsuite><testcase")
        result = self.collect("--junit", f"unit={bad}", "--dist", self.dist)
        self.assertNotEqual(result.returncode, 0)
        self.assertFalse(os.path.exists(self.out))

    def test_passed_build_needs_dist(self):
        result = self.collect()
        self.assertEqual(result.returncode, 2)
        self.assertFalse(os.path.exists(self.out))

    def test_unknown_suite_is_a_usage_error(self):
        result = self.collect("--junit", f"e2e={UNIT_JUNIT}", "--dist", self.dist)
        self.assertEqual(result.returncode, 2)

    def test_unknown_sha_exits_2(self):
        result = self.collect("--dist", self.dist, sha="f" * 40)
        self.assertEqual(result.returncode, 2)
        self.assertIn("f" * 40, result.stderr)
        self.assertFalse(os.path.exists(self.out))

    def test_value_error_exits_2(self):
        self.targets_path = self.write("targets.json", [{"target": "x86_64-linux", "status": "passed"}])
        result = self.collect("--dist", self.dist)
        self.assertEqual(result.returncode, 2)
        self.assertIn("x86_64-linux", result.stderr)
        self.assertFalse(os.path.exists(self.out))


if __name__ == "__main__":
    unittest.main()
