# SPDX-License-Identifier: Apache-2.0
# Copyright Pionix GmbH and Contributors to EVerest
"""The direction of the dependencies between ev-cli's packages."""

from __future__ import annotations

import subprocess
import sys


def test_the_schema_layer_does_not_depend_on_the_ir():
    """The IR is built from the schema layer, never the other way round.

    Checked in a fresh interpreter, since this one has imported the IR already.
    """
    code = (
        'import importlib, pkgutil, sys\n'
        'import ev_cli.schema as schema\n'
        'for module in pkgutil.iter_modules(schema.__path__):\n'
        '    importlib.import_module(f"ev_cli.schema.{module.name}")\n'
        'print(" ".join(sorted(n for n in sys.modules if n.startswith("ev_cli.ir"))))\n'
    )
    loaded = subprocess.run(
        [sys.executable, '-c', code], capture_output=True, encoding='utf-8', check=True,
    ).stdout.split()
    assert loaded == []
