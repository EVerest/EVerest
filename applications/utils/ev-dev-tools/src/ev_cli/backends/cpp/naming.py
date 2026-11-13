# SPDX-License-Identifier: Apache-2.0
# Copyright Pionix GmbH and Contributors to EVerest
"""The naming conventions of the generated C++.

Every name here appears in a header that everest-core's modules include, so
these are part of the generated API rather than an internal detail: changing
one is a source-breaking change for every implementer.
"""

from __future__ import annotations

from itertools import pairwise

from ...errors import UnsupportedDefinition

#: Reserved words that cannot be used as identifiers in generated C++.
#:
#: Whether a name is a legal identifier at all is checked when the IR is built,
#: since that applies to any target.  These matter only for the names generated
#: code uses on their own: struct members, command arguments and configuration
#: entries.  Variable and command names are always part of a longer identifier,
#: such as ``handle_delete``.
CPP_KEYWORDS = frozenset({
    'alignas', 'alignof', 'and', 'and_eq', 'asm', 'auto', 'bitand', 'bitor',
    'bool', 'break', 'case', 'catch', 'char', 'char8_t', 'char16_t',
    'char32_t', 'class', 'compl', 'concept', 'const', 'consteval',
    'constexpr', 'constinit', 'const_cast', 'continue', 'co_await',
    'co_return', 'co_yield', 'decltype', 'default', 'delete', 'do', 'double',
    'dynamic_cast', 'else', 'enum', 'explicit', 'export', 'extern', 'false',
    'float', 'for', 'friend', 'goto', 'if', 'inline', 'int', 'long',
    'mutable', 'namespace', 'new', 'noexcept', 'not', 'not_eq', 'nullptr',
    'operator', 'or', 'or_eq', 'private', 'protected', 'public', 'register',
    'reinterpret_cast', 'requires', 'return', 'short', 'signed', 'sizeof',
    'static', 'static_assert', 'static_cast', 'struct', 'switch', 'template',
    'this', 'thread_local', 'throw', 'true', 'try', 'typedef', 'typeid',
    'typename', 'union', 'unsigned', 'using', 'virtual', 'void', 'volatile',
    'wchar_t', 'while', 'xor', 'xor_eq',
})


def snake_case(word: str) -> str:
    """Convert a PascalCase or mixed name to snake_case.

    An underscore is inserted between a lower-case letter and an upper-case
    one, non-alphanumeric characters become underscores, and everything is
    lower-cased.  The generated enum helper names are built from this, so its
    output is part of the generated API: ``ChargingPhase`` has to keep yielding
    ``charging_phase_to_string_view``.
    """
    if not word:
        return ''

    if not word[0].isalnum():
        raise UnsupportedDefinition(
            f'cannot build an identifier from {word!r}: it does not start with '
            f'an alphanumeric character'
        )

    out = [word[0].lower()]
    for previous, current in pairwise(word):
        if previous.isalpha() and previous.islower() and current.isupper():
            out.append('_')
        out.append(current.lower() if current.isalnum() else '_')
    return ''.join(out)


def check_identifier(name: str, where: str) -> str:
    """Reject a name C++ will not accept as an identifier."""
    if name in CPP_KEYWORDS:
        raise UnsupportedDefinition(
            f'{where}: "{name}" is a C++ keyword and cannot be used as an '
            f'identifier in generated code'
        )
    return name


def unit_namespaces(unit: str) -> list[str]:
    """The namespaces a type unit's declarations live in."""
    return ['types', unit]


def qualified_type(unit: str, name: str) -> str:
    """A type's fully qualified name, as referenced from another unit.

    Same-unit references are qualified too, which keeps the spelling of a
    reference independent of where it is used.
    """
    return f'types::{unit}::{name}'


def type_header_guard(unit: str) -> str:
    return f'TYPES_{snake_case(unit[:1].upper() + unit[1:]).upper()}_TYPES_HPP'


def interface_header_guard(interface: str, filename: str) -> str:
    """``Types.hpp`` of ``isolation_monitor`` -> ``ISOLATION_MONITOR_TYPES_HPP``."""
    return f'{snake_case(interface).upper()}_{filename.replace(".", "_").upper()}'


def implementation_header_guard(implementation_id: str, interface: str) -> str:
    return f'{snake_case(f"{implementation_id}_{interface}").upper()}_IMPL_HPP'


def module_header_guard(module: str) -> str:
    return f'{snake_case(module).upper()}_HPP'


def type_header_include(unit: str) -> str:
    return f'generated/types/{unit}.hpp'
