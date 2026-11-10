# SPDX-License-Identifier: Apache-2.0
# Copyright Pionix GmbH and Contributors to EVerest
"""What every C++ view shares."""

#: Emitted into every generated header.  Bumped when the templates change in a
#: way that should invalidate already-generated output.
TEMPLATE_VERSION = '6'

#: Substituted for a property with no description.  This exact text sits in
#: the headers modules read today, so generated output keeps matching them.
MISSING_DESCRIPTION = 'TODO: description'
