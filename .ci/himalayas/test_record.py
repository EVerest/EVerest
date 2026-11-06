#
# SPDX-License-Identifier: Apache-2.0
# Copyright Pionix GmbH and Contributors to EVerest
import json
import os
import tempfile
import unittest

from record import artifact_name, build_record, find_move, modules_from_dist, record_path

TESTDATA = os.path.join(os.path.dirname(os.path.abspath(__file__)), "testdata")
SHA = "0bc286811e1279adc4711b6c5e6d9d141660277f"
TARGETS = [
    {"target": "x86_64-linux-gnu", "status": "passed", "toolchain": "gcc 13.2.0"},
    {"target": "aarch64-linux-gnu", "status": "failed", "toolchain": "gcc 13.2.0"},
]
TESTS = [
    {"name": "a", "status": "passed", "duration": 1, "suite": ["unit"], "tags": ["unit"]},
    {"name": "b", "status": "failed", "duration": 2, "suite": ["unit"], "tags": ["unit"], "message": "boom"},
    {"name": "c", "status": "passed", "duration": 3, "suite": ["ocpp"], "tags": ["ocpp"]},
    {"name": "d", "status": "skipped", "duration": 0, "suite": ["ocpp"], "tags": ["ocpp"]},
]


def load_pointers():
    with open(os.path.join(TESTDATA, "pointers.json"), encoding="utf-8") as f:
        return json.load(f)


def make_record(**overrides):
    args = {
        "pointer": "testing/main",
        "commit": SHA,
        "start_ms": 1793156400000,
        "stop_ms": 1793160000000,
        "build_url": "https://github.com/EVerest/EVerest/actions/runs/40690172036",
        "build_status": "passed",
        "modules": ["EvseManager", "RsExample"],
        "targets": TARGETS,
        "tests": TESTS,
    }
    args.update(overrides)
    return build_record(**args)


class ModulesFromDistTest(unittest.TestCase):
    def test_lists_module_directories_sorted(self):
        with tempfile.TemporaryDirectory() as dist:
            modules = os.path.join(dist, "libexec", "everest", "modules")
            os.makedirs(os.path.join(modules, "RsExample"))
            os.makedirs(os.path.join(modules, "EvseManager"))
            self.assertEqual(modules_from_dist(dist), ["EvseManager", "RsExample"])

    def test_ignores_files(self):
        with tempfile.TemporaryDirectory() as dist:
            modules = os.path.join(dist, "libexec", "everest", "modules")
            os.makedirs(os.path.join(modules, "Zeta"))
            os.makedirs(os.path.join(modules, "Alpha"))
            open(os.path.join(modules, "README"), "w").close()
            self.assertEqual(modules_from_dist(dist), ["Alpha", "Zeta"])

    def test_empty_modules_directory_raises(self):
        with tempfile.TemporaryDirectory() as dist:
            os.makedirs(os.path.join(dist, "libexec", "everest", "modules"))
            with self.assertRaises(ValueError):
                modules_from_dist(dist)

    def test_missing_modules_directory_raises(self):
        with tempfile.TemporaryDirectory() as dist:
            with self.assertRaises(ValueError):
                modules_from_dist(dist)


class ArtifactNameTest(unittest.TestCase):
    def test_pointer_slashes_become_dashes(self):
        self.assertEqual(artifact_name("testing/main", SHA), f"himalayas-record_testing-main_{SHA}")
        self.assertEqual(
            artifact_name("testing/stable-2026.10", SHA), f"himalayas-record_testing-stable-2026.10_{SHA}"
        )

    def test_rejects_bad_sha(self):
        for sha in ("0bc2868", SHA.upper(), SHA + "0", SHA + "\n", "g" * 40, ""):
            with self.subTest(sha=sha), self.assertRaises(ValueError):
                artifact_name("testing/main", sha)


class PointerTest(unittest.TestCase):
    BAD = (
        "../../x",
        "testing/../../x",
        "testing/main/../../x",
        "testing/feature",
        "testing/stable-2026.1",
        "testing/stable/2026.10",
        "testing/main\n",
        "main",
        "",
        None,
    )

    def test_artifact_name_rejects_bad_pointer(self):
        for pointer in self.BAD:
            with self.subTest(pointer=pointer), self.assertRaises(ValueError):
                artifact_name(pointer, SHA)

    def test_record_path_rejects_bad_pointer(self):
        for pointer in self.BAD:
            with self.subTest(pointer=pointer), self.assertRaises(ValueError):
                record_path(pointer, "2026-10-29T03:11:42Z")

    def test_stable_pointer_accepted(self):
        self.assertEqual(
            record_path("testing/stable-2026.10", "2026-10-29T03:11:42Z"), "testing/stable-2026.10/20261029.json"
        )


