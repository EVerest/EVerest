# SPDX-License-Identifier: Apache-2.0
# Copyright 2020 - 2025 Pionix GmbH and Contributors to EVerest

from pathlib import Path


def pytest_addoption(parser: object) -> None:
    parser.addoption(
        "--actual-types-csv",
        type=Path,
        required=True,
        help="Path to actual_types_file_hashes.csv generated in the build folder by CMake.",
    )
    parser.addoption(
        "--actual-ifc-csv",
        type=Path,
        required=True,
        help="Path to actual_interfaces_file_hashes.csv generated in the build folder by CMake.",
    )
