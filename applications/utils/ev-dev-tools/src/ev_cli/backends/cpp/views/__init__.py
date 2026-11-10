# SPDX-License-Identifier: Apache-2.0
# Copyright Pionix GmbH and Contributors to EVerest
"""What the C++ templates are handed.

The templates consume the IR directly, but every question about C++ -- how a
type is spelled, which conversion helper applies, what the include guard is --
is answered here rather than in Jinja.  That keeps the templates declarative
and, more usefully, keeps the answers in Python where they can be unit tested.
"""

from .implementation import ImplementationView
from .interface import InterfaceView, PayloadKind
from .module import ModuleView
from .type_unit import TypeUnitView

__all__ = [
    'ImplementationView',
    'InterfaceView',
    'ModuleView',
    'PayloadKind',
    'TypeUnitView',
]
