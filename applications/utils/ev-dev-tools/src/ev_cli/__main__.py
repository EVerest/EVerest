# SPDX-License-Identifier: Apache-2.0
# Copyright Pionix GmbH and Contributors to EVerest
"""Entry point for ``python -m ev_cli`` and for the Bazel binary.

The import is absolute so that this file works both as a module and when run
directly as a script, which is how Bazel's py_binary invokes it.
"""

import sys

from ev_cli.cli import main

if __name__ == '__main__':
    sys.exit(main())
