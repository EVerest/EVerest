#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright Pionix GmbH and Contributors to EVerest

"""Shared assertion helpers for the management API integration tests.

Used by both lifecycle_api_tests.py and configuration_api_tests.py.
"""


def assert_status_subsequence(statuses: list, expected: list) -> None:
    """Assert that `expected` appears, in order, as a (not necessarily contiguous)
    subsequence of `statuses`."""
    idx = 0
    for status in statuses:
        if idx < len(expected) and status == expected[idx]:
            idx += 1
    assert idx == len(expected), f"Expected subsequence {expected} in {statuses}"
