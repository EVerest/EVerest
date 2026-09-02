# SPDX-License-Identifier: Apache-2.0
# Copyright Pionix GmbH and Contributors to EVerest
"""A backend that writes the IR out as JSON.

This exists to keep the backend seam honest.  A single-backend abstraction is
indistinguishable from no abstraction: the only way to know that nothing about
C++ has leaked upwards is for something that is not the C++ generator to
consume the same IR.  It is also the cheapest way to look at what the loader
actually made of a definition, and it gives snapshot tests something stable to
compare without compiling anything.
"""

from __future__ import annotations

from pathlib import Path

from ...ir import json_io
from ...ir.model import Model
from ..base import GeneratedFile


class IrDumpBackend:
    """Serialises each definition's IR instead of generating code."""

    name = 'ir-dump'

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

    @staticmethod
    def _dump(node, path: Path, sourced) -> GeneratedFile:
        return GeneratedFile(
            path=path,
            content=json_io.dumps(node) + '\n',
            printable_name=path.name,
            source_mtime=sourced.source.file.stat().st_mtime,
        )
