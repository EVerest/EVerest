#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright Pionix GmbH and Contributors to EVerest
"""Smoke test for the generated EVerest API clients.

Run after the generated client wheels (and their deps) are installed. It
imports every generated command set the shell expects and instantiates the
interactive shell, which loads all CommandSets. This proves end to end that
generation from the real AsyncAPI specs, packaging, installation and the
shell <-> generated-client contract still work - so a spec or generator
change that breaks the clients fails here.

No MQTT broker is required: the shell and all command sets are constructed
without connecting (client_add, which connects, is not exercised).

Usage:
    smoke_test_clients.py [path/to/api_client.py]

If the path is omitted, api_client.py next to this script's parent is used.
"""

import importlib.util
import sys
from pathlib import Path


def main(argv=None):
    argv = list(sys.argv[1:] if argv is None else argv)
    if argv:
        api_client_path = Path(argv[0])
    else:
        api_client_path = Path(__file__).resolve().parents[1] / "api_client.py"

    if not api_client_path.is_file():
        print(f"error: api_client.py not found at {api_client_path}",
              file=sys.stderr)
        return 1

    # load the shell module (it is installed as a program, not a package)
    saved_argv = sys.argv
    sys.argv = [str(api_client_path)]
    try:
        spec = importlib.util.spec_from_file_location("api_client",
                                                      str(api_client_path))
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)
    finally:
        sys.argv = saved_argv

    expected = list(module.GENERATED_API_CLIENTS)

    # Importing each generated command set must not fail. load_generated_command
    # _sets() only warns on a missing package, so check imports explicitly here.
    failures = []
    for snake, camel in expected:
        module_name = f"{snake}.{camel}ClientCommandSet"
        try:
            importlib.import_module(module_name)
        except Exception as err:  # noqa: BLE001 - report any import failure
            failures.append(f"{module_name}: {err}")

    if failures:
        print("error: the following generated clients failed to import:",
              file=sys.stderr)
        for line in failures:
            print(f"  - {line}", file=sys.stderr)
        return 1

    # Instantiate the shell; this constructs every registered CommandSet.
    app = module.EVerestAPICmd()
    loaded = {type(cs).__name__ for cs in app._installed_command_sets}

    missing = []
    for snake, camel in expected:
        name = f"{camel}ClientCommandSet"
        if name not in loaded:
            missing.append(name)
    # the hand-written spy client is also expected
    if "SpyClientCommandSet" not in loaded:
        missing.append("SpyClientCommandSet")

    if missing:
        print("error: the shell did not load these command sets: "
              + ", ".join(missing), file=sys.stderr)
        return 1

    print(f"OK: shell started with {len(expected)} generated API clients "
          "plus the spy client")
    return 0


if __name__ == "__main__":
    sys.exit(main())
