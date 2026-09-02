# SPDX-License-Identifier: Apache-2.0
# Copyright Pionix GmbH and Contributors to EVerest
"""What a backend is.

A backend turns the IR into files.  It owns everything about its target: the
spelling of types, the naming conventions, the templates.  Nothing above this
line knows that C++ exists, which is what makes a second target -- Rust,
OpenAPI, documentation -- an addition rather than a second parser.
"""

from __future__ import annotations

from dataclasses import dataclass, field
from pathlib import Path
from typing import Protocol, runtime_checkable

from ..ir.model import Model


@dataclass
class GeneratedFile:
    """One file a backend produced, before anything is written to disk.

    Generation and writing are kept apart so that ``--diff`` can render what
    would change without touching the tree, and so that the write strategies
    stay in one place rather than in every backend.
    """

    #: Where it goes.
    path: Path
    content: str
    #: How it is named in log output; usually relative to the output directory.
    printable_name: str
    #: Newest modification time among the definitions this was built from.
    #: The ``update`` strategy compares it against the destination.
    source_mtime: float
    #: Templates involved, so a changed template forces regeneration.
    template_paths: tuple[Path, ...] = ()
    #: The ``--only`` filter key, for the files ``module create`` manages.
    abbr: str | None = None
    #: License text this file should start with, when the module declares one.
    license_header: str | None = None
    #: Which group this file belongs to for ``--only which`` output.
    category: str = 'core'


@runtime_checkable
class Backend(Protocol):
    """The interface every target implements."""

    name: str

    def templates(self) -> list[Path]:
        """Template files whose modification should force regeneration.

        This is what ``get-templates`` prints, and what EVerest's CMake stores
        as a target property to use as a dependency.
        """
        ...

    def emit_type_unit(self, model: Model, unit: str, output_dir: Path) -> list[GeneratedFile]:
        ...

    def emit_interface(self, model: Model, interface: str, output_dir: Path) -> list[GeneratedFile]:
        ...

    def emit_module_loader(self, model: Model, module: str, output_dir: Path) -> list[GeneratedFile]:
        ...


class BackendRegistry:
    """The backends this build of ev-cli knows about."""

    def __init__(self) -> None:
        self._factories: dict[str, callable] = {}

    def register(self, name: str, factory) -> None:
        self._factories[name] = factory

    def names(self) -> tuple[str, ...]:
        return tuple(sorted(self._factories))

    def create(self, name: str) -> Backend:
        try:
            factory = self._factories[name]
        except KeyError:
            raise KeyError(
                f'unknown backend {name!r}; available: {", ".join(self.names())}'
            ) from None
        return factory()


registry = BackendRegistry()


def _cpp_backend():
    from .cpp.backend import CppBackend
    return CppBackend()


def _ir_dump_backend():
    from .ir_dump.backend import IrDumpBackend
    return IrDumpBackend()


registry.register('cpp', _cpp_backend)
registry.register('ir-dump', _ir_dump_backend)

#: The target everything defaults to; EVerest generates C++.
DEFAULT_BACKEND = 'cpp'
