# SPDX-License-Identifier: Apache-2.0
# Copyright Pionix GmbH and Contributors to EVerest
"""What ev-cli reports to its caller.

Everything the tool raises deliberately derives from :class:`EvCliError`, which
``cli.main`` turns into a message on stderr and a non-zero exit code.  Anything
else reaching the top level is a bug and keeps its traceback.  A problem that
does not stop the run is reported with :func:`warn`.
"""

from __future__ import annotations

import sys


def warn(message: str) -> None:
    print(f'ev-cli: warning: {message}', file=sys.stderr)


class EvCliError(Exception):
    """Base class for every expected failure."""


class EverestDirNotFound(EvCliError):
    """An everest directory to read definitions from is missing.

    Raised before anything is looked up, so a mistyped ``--everest-dir`` is
    reported as such rather than as every definition in it going missing.
    """


class WorkDirNotFound(EvCliError):
    """The work directory is missing.

    Raised before anything is written, since output defaults to a directory
    beneath it and would otherwise be created there.
    """


class DefinitionNotFound(EvCliError):
    """A definition file could not be found in any of the everest directories."""


class MalformedReference(EvCliError):
    """A ``$ref`` or error reference could not be parsed.

    Only about the shape of the string: nothing was looked up.  A reference
    that parses but names something that does not exist is a
    :class:`DefinitionNotFound` instead.
    """


class SchemaError(EvCliError):
    """A definition file is not valid against its meta-schema."""


class UnsupportedDefinition(EvCliError):
    """A definition is valid but says something the generator cannot express."""


class BlockError(EvCliError):
    """A generated file's ``ev@<uuid>`` markers are malformed."""


class LicenseNotFound(EvCliError):
    """No license text could be found for a module's declared license."""
