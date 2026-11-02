#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright Pionix GmbH and Contributors to EVerest
"""Interactive client for the EVerest API modules.

The commands for every API are created at startup from the AsyncAPI specs in
docs/source/reference/EVerest_API (or --spec-dir / $EVEREST_API_SPEC_DIR).
Arguments after the options are run as commands, e.g.
    everest_api_client.py "run_script examples/ac_charging_session.script"
"""

import argparse
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from everest_api.shell import EVerestAPICmd  # noqa: E402
from everest_api.specs import SPEC_DIR_ENV, find_spec_dir, load_models  # noqa: E402


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--spec-dir", help=f"directory with the *_API.yaml specs (default: ${SPEC_DIR_ENV} "
                                           "or the specs of this source tree)")
    args, commands = parser.parse_known_args()

    spec_dir = find_spec_dir(args.spec_dir)
    if not spec_dir.is_dir():
        parser.error(f"spec directory not found: {spec_dir}")
    models = load_models(spec_dir)
    if not models:
        parser.error(f"no *_API.yaml specs found in {spec_dir}")

    sys.argv = [sys.argv[0]] + commands
    return EVerestAPICmd(models).cmdloop()


if __name__ == "__main__":
    sys.exit(main())
