# SPDX-License-Identifier: Apache-2.0
# Copyright Pionix GmbH and Contributors to EVerest
"""A backend that writes the IR out as JSON.

This exists to keep the backend seam honest.  A single-backend abstraction is
indistinguishable from no abstraction: the only way to know that nothing about
C++ has leaked upwards is for something that is not the C++ generator to
consume the same IR.  It is also the cheapest way to look at what the loader
actually made of a definition.
"""

from __future__ import annotations

import argparse
import json
from dataclasses import fields, is_dataclass
from enum import Enum
from pathlib import Path
from typing import Any, Mapping

from ...ir.common import Constraints
from ...ir.model import Model
from ...ir.types import Array, LocalEnum, LocalStruct, Opaque, Primitive, Ref, Variant
from ...schema.references import TypeRef
from ...schema.source import SourceRef
from ..base import GeneratedFile

#: A field's type can be any of these, so each is written with a ``kind``.
_TYPE_EXPRESSIONS = (Array, LocalEnum, LocalStruct, Opaque, Primitive, Ref, Variant)


class IrDumpBackend:
    """Serialises each definition's IR instead of generating code."""

    name = 'ir-dump'
    type_output_dir = None
    interface_output_dir = None
    loader_output_dir = None

    @staticmethod
    def options() -> argparse.ArgumentParser:
        parser = argparse.ArgumentParser(add_help=False)
        group = parser.add_argument_group('ir-dump backend')
        group.add_argument(
            '--ir-indent', type=int, default=2,
            help='indentation of the emitted JSON, 0 for one line (default: 2)')
        return parser

    def __init__(self, args=None) -> None:
        indent = getattr(args, 'ir_indent', 2)
        self._indent = indent if indent else None
        everest_dirs = getattr(args, 'everest_dir', None) or ()
        work_dir = getattr(args, 'work_dir', None)
        #: Source files are written relative to these, so a dump does not
        #: depend on where the checkout lives.
        self._roots = tuple(
            Path(root).resolve() for root in (*everest_dirs, *([work_dir] if work_dir else []))
        )

    def postprocess(self, files: list[GeneratedFile]) -> None:
        """Nothing to do: JSON comes out of the serialiser already formatted."""

    def templates(self, scope: str = 'all') -> list[Path]:
        return []

    def emit_type_unit(self, model: Model, unit: str, output_dir: Path) -> list[GeneratedFile]:
        type_unit = model.types[unit]
        return [self._dump(type_unit, output_dir / f'{unit}.json', type_unit)]

    def emit_interface(self, model: Model, interface: str, output_dir: Path) -> list[GeneratedFile]:
        definition = model.interfaces[interface]
        return [self._dump(definition, output_dir / f'{interface}.json', definition)]

    def emit_module_loader(self, model: Model, module: str, output_dir: Path) -> list[GeneratedFile]:
        name = module.rpartition('/')[2]
        definition = model.modules[module]
        return [self._dump(definition, output_dir / name / 'module.json', definition)]

    def emit_module_files(
        self,
        model: Model,
        module: str,
        module_dir: Path,
        *,
        license_header: str,
        read_blocks,
    ) -> list[GeneratedFile]:
        """The module together with the interfaces it implements.

        That pairing is exactly what ``module create`` consumes over and above
        ``generate-loader``, so it is what this writes out.  A license header
        and protected regions mean nothing to a JSON dump, so both arguments
        are accepted and ignored.
        """
        definition = model.modules[module]
        payload = {
            'module': definition,
            'interfaces': {
                provided.interface: model.interfaces[provided.interface]
                for provided in definition.provides
            },
        }
        path = module_dir / f'{definition.name}.ir.json'
        return [GeneratedFile(
            path=path,
            content=self._render(payload),
            printable_name=path.relative_to(module_dir).as_posix(),
            source_mtime=definition.source.file.stat().st_mtime,
            abbr='module.json',
            category='core',
        )]

    def _dump(self, node, path: Path, sourced) -> GeneratedFile:
        return GeneratedFile(
            path=path,
            content=self._render(node),
            printable_name=path.name,
            source_mtime=sourced.source.file.stat().st_mtime,
        )

    def _render(self, value: Any) -> str:
        return json.dumps(self._to_json(value), indent=self._indent) + '\n'

    def _to_json(self, value: Any) -> Any:
        if isinstance(value, SourceRef):
            return self._relative_source(value)
        if isinstance(value, TypeRef):
            return str(value)
        if isinstance(value, Constraints):
            return self._to_json(value.as_dict())
        if is_dataclass(value):
            kind = {'kind': type(value).__name__} if isinstance(value, _TYPE_EXPRESSIONS) else {}
            return kind | {f.name: self._to_json(getattr(value, f.name)) for f in fields(value)}
        if isinstance(value, Enum):
            return value.value
        if isinstance(value, frozenset):
            return sorted(self._to_json(item) for item in value)
        if isinstance(value, (tuple, list)):
            return [self._to_json(item) for item in value]
        if isinstance(value, Mapping):
            return {str(key): self._to_json(item) for key, item in value.items()}
        return value

    def _relative_source(self, source: SourceRef) -> str:
        file = source.file
        root = next((root for root in self._roots if file.is_relative_to(root)), None)
        name = (file.relative_to(root) if root else file).as_posix()
        return f'{name}#{source.pointer}' if source.pointer else name
