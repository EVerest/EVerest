#
# SPDX-License-Identifier: Apache-2.0
# Copyright Pionix GmbH and Contributors to EVerest
import hashlib
import os
import tempfile
import unittest
import xml.etree.ElementTree as ET

from junit_to_ctrf import convert

TESTDATA = os.path.join(os.path.dirname(os.path.abspath(__file__)), "testdata")


class ConvertTest(unittest.TestCase):
    def convert_text(self, text, suite="unit"):
        with tempfile.TemporaryDirectory() as tmp:
            path = os.path.join(tmp, "result.xml")
            with open(path, "w", encoding="utf-8") as f:
                f.write(text)
            return convert(path, suite)

    def test_ctest_cases_in_document_order(self):
        tests = convert(os.path.join(TESTDATA, "ctest-junit.xml"), "unit")
        self.assertEqual(
            [t["name"] for t in tests],
            [
                "everest-core_EvseManager_tests",
                "libocpp_unit_tests",
                "everest-core_OCPPmulti_tests",
            ],
        )
        self.assertEqual([t["status"] for t in tests], ["passed", "failed", "skipped"])

    def test_ctest_passed_case(self):
        passed = convert(os.path.join(TESTDATA, "ctest-junit.xml"), "unit")[0]
        self.assertEqual(
            passed,
            {
                "name": "everest-core_EvseManager_tests",
                "status": "passed",
                "duration": 72,
                "suite": ["unit"],
                "tags": ["unit"],
            },
        )

    def test_ctest_failure_message_and_trace(self):
        failed = convert(os.path.join(TESTDATA, "ctest-junit.xml"), "unit")[1]
        self.assertEqual(failed["message"], "Failed")
        self.assertEqual(failed["trace"], "ChargePointTest.Reset: Value of: result\n  Actual: false\nExpected: true")
        self.assertEqual(failed["duration"], 1500)

    def test_ctest_skipped_has_message_without_trace(self):
        skipped = convert(os.path.join(TESTDATA, "ctest-junit.xml"), "unit")[2]
        self.assertEqual(skipped["message"], "Disabled")
        self.assertNotIn("trace", skipped)
        self.assertEqual(skipped["duration"], 0)

    def test_pytest_classname_and_error(self):
        tests = convert(os.path.join(TESTDATA, "pytest-junit.xml"), "ocpp")
        self.assertEqual(
            [t["name"] for t in tests],
            [
                "core_tests.startup_tests.TestStartup.test_startup[legacy]",
                "ocpp_tests.test_sets.ocpp201.authorization.test_C01[multi]",
            ],
        )
        self.assertEqual(tests[1]["status"], "failed")
        self.assertEqual(tests[1]["message"], 'failed on setup with "TimeoutError: CSMS did not connect"')
        self.assertTrue(tests[1]["trace"].endswith("E   TimeoutError: CSMS did not connect"))
        self.assertEqual(tests[1]["suite"], ["ocpp"])
        self.assertEqual(tests[1]["tags"], ["ocpp"])

    def test_name_without_classname(self):
        tests = self.convert_text('<testsuite><testcase name="a" classname=""/><testcase name="b"/></testsuite>')
        self.assertEqual([t["name"] for t in tests], ["a", "b"])

    def test_classname_equal_to_name_used_once(self):
        tests = self.convert_text(
            '<testsuite><testcase name="a" classname="a"/><testcase name="b" classname="c"/></testsuite>'
        )
        self.assertEqual([t["name"] for t in tests], ["a", "c.b"])

    def test_duration_rounds_to_milliseconds(self):
        tests = self.convert_text(
            '<testsuite><testcase name="a" time="0.0004"/><testcase name="b" time="0.0015"/>'
            '<testcase name="c" time="2"/><testcase name="d"/><testcase name="e" time=""/></testsuite>'
        )
        self.assertEqual([t["duration"] for t in tests], [0, 2, 2000, 0, 0])

    def test_nested_suites_in_document_order(self):
        tests = self.convert_text(
            "<testsuites><testsuite>"
            '<testcase name="a"/>'
            '<testsuite><testcase name="b"/><testsuite><testcase name="c"/></testsuite></testsuite>'
            '<testcase name="d"/>'
            "</testsuite></testsuites>"
        )
        self.assertEqual([t["name"] for t in tests], ["a", "b", "c", "d"])

    def test_failure_or_error_wins_over_earlier_skipped(self):
        tests = self.convert_text(
            '<testsuite><testcase name="a"><skipped message="s"/><failure message="f">ft</failure></testcase>'
            '<testcase name="b"><error message="e">et</error><failure message="f">ft</failure></testcase>'
            "</testsuite>"
        )
        self.assertEqual((tests[0]["status"], tests[0]["message"], tests[0]["trace"]), ("failed", "f", "ft"))
        self.assertEqual((tests[1]["status"], tests[1]["message"], tests[1]["trace"]), ("failed", "e", "et"))

    def test_message_and_trace_truncated(self):
        message = "m" * 600
        trace = "t" * 5000
        tests = self.convert_text(
            f'<testsuite><testcase name="a"><failure message="{message}">{trace}</failure></testcase></testsuite>'
        )
        self.assertEqual(tests[0]["message"], "m" * 500)
        self.assertEqual(tests[0]["trace"], "t" * 4000)

    def test_absent_message_and_empty_text_omitted(self):
        tests = self.convert_text(
            '<testsuite><testcase name="a"><failure/></testcase>'
            '<testcase name="b"><error>  </error></testcase></testsuite>'
        )
        self.assertEqual(
            tests[0], {"name": "a", "status": "failed", "duration": 0, "suite": ["unit"], "tags": ["unit"]}
        )
        self.assertNotIn("message", tests[1])
        self.assertNotIn("trace", tests[1])

    def test_empty_message_attribute_kept(self):
        tests = self.convert_text('<testsuite><testcase name="a"><skipped message=""/></testcase></testsuite>')
        self.assertEqual(tests[0]["message"], "")

    def test_name_of_400_characters_kept(self):
        name = "n" * 400
        tests = self.convert_text(f'<testsuite><testcase name="{name}"/></testsuite>')
        self.assertEqual(tests[0]["name"], name)

    def test_name_over_400_characters_shortened_with_hash(self):
        name = "n" * 300 + "m" * 101
        tests = self.convert_text(f'<testsuite><testcase name="{name}"/></testsuite>')
        digest = hashlib.sha1(name.encode("utf-8")).hexdigest()[:8]
        self.assertEqual(tests[0]["name"], name[:391] + "~" + digest)
        self.assertEqual(len(tests[0]["name"]), 400)

    def test_long_names_sharing_a_prefix_stay_distinct(self):
        prefix = "p" * 500
        tests = self.convert_text(
            f'<testsuite><testcase classname="{prefix}" name="a"/><testcase classname="{prefix}" name="b"/></testsuite>'
        )
        self.assertNotEqual(tests[0]["name"], tests[1]["name"])

    def test_case_without_name_raises(self):
        for case in ('<testcase classname="c"/>', '<testcase name="" classname="c"/>'):
            with self.subTest(case=case):
                with self.assertRaises(ValueError) as raised:
                    self.convert_text(f'<testsuite><testcase name="a"/>{case}</testsuite>')
                self.assertIn("result.xml", str(raised.exception))
                self.assertIn("testcase 1", str(raised.exception))

    def test_duration_zero_for_non_finite_or_negative_time(self):
        tests = self.convert_text(
            '<testsuite><testcase name="a" time="nan"/><testcase name="b" time="inf"/>'
            '<testcase name="c" time="-inf"/><testcase name="d" time="-1.5"/><testcase name="e" time="1e308"/>'
            '<testcase name="f" time="abc"/><testcase name="g" time="-0.0009"/></testsuite>'
        )
        self.assertEqual([t["duration"] for t in tests], [0, 0, 0, 0, 0, 0, 0])

    def test_missing_file_raises(self):
        with self.assertRaises(FileNotFoundError):
            convert(os.path.join(TESTDATA, "no-such.xml"), "unit")

    def test_malformed_file_raises(self):
        with self.assertRaises(ET.ParseError):
            self.convert_text("<testsuite><testcase name='a'></testsuite>")


if __name__ == "__main__":
    unittest.main()
