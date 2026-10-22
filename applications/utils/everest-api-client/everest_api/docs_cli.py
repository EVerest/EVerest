# SPDX-License-Identifier: Apache-2.0
# Copyright Pionix GmbH and Contributors to EVerest
"""Command line entry point for the EVerest API HTML reference generator."""

import argparse
import sys

from everest_api.docs_generator import generate_reference
from everest_api.spec_loader import SpecError


def main(argv=None):
    parser = argparse.ArgumentParser(
        prog="python -m everest_api.docs_cli",
        description="Render the HTML reference of the EVerest AsyncAPI specs: one "
                    "page per spec, the shared stylesheet and script, and the RST "
                    "index page listing every API.")
    parser.add_argument("--spec", nargs="+", action="extend", required=True, metavar="FILE",
                        help="AsyncAPI YAML specs of the reference (all in one directory)")
    parser.add_argument("--html-out", required=True, metavar="DIR",
                        help="directory receiving <name>/index.html per spec and _assets/")
    parser.add_argument("--index-out", required=True, metavar="FILE",
                        help="path of the RST index page")

    args = parser.parse_args(argv)
    try:
        generate_reference(args.spec, args.html_out, args.index_out)
    except SpecError as err:
        print(f"error: {err}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
