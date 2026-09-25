# -*- coding: utf-8 -*-
#
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Pionix GmbH and Contributors to EVerest
#
"""
Turn the telemetry section of a module manifest into template data for the generated telemetry handles.
"""

from pathlib import Path
from typing import Dict, List, Optional, Tuple

import stringcase

from ev_cli import helpers
from ev_cli.type_parsing import TypeParser

HANDLES = {
    'gauge': 'Gauge',
    'counter': 'Counter',
    'state': 'State',
    'event': 'Event',
}

NUMERIC_TYPES = {
    'number': 'double',
    'integer': 'std::int64_t',
}

# element names become members of the generated tel::Elements struct
RESERVED_NAMES = {'m_telemetry', 'Elements'}

CPP_KEYWORDS = {
    'alignas', 'alignof', 'and', 'and_eq', 'asm', 'auto', 'bitand', 'bitor', 'bool', 'break', 'case', 'catch',
    'char', 'char8_t', 'char16_t', 'char32_t', 'class', 'compl', 'concept', 'const', 'consteval', 'constexpr',
    'constinit', 'const_cast', 'continue', 'co_await', 'co_return', 'co_yield', 'decltype', 'default', 'delete',
    'do', 'double', 'dynamic_cast', 'else', 'enum', 'explicit', 'export', 'extern', 'false', 'float', 'for',
    'friend', 'goto', 'if', 'inline', 'int', 'long', 'mutable', 'namespace', 'new', 'noexcept', 'not', 'not_eq',
    'nullptr', 'operator', 'or', 'or_eq', 'private', 'protected', 'public', 'register', 'reinterpret_cast',
    'requires', 'return', 'short', 'signed', 'sizeof', 'static', 'static_assert', 'static_cast', 'struct',
    'switch', 'template', 'this', 'thread_local', 'throw', 'true', 'try', 'typedef', 'typeid', 'typename',
    'union', 'unsigned', 'using', 'virtual', 'void', 'volatile', 'wchar_t', 'while', 'xor', 'xor_eq',
}


def _resolve_ref(where: str, type_url: str, json_type: str, require_enum: bool) -> Tuple[str, str]:
    """Return the fully qualified C++ type and the header of a referenced type."""
    try:
        type_dict = TypeParser.parse_type_url(type_url)
        TypeParser.does_type_exist(type_url=type_url, json_type=json_type)
    except (Exception, helpers.EVerestParsingException) as e:
        raise helpers.EVerestParsingException(f'{where}: {e}') from e

    if require_enum:
        type_path = helpers.resolve_everest_dir_path('types' / type_dict['type_relative_path'].with_suffix('.yaml'))
        type_schema = TypeParser.validated_type_defs[type_path]['types'][type_dict['type_name']]
        if 'enum' not in type_schema:
            raise helpers.EVerestParsingException(
                f'{where}: kind "state" requires $ref to a string enum, but {type_url} has no enum values')

    header = (Path('generated/types') / type_dict['type_relative_path'].with_suffix('.hpp')).as_posix()
    return f'::{type_dict["namespaced_type"]}', header


def _check_identifier(where: str, name: str, what: str):
    if name in CPP_KEYWORDS:
        raise helpers.EVerestParsingException(f'{where}: {what} "{name}" is a C++ keyword')


def parse_module_telemetry(module_name: str, module_def: Dict) -> Dict:
    """Template data for the generated telemetry handles of a module.

    Returns a dict with 'elements' (one entry per telemetry element, in manifest order), 'type_headers' (headers
    of referenced types) and 'inline_enums' (enums declared inline on state elements).
    """
    elements: List[Dict] = []
    type_headers = set()
    inline_enums: List[Dict] = []

    for name, declaration in module_def.get('telemetry', {}).items():
        where = f'{module_name}/manifest.yaml: telemetry.{name}'
        _check_identifier(where, name, 'element name')
        if name in RESERVED_NAMES:
            raise helpers.EVerestParsingException(f'{where}: element name "{name}" is reserved')

        kind = declaration['kind']
        json_type = declaration['type']
        header: Optional[str] = None

        if kind in ('gauge', 'counter'):
            cpp_type = NUMERIC_TYPES[json_type]
        elif kind == 'state':
            if json_type == 'boolean':
                cpp_type = 'bool'
            elif 'enum' in declaration:
                enum_type = stringcase.pascalcase(name)
                for value in declaration['enum']:
                    _check_identifier(where, value, 'enum value')
                inline_enums.append({
                    'enum_type': enum_type,
                    'enum': declaration['enum'],
                    'description': declaration['description'],
                })
                cpp_type = f'tel::types::{enum_type}'
            elif '$ref' in declaration:
                cpp_type, header = _resolve_ref(where, declaration['$ref'], 'string', require_enum=True)
            else:
                cpp_type = 'std::string'
        elif kind == 'event':
            cpp_type, header = _resolve_ref(where, declaration['$ref'], 'object', require_enum=False)
        else:
            raise helpers.EVerestParsingException(f'{where}: unknown kind "{kind}"')

        if header:
            type_headers.add(header)

        elements.append({
            'name': name,
            'kind': kind,
            'handle': HANDLES[kind],
            'cpp_type': cpp_type,
            'description': declaration['description'],
        })

    return {
        'elements': elements,
        'type_headers': sorted(type_headers),
        'inline_enums': inline_enums,
    }
