# SPDX-License-Identifier: Apache-2.0
# Copyright Pionix GmbH and Contributors to EVerest
"""What a backend is.

A backend turns the IR into files.  It owns everything about its target: the
spelling of types, the naming conventions, the templates.  Nothing above this
line knows that C++ exists, which is what makes a second target -- Rust,
OpenAPI, documentation -- an addition rather than a second parser.
"""

from __future__ import annotations

import argparse
from dataclasses import dataclass
from enum import Enum
from pathlib import Path
from collections.abc import Callable
from typing import Protocol, runtime_checkable

from ..ir.model import Model


class UpdatePolicy(Enum):
    """What ``module update`` may do to one of a backend's outputs.

    Declared by the backend rather than the command, because which outputs are
    safe to regenerate is a property of the target: for C++ the ``.cpp`` files
    are where a module's behaviour lives, and no command should have to know
    that.
    """

    #: Regenerate it, carrying any protected regions across.
    REGENERATE = 'regenerate'
    #: Only ever create it, never replace it.  This is what protects the
    #: sources a human owns.
    CREATE_IF_MISSING = 'create-if-missing'
    #: Leave it alone entirely, not even recreating it when absent.
    LEAVE_ALONE = 'leave-alone'


class OutputKind(Enum):
    """What a backend is asked to emit, one per ``emit_*`` method."""

    TYPE_UNIT = 'type-unit'
    INTERFACE = 'interface'
    MODULE_LOADER = 'module-loader'
    MODULE_FILES = 'module-files'


class FileCategory(Enum):
    """The groups ``--only which`` lists a module's files under, in this order."""

    CORE = 'core'
    INTERFACES = 'interfaces'
    DOCS = 'docs'


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
    only_key: str | None = None
    #: License text this file should start with, when the module declares one.
    license_header: str | None = None
    #: Which group this file belongs to for ``--only which`` output.
    category: FileCategory = FileCategory.CORE
    #: What ``module update`` may do to it if it already exists.
    update_policy: UpdatePolicy = UpdatePolicy.REGENERATE


@runtime_checkable
class Backend(Protocol):
    """The interface every target implements."""

    name: str
    #: Where each kind of output goes when ``--output-dir`` is not given,
    #: relative to the work directory; ``None`` makes ``--output-dir`` required.
    type_output_dir: str | None
    interface_output_dir: str | None
    loader_output_dir: str | None

    def __init__(self, args: argparse.Namespace | None = None) -> None:
        """Built from the parsed command line, or with every option at its default."""
        ...

    @staticmethod
    def options() -> argparse.ArgumentParser:
        """The command line options that belong to this target alone.

        Returned as an ``add_help=False`` parser so that it can be handed to
        ``add_parser(parents=...)``.  Every backend's options are attached to
        every emitting command, because the selected backend is only known
        once ``--backend`` has been parsed; an option belonging to a target
        that was not selected is simply inert.

        A backend prefixes its options with its name.  The two C++ ones are
        not, because the build passes them and their spelling is frozen.
        """
        ...

    def templates(self, kind: OutputKind) -> tuple[Path, ...]:
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

    def emit_module_files(
        self,
        model: Model,
        module: str,
        module_dir: Path,
        *,
        license_header: str,
        preserve_regions: bool,
    ) -> list[GeneratedFile]:
        """The scaffolding ``module create`` and ``module update`` manage.

        With ``preserve_regions`` the protected regions of files that already
        exist are carried over; otherwise every region starts as its placeholder.
        """
        ...


class BackendRegistry:
    """The backends this build of ev-cli knows about."""

    def __init__(self) -> None:
        self._factories: dict[str, Callable[[], type[Backend]]] = {}

    def register(self, name: str, factory: Callable[[], type[Backend]]) -> None:
        self._factories[name] = factory

    def names(self) -> tuple[str, ...]:
        return tuple(sorted(self._factories))

    def backend_class(self, name: str) -> type[Backend]:
        """The backend's class, imported on demand.

        Needed as well as :meth:`create` because a backend's options have to
        be collected before anything has been parsed, and therefore before any
        instance can be built.
        """
        try:
            importer = self._factories[name]
        except KeyError:
            raise KeyError(
                f'unknown backend {name!r}; available: {", ".join(self.names())}'
            ) from None
        return importer()

    def create(self, name: str, args=None) -> Backend:
        """Build a backend, handing it the parsed arguments.

        A backend that declares its own options has to be able to read them,
        so construction takes the namespace rather than nothing.
        """
        return self.backend_class(name)(args)

    def options(self) -> list[argparse.ArgumentParser]:
        """Every backend's own option group, for ``add_parser(parents=...)``."""
        return [self.backend_class(name).options() for name in self.names()]


registry = BackendRegistry()


def _cpp_backend():
    from .cpp.backend import CppBackend
    return CppBackend


def _ir_dump_backend():
    from .ir_dump.backend import IrDumpBackend
    return IrDumpBackend


registry.register('cpp', _cpp_backend)
registry.register('ir-dump', _ir_dump_backend)

#: The target everything defaults to; EVerest generates C++.
DEFAULT_BACKEND = 'cpp'
