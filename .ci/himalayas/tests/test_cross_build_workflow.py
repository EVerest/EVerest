#
# SPDX-License-Identifier: Apache-2.0
# Copyright Pionix GmbH and Contributors to EVerest
import os
import unittest

from himalayas_record.runs import CROSS_FAILED_STEP

HERE = os.path.dirname(os.path.abspath(__file__))
CROSS_BUILD_WORKFLOW = os.path.join(HERE, "..", "..", "..", ".github", "workflows", "job_bazel-cross-build.yaml")


class CrossBuildWorkflowTest(unittest.TestCase):
    def test_has_the_step_that_marks_a_failed_build(self):
        with open(CROSS_BUILD_WORKFLOW, encoding="utf-8") as f:
            step_names = [line.strip().removeprefix("- name:").strip() for line in f if line.strip().startswith("- name:")]
        self.assertIn(CROSS_FAILED_STEP, step_names)


if __name__ == "__main__":
    unittest.main()