class RecordPathTest(unittest.TestCase):
    def test_first_move_of_the_day(self):
        self.assertEqual(record_path("testing/main", "2026-10-29T03:11:42Z"), "testing/main/20261029.json")

    def test_later_move_of_the_day(self):
        self.assertEqual(record_path("testing/main", "2026-10-29T03:11:42Z", 2), "testing/main/20261029-2.json")

    def test_rejects_n_below_one(self):
        for n in (0, -1):
            with self.subTest(n=n), self.assertRaises(ValueError):
                record_path("testing/main", "2026-10-29T03:11:42Z", n)

    def test_rejects_non_utc_timestamp(self):
        for moved_at in ("2026-10-29T03:11:42+02:00", "2026-10-29", "yesterday"):
            with self.subTest(moved_at=moved_at), self.assertRaises(ValueError):
                record_path("testing/main", moved_at)


class FindMoveTest(unittest.TestCase):
    def test_first_move_of_the_day(self):
        self.assertEqual(
            find_move(load_pointers(), "testing/main", "0fa14b932e1774f354b283c596d3c9687bc0cc3a"),
            ("2026-10-28T03:21:15Z", 1),
        )

    def test_second_move_of_the_day(self):
        self.assertEqual(find_move(load_pointers(), "testing/main", SHA), ("2026-10-28T13:05:41Z", 2))

    def test_other_pointers_on_the_same_day_not_counted(self):
        self.assertEqual(
            find_move(load_pointers(), "testing/stable-2026.10", "f45ba0d0827f29d53d516fec98960f2528460802"),
            ("2026-10-23T07:37:58Z", 1),
        )

    def test_same_sha_on_another_pointer_does_not_match(self):
        with self.assertRaises(LookupError):
            find_move(load_pointers(), "testing/stable-2026.02", SHA)

    def test_last_matching_entry_wins(self):
        pointers = {
            "log": [
                {"pointer": "testing/main", "sha": SHA, "moved_at": "2026-10-28T01:00:00Z"},
                {"pointer": "testing/main", "sha": "1" * 40, "moved_at": "2026-10-29T01:00:00Z"},
                {"pointer": "testing/main", "sha": "2" * 40, "moved_at": "2026-10-29T02:00:00Z"},
                {"pointer": "testing/main", "sha": SHA, "moved_at": "2026-10-29T03:00:00Z"},
                {"pointer": "testing/main", "sha": "3" * 40, "moved_at": "2026-10-29T04:00:00Z"},
            ]
        }
        self.assertEqual(find_move(pointers, "testing/main", SHA), ("2026-10-29T03:00:00Z", 3))

    def test_unknown_sha_raises(self):
        with self.assertRaises(LookupError):
            find_move(load_pointers(), "testing/main", "f" * 40)


