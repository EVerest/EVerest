#
# SPDX-License-Identifier: Apache-2.0
# Copyright Pionix GmbH and Contributors to EVerest
"""Convert a JUnit XML report into the test entries of a CTRF record."""

import hashlib
import math
import xml.etree.ElementTree as ET

NAME_LIMIT = 400
MESSAGE_LIMIT = 500
TRACE_LIMIT = 4000


def _bounded_name(name: str) -> str:
    if len(name) <= NAME_LIMIT:
        return name
    digest = hashlib.sha1(name.encode("utf-8")).hexdigest()[:8]
    return f"{name[:NAME_LIMIT - len(digest) - 1]}~{digest}"


def _duration_ms(time: str | None) -> int:
    try:
        duration_ms = float(time) * 1000
    except (TypeError, ValueError):
        return 0
    return round(duration_ms) if math.isfinite(duration_ms) and duration_ms >= 0 else 0


def convert(xml_path: str, suite: str) -> list[dict]:
    tests = []
    for index, case in enumerate(ET.parse(xml_path).getroot().iter("testcase")):
        classname = case.get("classname", "")
        name = case.get("name", "")
        if not name:
            raise ValueError(f"{xml_path}: testcase {index} has no name")
        problem = next((child for child in case if child.tag in ("failure", "error")), None)
        if problem is not None:
            status = "failed"
        else:
            problem = case.find("skipped")
            status = "passed" if problem is None else "skipped"
        test = {
            "name": _bounded_name(f"{classname}.{name}" if classname and classname != name else name),
            "status": status,
            "duration": _duration_ms(case.get("time")),
            "suite": [suite],
            "tags": [suite],
        }
        if problem is not None:
            if "message" in problem.attrib:
                test["message"] = problem.get("message")[:MESSAGE_LIMIT]
            if problem.text and problem.text.strip():
                test["trace"] = problem.text[:TRACE_LIMIT]
        tests.append(test)
    return tests
