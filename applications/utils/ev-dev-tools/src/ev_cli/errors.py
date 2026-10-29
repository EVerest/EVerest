# SPDX-License-Identifier: Apache-2.0
# Copyright Pionix GmbH and Contributors to EVerest
"""The exceptions ev-cli reports to its caller.

Everything the tool raises deliberately derives from :class:`EvCliError`, which
``cli.main`` turns into a message on stderr and a non-zero exit code.  Anything
else reaching the top level is a bug and keeps its traceback.
"""

from __future__ import annotations


class EvCliError(Exception):
    """Base class for every expected failure."""


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
