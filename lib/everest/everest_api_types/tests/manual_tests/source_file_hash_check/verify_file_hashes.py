#!/usr/bin/env python3
"""
Compares expected vs actual hashes CSVs.
Prints mismatching lines to stdout and exits 1 if any differ, 0 otherwise.
"""

import argparse
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
from create_file_hashes import verify_hashes


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("expected_csv", type=Path, help="Expected hashes CSV")
    parser.add_argument("actual_csv",   type=Path, help="Actual hashes CSV")
    args = parser.parse_args()

    _matching, mismatching = verify_hashes(args.expected_csv, args.actual_csv)

    if mismatching:
        for expected, actual in mismatching:
            print(f"  actual:   {actual}")
            print(f"  expected: {expected}")
        sys.exit(1)


if __name__ == "__main__":
    main()
