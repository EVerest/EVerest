# SPDX-License-Identifier: Apache-2.0
# Copyright Pionix GmbH and Contributors to EVerest
"""Building modules from their manifests."""

from __future__ import annotations

from collections.abc import Iterable, Mapping
from typing import Any

from ...errors import UnsupportedDefinition
from ...schema.loader import DefinitionLoader
from ...schema.source import SourceRef
from ..common import Constraints
from ..modules import (
    ConfigEntry,
    ModuleDef,
    ModuleMetadata,
    Mutability,
    ProvidesDef,
    RequiresDef,
)
from .common import read_json_type, reject_non_identifier


def build_modules(loader: DefinitionLoader, names: Iterable[str]) -> dict[str, ModuleDef]:
    return {name: build_module(loader, name) for name in names}


def build_module(loader: DefinitionLoader, relative_module_dir: str) -> ModuleDef:
    document = loader.module_document(relative_module_dir)
    data = document.data
    source = document.source

    return ModuleDef(
        # Modules are addressed by a path but named by its last segment.
        name=relative_module_dir.rpartition('/')[2],
        description=data['description'],
        metadata=_build_metadata(data['metadata']),
        config=_build_config(data.get('config'), source.child('config')),
        provides=tuple(
            _build_provides(name, schema, source.child('provides', name))
            for name, schema in (data.get('provides') or {}).items()
        ),
        requires=tuple(
            _build_requires(name, schema, source.child('requires', name))
            for name, schema in (data.get('requires') or {}).items()
        ),
        source=source,
        enable_external_mqtt=data.get('enable_external_mqtt', False),
        enable_telemetry=data.get('enable_telemetry', False),
        enable_global_errors=data.get('enable_global_errors', False),
        capabilities=tuple(data.get('capabilities') or ()),
    )


def _build_metadata(data: Mapping[str, Any]) -> ModuleMetadata:
    known = {'license', 'base_license', 'authors'}
    return ModuleMetadata(
        license=data['license'],
        authors=tuple(data['authors']),
        base_license=data.get('base_license'),
        extra={k: v for k, v in data.items() if k not in known},
    )


def _build_config(entries: Mapping[str, Any] | None, source: SourceRef) -> tuple[ConfigEntry, ...]:
    return tuple(
        _build_config_entry(name, schema, source.child(name))
        for name, schema in (entries or {}).items()
    )


def _build_config_entry(name: str, schema: Mapping[str, Any], source: SourceRef) -> ConfigEntry:
    reject_non_identifier(name, source, 'configuration name')
    return ConfigEntry(
        name=name,
        description=schema['description'],
        type=read_json_type(schema['type'], source),
        source=source,
        mutability=_read_mutability(schema, source),
        constraints=Constraints.from_schema(schema),
    )


def _read_mutability(schema: Mapping[str, Any], source: SourceRef) -> Mutability:
    """Read the declared mutability, defaulting as the meta-schema does.

    A value the manifest spells wrongly is an error rather than silently
    the default.
    """
    declared = schema.get('mutability', Mutability.READ_ONLY.value)
    try:
        return Mutability(declared)
    except ValueError:
        raise UnsupportedDefinition(
            f'{source}: unknown mutability {declared!r}'
        ) from None


def _build_provides(name: str, schema: Mapping[str, Any], source: SourceRef) -> ProvidesDef:
    reject_non_identifier(name, source, 'implementation id')
    known = {'description', 'interface', 'config'}
    return ProvidesDef(
        id=name,
        interface=schema['interface'],
        description=schema['description'],
        config=_build_config(schema.get('config'), source.child('config')),
        source=source,
        # The manifest allows further primitive properties here, which the
        # framework matches requirements against.  Code generation ignores
        # them; they are carried so the IR does not lose them.
        extra={k: v for k, v in schema.items() if k not in known},
    )


def _build_requires(name: str, schema: Mapping[str, Any], source: SourceRef) -> RequiresDef:
    reject_non_identifier(name, source, 'requirement id')
    ignore = schema.get('ignore') or {}
    ignored_vars = ignore.get('vars', ())
    if isinstance(ignored_vars, str):
        ignored_vars = (ignored_vars,)

    return RequiresDef(
        id=name,
        interface=schema['interface'],
        source=source,
        min_connections=schema.get('min_connections', 1),
        max_connections=schema.get('max_connections', 1),
        ignore_vars=tuple(ignored_vars),
        ignore_errors=ignore.get('errors', False),
    )
