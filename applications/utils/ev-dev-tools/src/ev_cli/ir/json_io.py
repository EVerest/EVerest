# SPDX-License-Identifier: Apache-2.0
# Copyright Pionix GmbH and Contributors to EVerest
"""Serialising the IR to JSON and back.

Two things want this.  A backend that writes the IR out is the cheapest
possible proof that the backend seam is real rather than notional -- if the IR
can be handed to something that is not the C++ generator, then the C++
generator is not secretly the only consumer.  And a round trip is a compact
test that every node is fully described by its fields, with nothing important
living in a closure or a cache.
"""

from __future__ import annotations

import json
from dataclasses import fields, is_dataclass
from enum import Enum
from pathlib import Path
from typing import Any, TypeVar

from . import interfaces as interfaces_ir
from . import modules as modules_ir
from . import types as types_ir
from .common import Constraints, SourceRef
from .model import Model

#: Every class the IR can contain, by the tag used in the serialised form.
#: Union members must be tagged because their fields alone do not identify them.
_CLASSES: dict[str, type] = {
    cls.__name__: cls
    for cls in (
        SourceRef, Constraints,
        types_ir.TypeRef, types_ir.Primitive, types_ir.Array, types_ir.Ref,
        types_ir.LocalEnum, types_ir.LocalStruct, types_ir.Opaque, types_ir.Variant,
        types_ir.Field, types_ir.StructDef, types_ir.EnumDef, types_ir.TypeUnit,
        interfaces_ir.VarDef, interfaces_ir.ArgDef, interfaces_ir.CmdDef,
        interfaces_ir.ErrorDef, interfaces_ir.InterfaceDef,
        modules_ir.ConfigEntry, modules_ir.ProvidesDef, modules_ir.RequiresDef,
        modules_ir.ModuleMetadata, modules_ir.ModuleDef,
        Model,
    )
}

_ENUMS: dict[str, type[Enum]] = {
    cls.__name__: cls
    for cls in (types_ir.JsonType, modules_ir.Mutability)
}

T = TypeVar('T')


def to_jsonable(value: Any) -> Any:
    """Convert an IR value into something :mod:`json` can write."""
    if is_dataclass(value) and not isinstance(value, type):
        payload: dict[str, Any] = {'_type': type(value).__name__}
        for field in fields(value):
            payload[field.name] = to_jsonable(getattr(value, field.name))
        return payload
    if isinstance(value, Enum):
        return {'_enum': type(value).__name__, 'value': value.value}
    if isinstance(value, Path):
        return {'_path': str(value)}
    if isinstance(value, frozenset):
        return {'_frozenset': sorted(to_jsonable(v) for v in value)}
    if isinstance(value, tuple):
        return {'_tuple': [to_jsonable(v) for v in value]}
    if isinstance(value, list):
        return [to_jsonable(v) for v in value]
    if isinstance(value, dict):
        return {str(k): to_jsonable(v) for k, v in value.items()}
    return value


def from_jsonable(value: Any) -> Any:
    """Rebuild an IR value written by :func:`to_jsonable`."""
    if isinstance(value, list):
        return [from_jsonable(v) for v in value]
    if not isinstance(value, dict):
        return value

    if '_type' in value:
        cls = _CLASSES[value['_type']]
        kwargs = {
            field.name: from_jsonable(value[field.name])
            for field in fields(cls) if field.name in value
        }
        return cls(**kwargs)
    if '_enum' in value:
        return _ENUMS[value['_enum']](value['value'])
    if '_path' in value:
        return Path(value['_path'])
    if '_frozenset' in value:
        return frozenset(from_jsonable(v) for v in value['_frozenset'])
    if '_tuple' in value:
        return tuple(from_jsonable(v) for v in value['_tuple'])
    return {k: from_jsonable(v) for k, v in value.items()}


def dumps(value: Any, *, indent: int | None = 2) -> str:
    return json.dumps(to_jsonable(value), indent=indent, sort_keys=False)


def loads(text: str) -> Any:
    return from_jsonable(json.loads(text))


def round_trip(value: T) -> T:
    """Serialise and rebuild, for tests."""
    return loads(dumps(value))