class BuildRecordTest(unittest.TestCase):
    def test_passed_build_shape(self):
        record = make_record()
        self.assertEqual(record["reportFormat"], "CTRF")
        self.assertEqual(record["specVersion"], "1.0.0")
        results = record["results"]
        self.assertEqual(results["tool"], {"name": "everest-ci", "version": "1"})
        self.assertEqual(
            results["environment"],
            {
                "appName": "EVerest",
                "appVersion": "testing/main",
                "commit": SHA,
                "branchName": "main",
                "testEnvironment": "SIL",
                "osPlatform": "linux",
                "healthy": True,
                "buildUrl": "https://github.com/EVerest/EVerest/actions/runs/40690172036",
            },
        )
        self.assertEqual(
            results["extra"],
            {
                "build": {"status": "passed", "modules": ["EvseManager", "RsExample"], "targets": TARGETS},
                "integrator": {"id": "everest"},
            },
        )
        self.assertEqual(results["tests"], TESTS)
        self.assertEqual(set(record), {"reportFormat", "specVersion", "results"})
        self.assertEqual(set(results), {"tool", "summary", "environment", "extra", "tests"})

    def test_summary_counted_from_tests(self):
        self.assertEqual(
            make_record()["results"]["summary"],
            {
                "tests": 4,
                "passed": 2,
                "failed": 1,
                "skipped": 1,
                "pending": 0,
                "other": 0,
                "start": 1793156400000,
                "stop": 1793160000000,
            },
        )

    def test_failed_build_without_modules(self):
        record = make_record(build_status="failed", modules=[], tests=[])
        results = record["results"]
        self.assertFalse(results["environment"]["healthy"])
        self.assertEqual(results["extra"]["build"], {"status": "failed", "targets": TARGETS})
        self.assertEqual(results["tests"], [])
        self.assertEqual(results["summary"]["tests"], 0)

    def test_healthy_mirrors_build_status(self):
        self.assertIs(make_record()["results"]["environment"]["healthy"], True)
        self.assertIs(
            make_record(build_status="failed", modules=[], tests=[])["results"]["environment"]["healthy"], False
        )

    def test_rejects_unknown_build_status(self):
        for status in ("success", "skipped", "", None):
            with self.subTest(status=status), self.assertRaises(ValueError):
                make_record(build_status=status)

    def test_rejects_failed_build_with_modules_or_tests(self):
        with self.assertRaises(ValueError):
            make_record(build_status="failed", modules=["EvseManager"], tests=[])
        with self.assertRaises(ValueError):
            make_record(build_status="failed", modules=[], tests=TESTS)

    def test_rejects_passed_build_without_modules(self):
        with self.assertRaises(ValueError):
            make_record(modules=[])

    def test_rejects_bad_commit(self):
        for commit in ("0bc2868", SHA.upper(), SHA + "\n"):
            with self.subTest(commit=commit), self.assertRaises(ValueError):
                make_record(commit=commit)

    def test_rejects_bad_targets(self):
        bad = [
            "x86_64-linux-gnu",
            {"target": "x86_64-linux-gnu", "status": "passed"},
            {"target": "x86_64-linux-gnu", "status": "success", "toolchain": "gcc 13"},
            {"target": "x86_64-linux-gnu", "status": "passed", "toolchain": "gcc 13", "extra": 1},
        ]
        for target in bad:
            with self.subTest(target=target), self.assertRaises(ValueError):
                make_record(targets=[target])

    def test_empty_targets_omitted(self):
        self.assertEqual(
            make_record(targets=[])["results"]["extra"]["build"],
            {"status": "passed", "modules": ["EvseManager", "RsExample"]},
        )
        self.assertEqual(
            make_record(build_status="failed", modules=[], tests=[], targets=[])["results"]["extra"]["build"],
            {"status": "failed"},
        )

    def test_rejects_bad_start_and_stop(self):
        for start_ms, stop_ms in ((0, 1), (-1, 1), (2, 1), (1793160000000, 1793156400000)):
            with self.subTest(start_ms=start_ms, stop_ms=stop_ms), self.assertRaises(ValueError):
                make_record(start_ms=start_ms, stop_ms=stop_ms)

    def test_accepts_start_equal_to_stop(self):
        summary = make_record(start_ms=1, stop_ms=1)["results"]["summary"]
        self.assertEqual((summary["start"], summary["stop"]), (1, 1))

    def test_rejects_build_url_not_https(self):
        bad = ("http://github.com/EVerest/EVerest/actions/runs/1", "javascript:alert(1)", "github.com/x", "", None)
        for build_url in bad:
            with self.subTest(build_url=build_url), self.assertRaises(ValueError):
                make_record(build_url=build_url)

    def test_accepts_module_names(self):
        for module in ("Ab", "EvseManager", "OCPP201", "Rs_Example", "A" + "b" * 49):
            with self.subTest(module=module):
                self.assertEqual(make_record(modules=[module])["results"]["extra"]["build"]["modules"], [module])

    def test_rejects_bad_module_names(self):
        bad = ("A", "evseManager", "_Evse", "Evse-Manager", "Evse Manager", "A" + "b" * 50, "Evse\n", "../x", "", None)
        for module in bad:
            with self.subTest(module=module), self.assertRaises(ValueError):
                make_record(modules=["EvseManager", module])

    def test_skipped_target(self):
        target = {"target": "aarch64-linux-gnu", "status": "skipped", "toolchain": "gcc 13.2.0"}
        self.assertEqual(make_record(targets=[target])["results"]["extra"]["build"]["targets"], [target])

    def test_accepts_toolchains(self):
        accepted = ("gcc 13", "gcc 13.2", "gcc 13.2.0", "clang 18.1.8", "msvc 19.40", "icc 2021", "gcc " + "1" * 56)
        for toolchain in accepted:
            target = {"target": "x86_64-linux-gnu", "status": "passed", "toolchain": toolchain}
            with self.subTest(toolchain=toolchain):
                self.assertEqual(make_record(targets=[target])["results"]["extra"]["build"]["targets"], [target])

    def test_rejects_bad_toolchains(self):
        bad = (
            "bazel",
            "cmake-gcc",
            "gcc",
            "gcc13",
            "GCC 13",
            "gcc 13.2.0.1",
            "gcc 13.",
            "gcc 13\n",
            " gcc 13",
            "gcc " + "1" * 57,
            None,
        )
        for toolchain in bad:
            target = {"target": "x86_64-linux-gnu", "status": "passed", "toolchain": toolchain}
            with self.subTest(toolchain=toolchain), self.assertRaises(ValueError):
                make_record(targets=[target])

    def test_branch_name_derived_from_pointer(self):
        for pointer, branch_name in (("testing/main", "main"), ("testing/stable-2026.10", "stable/2026.10")):
            with self.subTest(pointer=pointer):
                environment = make_record(pointer=pointer)["results"]["environment"]
                self.assertEqual((environment["appVersion"], environment["branchName"]), (pointer, branch_name))

    def test_rejects_bad_pointer(self):
        for pointer in PointerTest.BAD:
            with self.subTest(pointer=pointer), self.assertRaises(ValueError):
                make_record(pointer=pointer)

    def test_branch_name_not_accepted(self):
        with self.assertRaises(TypeError):
            make_record(branch_name="main")

    def test_rejects_duplicate_modules(self):
        with self.assertRaises(ValueError):
            make_record(modules=["EvseManager", "RsExample", "EvseManager"])

    def test_accepts_target_triples(self):
        for triple in ("x86_64-linux-gnu", "armv7-linux-musleabihf", "a" * 90 + "-linux-gnu"):
            target = {"target": triple, "status": "passed", "toolchain": "gcc 13.2.0"}
            with self.subTest(triple=triple):
                self.assertEqual(make_record(targets=[target])["results"]["extra"]["build"]["targets"], [target])

    def test_rejects_bad_target_triples(self):
        bad = (
            "x86_64-linux",
            "x86_64-linux-gnu-x",
            "X86_64-linux-gnu",
            "../x-y-z",
            "x86_64-linux-gnu\n",
            "a" * 91 + "-linux-gnu",
            "",
            None,
        )
        for triple in bad:
            target = {"target": triple, "status": "passed", "toolchain": "gcc 13.2.0"}
            with self.subTest(triple=triple), self.assertRaises(ValueError):
                make_record(targets=[target])

    def test_rejects_start_and_stop_that_are_not_int(self):
        for start_ms, stop_ms in ((1.5, 2), (True, 2), (1, 2.5), (1, True), ("1", 2)):
            with self.subTest(start_ms=start_ms, stop_ms=stop_ms), self.assertRaises(ValueError):
                make_record(start_ms=start_ms, stop_ms=stop_ms)

    def test_summary_counts_pending_and_other(self):
        tests = TESTS + [
            {"name": "e", "status": "pending", "duration": 0, "suite": ["ocpp"], "tags": ["ocpp"]},
            {"name": "f", "status": "other", "duration": 0, "suite": ["ocpp"], "tags": ["ocpp"]},
            {"name": "g", "status": "other", "duration": 0, "suite": ["ocpp"], "tags": ["ocpp"]},
        ]
        summary = make_record(tests=tests)["results"]["summary"]
        self.assertEqual(
            [summary[k] for k in ("tests", "passed", "failed", "skipped", "pending", "other")], [7, 2, 1, 1, 1, 2]
        )

    def test_round_trips_through_json(self):
        record = make_record()
        self.assertEqual(json.loads(json.dumps(record)), record)


if __name__ == "__main__":
    unittest.main()
