#!/usr/bin/env python3
"""
Scans the source folder declared in an expected-hashes CSV, recomputes SHA256
for TRACKING entries, and writes the result to an actual-hashes CSV.

CSV format per data line:  <hash>,<TRACKING|___no___>,<relative/path/to/file>
Blank lines and lines starting with '#' are ignored.

Errors if any file in the source folder is not listed in the expected CSV.
"""

import argparse
import hashlib
import sys
from pathlib import Path

_ZERO_HASH = "0" * 64


def read_data_lines(csv_path: Path) -> list[str]:
    """Return stripped non-blank, non-comment lines from a hashes CSV."""
    lines = []
    for line in csv_path.read_text(encoding="utf-8").splitlines():
        stripped = line.strip()
        if stripped and not stripped.startswith("#"):
            lines.append(stripped)
    return lines


def verify_hashes(expected_csv: Path, actual_csv: Path) -> tuple[list[str], list[tuple[str, str]]]:
    """Compare two hashes CSVs line by line.

    Returns:
        matching:    lines that are identical in both files.
        mismatching: (expected, actual) pairs for lines that differ.
    """
    expected_lines = read_data_lines(expected_csv)
    actual_lines   = read_data_lines(actual_csv)

    matching:    list[str]             = []
    mismatching: list[tuple[str, str]] = []

    for expected, actual in zip(expected_lines, actual_lines):
        if expected == actual:
            matching.append(expected)
        else:
            mismatching.append((expected, actual))

    return matching, mismatching


def _parse_line(line: str, csv_path: Path) -> tuple[str, str]:
    """Return (tracking_status, relative_filename) from a hashes CSV data line."""
    parts = line.split(",", 2)
    if len(parts) != 3:
        sys.exit(
            f"error: line '{line}' in '{csv_path}' does not match expected '<hash>,<TRACKING|___no___>,<filename>' format"
        )
    return parts[1], parts[2]


def _sha256(path: Path) -> str:
    digest = hashlib.sha256()
    digest.update(path.read_bytes())
    return digest.hexdigest()


def find_unregistered_yaml(expected_csv: Path, src_root: Path) -> list[str]:
    """Return yaml files present in the source folder but absent from the expected CSV."""
    # build set of expected files
    expected_files = {
        _parse_line(line, expected_csv)[1]
        for line in read_data_lines(expected_csv)
    }

    # collect all folders in a set and test if there are multiple
    folders = {Path(f).parent for f in expected_files}
    if len(folders) != 1:
        sys.exit(
            f"error: '{expected_csv}' entries do not all share the same parent directory"
        )
    yaml_folder = src_root / folders.pop()

    if not yaml_folder.is_dir():
        sys.exit(f"error: source folder '{yaml_folder}' does not exist")

    # build set of existing files
    existing_files = {
        str(f.relative_to(src_root))
        for f in yaml_folder.iterdir()
        if f.is_file() and f.suffix == ".yaml"
    }
    return sorted(existing_files - expected_files)


def create_actual_hashes(expected_csv: Path, src_root: Path, actual_csv: Path) -> None:
    """Write actual-hashes CSV. Warns to stderr for unregistered yaml files."""
    # Build {relative_filename: tracking_status} from the expected CSV,
    # preserving order for a stable output.
    expected: dict[str, str] = {}
    for line in read_data_lines(expected_csv):
        tracking_status, filename = _parse_line(line, expected_csv)
        expected[filename] = tracking_status

    unregistered = find_unregistered_yaml(expected_csv, src_root)
    for unregistered_file in unregistered:
        print(
            f"warning: yaml file found in '{src_root / Path(unregistered_file).parent}' but not listed in '{expected_csv}': {unregistered_file}\n",
            file=sys.stderr,
        )

    # Find files listed in the expected CSV but absent from the folder.
    missing = sorted(f for f in expected if not (src_root / f).exists())
    if missing:
        sys.exit(
            f"error: files listed in '{expected_csv}' not found in '{src_root / Path(missing[0]).parent}':\n"
            + "\n".join(f"  {f}" for f in missing)
        )

    # Write the actual CSV in the same order as the expected CSV.
    output_lines = [
        f"{_sha256(src_root / filename) if tracking_status == 'TRACKING' else _ZERO_HASH}"
        f",{tracking_status},{filename}"
        for filename, tracking_status in expected.items()
    ]

    actual_csv.parent.mkdir(parents=True, exist_ok=True)
    actual_csv.write_text("\n".join(output_lines) + "\n", encoding="utf-8")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("expected_csv", type=Path, help="Input expected-hashes CSV")
    parser.add_argument("src_root",     type=Path, help="yaml file source folder")
    parser.add_argument("actual_csv",   type=Path, help="Output actual-hashes CSV")
    args = parser.parse_args()

    create_actual_hashes(args.expected_csv, args.src_root, args.actual_csv)


if __name__ == "__main__":
    main()
