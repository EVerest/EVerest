# SPDX-License-Identifier: Apache-2.0
# Copyright 2020 - 2026 Pionix GmbH and Contributors to EVerest

import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).parent))
from create_file_hashes import find_unregistered_yaml, read_data_lines, verify_hashes

# lib/everest/everest_api_types/tests/manual_tests/source_file_hash_check/
# parents[2] == tests/,  parents[6] == EVerest/ (CMAKE PROJECT_SOURCE_DIR, contains types/ and interfaces/)
_TESTS_DIR = Path(__file__).parents[2]
_PROJECT_ROOT = Path(__file__).parents[6]

_ACTUAL_CSV_OPTION = {
    "everest_core_types":      "--actual-types-csv",
    "everest_core_interfaces": "--actual-ifc-csv",
}


@pytest.mark.parametrize(
    "test_name,expected_csv",
    [
        ("everest_core_types",      _TESTS_DIR / "expected_types_file_hashes.csv"),
        ("everest_core_interfaces", _TESTS_DIR / "expected_interfaces_file_hashes.csv"),
    ],
    ids=["everest_core_types", "everest_core_interfaces"],
)
def test_file_hashes(test_name: str, expected_csv: Path, request: pytest.FixtureRequest) -> None:
    actual_csv: Path | None = request.config.getoption(_ACTUAL_CSV_OPTION[test_name])
    assert actual_csv is not None, (
        f"Required option '{_ACTUAL_CSV_OPTION[test_name]}' not provided."
    )

    unregistered = find_unregistered_yaml(expected_csv, _PROJECT_ROOT)
    assert not unregistered, (
        "The following yaml files are not listed in the expected hashes CSV"
        f" and must be added to '{expected_csv.name}':\n"
        + "\n".join(f"  {f}" for f in unregistered)
    )

    expected_lines = read_data_lines(expected_csv)
    actual_lines   = read_data_lines(actual_csv)

    assert len(expected_lines) == len(actual_lines), (
        f"Number of hash entries does not match.\n"
        f"  Expected file has {len(expected_lines)} entries.\n"
        f"  Actual file has {len(actual_lines)} entries."
    )

    _matching, mismatching = verify_hashes(expected_csv, actual_csv)

    assert not mismatching, (
        "Hash mismatch detected!\n\n"
        + "\n".join(
            f"  actual:   {actual}\n  expected: {expected}"
            for expected, actual in mismatching
        )
        + "\n\n####################################################################################\n"
        + "######### See lib/everest/everest_api_types/README.md for further details. #########\n"
        + "####################################################################################\n"
    )
