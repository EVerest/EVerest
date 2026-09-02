# SPDX-License-Identifier: Apache-2.0
# Copyright Pionix GmbH and Contributors to EVerest
"""Parsing the two reference dialects into one resolved reference.

EVerest has historically written cross-type references as ``/energy#/Power``.
That string is a legal URI reference, but its *resolution* is not: EVerest's
loader looks the path up in a list of ``--everest-dir`` roots rather than
against a base URI, and the fragment ``/Power`` addresses the contents of the
document's ``types:`` key rather than the document itself.  Neither is
something a standard JSON Schema or OpenAPI tool can follow.

The conformant spelling of the same thing addresses the document as it really
is -- ``#/types/Power`` within a file, ``energy.yaml#/types/Power`` across
files.  Both dialects are accepted and produce the same :class:`TypeRef`, so
the sources can migrate a file at a time without any consumer noticing.
"""

from __future__ import annotations

import posixpath
import re
from dataclasses import dataclass
from enum import Enum

from ..errors import ReferenceError as EvReferenceError
from ..ir.types import TypeRef

#: A single JSON pointer segment naming a type.
_NAME = r'[A-Za-z_][A-Za-z0-9_.-]*'
#: ``/<unit>#/<Type>``, where the unit may itself be a nested path.
_LEGACY = re.compile(rf'^/(?P<unit>{_NAME}(?:/{_NAME})*)#/(?P<name>{_NAME})$')
#: ``[<path>.yaml]#/types/<Type>``
_CONFORMANT = re.compile(rf'^(?P<path>[^#]*)#/types/(?P<name>{_NAME})$')

_ERROR_PREFIX = '/errors/'
_ERROR_REFERENCE = re.compile(
    rf'^{re.escape(_ERROR_PREFIX)}(?P<namespace>[a-z][a-zA-Z0-9_]*)'
    rf'(?:#/(?P<name>[A-Z][A-Za-z0-9]*))?$'
)

YAML_SUFFIXES = ('.yaml', '.yml')


class Dialect(Enum):
    """Which spelling a reference used."""

    #: ``/energy#/Power`` -- resolvable only through EVerest's own loader.
    LEGACY = 'legacy'
    #: ``energy.yaml#/types/Power`` -- resolvable by any JSON Schema tool.
    CONFORMANT = 'conformant'


@dataclass(frozen=True)
class ParsedReference:
    target: TypeRef
    dialect: Dialect
    raw: str

    @property
    def is_legacy(self) -> bool:
        return self.dialect is Dialect.LEGACY


@dataclass(frozen=True)
class ParsedErrorReference:
    """``/errors/<namespace>`` or ``/errors/<namespace>#/<Name>``.

    Error references are not a JSON Schema construct at all -- they live under
    a ``reference:`` key rather than ``$ref`` -- so there is no conformant
    spelling to migrate towards and no diagnostic to raise.
    """

    namespace: str
    #: ``None`` means "every error declared in that namespace".
    name: str | None
    raw: str

    @property
    def is_whole_namespace(self) -> bool:
        return self.name is None


def parse_type_reference(raw: str, *, from_unit: str | None) -> ParsedReference:
    """Parse either dialect.

    ``from_unit`` is the type unit doing the referring, as a ``/``-separated
    path without its suffix (``energy``, or ``sub/energy`` for a nested file).
    It is needed because a conformant reference is relative to the referring
    file.  Pass ``None`` when the referring document is not itself a type unit
    -- an interface or a manifest -- in which case a conformant reference is
    read relative to the types root and must name a file.
    """
    if not isinstance(raw, str):
        raise EvReferenceError(f'reference must be a string, got {type(raw).__name__}')

    legacy = _LEGACY.match(raw)
    if legacy:
        return ParsedReference(
            target=TypeRef(unit=legacy.group('unit'), name=legacy.group('name')),
            dialect=Dialect.LEGACY,
            raw=raw,
        )

    conformant = _CONFORMANT.match(raw)
    if conformant:
        path = conformant.group('path')
        unit = _resolve_relative_unit(path, from_unit=from_unit, raw=raw)
        return ParsedReference(
            target=TypeRef(unit=unit, name=conformant.group('name')),
            dialect=Dialect.CONFORMANT,
            raw=raw,
        )

    raise EvReferenceError(
        f'cannot parse type reference {raw!r}; expected either '
        f'"/<unit>#/<TypeName>" or "[<file>.yaml]#/types/<TypeName>"'
    )


def _resolve_relative_unit(path: str, *, from_unit: str | None, raw: str) -> str:
    """Turn the file part of a conformant reference into a unit name."""
    if not path:
        if from_unit is None:
            raise EvReferenceError(
                f'reference {raw!r} has no file part, but the referring '
                f'document is not a type unit and so declares no types of its own'
            )
        return from_unit

    if not path.endswith(YAML_SUFFIXES):
        raise EvReferenceError(
            f'reference {raw!r} must point at a .yaml file so that standard '
            f'tools can resolve it'
        )

    stem = path
    for suffix in YAML_SUFFIXES:
        if stem.endswith(suffix):
            stem = stem[: -len(suffix)]
            break

    if stem.startswith('/'):
        return stem.lstrip('/')

    base = posixpath.dirname(from_unit) if from_unit is not None else ''
    unit = posixpath.normpath(posixpath.join(base, stem))
    if unit.startswith('..'):
        raise EvReferenceError(
            f'reference {raw!r} points outside the types directory'
        )
    return unit


def conformant_form(target: TypeRef, *, from_unit: str | None) -> str:
    """How ``target`` should be written from ``from_unit`` once migrated.

    This is what the conformance report suggests, so that the eventual cleanup
    is a mechanical rewrite rather than a judgement call at 512 sites.
    """
    if from_unit is None:
        return f'{target.unit}.yaml#/types/{target.name}'

    if target.unit == from_unit:
        return f'#/types/{target.name}'

    base = posixpath.dirname(from_unit)
    relative = posixpath.relpath(f'{target.unit}.yaml', base or '.')
    return f'{relative}#/types/{target.name}'


def parse_error_reference(raw: str) -> ParsedErrorReference:
    match = _ERROR_REFERENCE.match(raw) if isinstance(raw, str) else None
    if not match:
        raise EvReferenceError(
            f'cannot parse error reference {raw!r}; expected '
            f'"/errors/<namespace>" or "/errors/<namespace>#/<ErrorName>"'
        )
    return ParsedErrorReference(
        namespace=match.group('namespace'),
        name=match.group('name'),
        raw=raw,
    )
