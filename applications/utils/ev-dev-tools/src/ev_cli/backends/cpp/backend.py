# SPDX-License-Identifier: Apache-2.0
# Copyright Pionix GmbH and Contributors to EVerest
"""The C++ backend.

Everything that knows C++ lives under this package: the type mapping, the
naming conventions and the templates.  The templates render from the IR
directly, through the views in :mod:`.views`.
"""

from __future__ import annotations

import argparse
import shutil
import subprocess
from enum import Enum
from pathlib import Path

import jinja2

from ... import blocks
from ...errors import EvCliError
from ...ir.model import Model
from ..base import FileCategory, GeneratedFile, OutputKind, UpdatePolicy
from . import naming
from .views import (
    PayloadKind,
    ImplementationView,
    InterfaceView,
    ModuleView,
    TypeUnitView,
)
from .views.common import TEMPLATE_VERSION
from .views.module import ProvidedView, config_type

TEMPLATE_DIR = Path(__file__).parent / 'templates'


class Template(Enum):
    """Every template this backend renders; the value is its path under templates/.

    The first path segment is the output kind it belongs to.  ``get-templates``
    lists a kind's templates, and CMake regenerates that output when one of
    them changes.
    """

    TYPE_UNIT = 'type-unit/unit.hpp.j2'

    INTERFACE_IMPLEMENTATION_BASE = 'interface/implementation-base.hpp.j2'
    INTERFACE = 'interface/interface.hpp.j2'
    INTERFACE_TYPES = 'interface/types.hpp.j2'

    LOADER_HPP = 'module-loader/ld-ev.hpp.j2'
    LOADER_CPP = 'module-loader/ld-ev.cpp.j2'

    MODULE_HPP = 'module-files/module.hpp.j2'
    MODULE_CPP = 'module-files/module.cpp.j2'
    IMPLEMENTATION_HPP = 'module-files/implementation.hpp.j2'
    IMPLEMENTATION_CPP = 'module-files/implementation.cpp.j2'
    CMAKELISTS = 'module-files/CMakeLists.txt.j2'
    DOCS_INDEX = 'module-files/docs/index.rst.j2'

    @property
    def kind(self) -> OutputKind:
        return OutputKind(self.value.partition('/')[0])

    @property
    def path(self) -> Path:
        return TEMPLATE_DIR / self.value


class CppBackend:
    """Generates the C++ headers everest-core's modules compile against."""

    name = 'cpp'
    type_output_dir = 'build/generated/generated/types'
    interface_output_dir = 'build/generated/include/generated/interfaces'
    loader_output_dir = 'build/generated/generated/modules'

    @staticmethod
    def options() -> argparse.ArgumentParser:
        """Formatting the generated sources is this target's business alone.

        Both options keep their historical names: EVerest's build passes
        ``--disable-clang-format`` and the spelling is part of the contract.
        """
        parser = argparse.ArgumentParser(add_help=False)
        group = parser.add_argument_group('cpp backend')
        group.add_argument(
            '--clang-format-file', type=str, default=str(Path.cwd()),
            help='directory containing the .clang-format file (default: .)')
        group.add_argument(
            '--disable-clang-format', action='store_true', default=False,
            help='do not run clang-format over generated sources')
        return parser

    def __init__(self, args=None) -> None:
        self._clang_format_dir = Path(getattr(args, 'clang_format_file', None) or Path.cwd())
        self._disable_clang_format = bool(getattr(args, 'disable_clang_format', False))
        self._env = jinja2.Environment(
            loader=jinja2.FileSystemLoader(TEMPLATE_DIR),
            lstrip_blocks=True,
            trim_blocks=True,
            keep_trailing_newline=True,
            undefined=jinja2.StrictUndefined,
        )
        self._env.filters['snake_case'] = naming.snake_case
        self._env.globals['PayloadKind'] = PayloadKind
        self._env.globals['template_version'] = TEMPLATE_VERSION
        self._env.globals['config_type'] = config_type

    # -- post-processing ----------------------------------------------------

    def postprocess(self, files: list[GeneratedFile]) -> None:
        """Format the generated C++ sources with clang-format.

        The commands do not know that C++ needs formatting, or which of a
        target's outputs are even C++.
        """
        sources = [file for file in files if file.path.suffix in ('.hpp', '.cpp')]
        if self._disable_clang_format or not sources:
            return
        executable = _find_clang_format(self._clang_format_dir)
        for file in sources:
            file.content = _clang_format(executable, self._clang_format_dir, file.content)

    # -- templates ----------------------------------------------------------

    def templates(self, kind: OutputKind) -> tuple[Path, ...]:
        return tuple(template.path for template in Template if template.kind is kind)

    def _render(self, template: Template, view, **context) -> str:
        return self._env.get_template(template.value).render(u=view, **context)

    # -- emission -----------------------------------------------------------

    def emit_type_unit(self, model: Model, unit: str, output_dir: Path) -> list[GeneratedFile]:
        view = TypeUnitView(unit=model.types[unit])
        content = self._render(Template.TYPE_UNIT, view)

        path = output_dir / f'{unit}.hpp'
        return [GeneratedFile(
            path=path,
            content=content,
            # The output directory's own name followed by the unit, e.g.
            # "types/energy.hpp", as the build logs it.
            printable_name=(Path(output_dir.name) / f'{unit}.hpp').as_posix(),
            # Deliberately the unit's own modification time, not the newest
            # among the units it references: EVerest's incremental builds are
            # tuned for that comparison.
            source_mtime=view.unit.source.file.stat().st_mtime,
            template_paths=self.templates(OutputKind.TYPE_UNIT),
        )]

    def emit_interface(self, model: Model, interface: str, output_dir: Path) -> list[GeneratedFile]:
        view = InterfaceView(interface=model.interfaces[interface], model=model)
        output_path = output_dir / interface
        mtime = view.interface.source.file.stat().st_mtime
        templates = self.templates(OutputKind.INTERFACE)

        # All of them are declared as genrule outputs in interfaces/BUILD.bazel,
        # so all of them have to be produced for every interface.
        outputs = (
            ('Implementation.hpp', Template.INTERFACE_IMPLEMENTATION_BASE),
            ('Interface.hpp', Template.INTERFACE),
            ('Types.hpp', Template.INTERFACE_TYPES),
        )
        return [
            GeneratedFile(
                path=output_path / filename,
                content=self._render(template, view, filename=filename),
                printable_name=f'{interface}/{filename}',
                source_mtime=mtime,
                template_paths=templates,
            )
            for filename, template in outputs
        ]

    def emit_module_loader(self, model: Model, module: str, output_dir: Path) -> list[GeneratedFile]:
        definition = model.modules[module]
        view = ModuleView(module=definition)
        # Modules are addressed by path but their generated sources sit under
        # the last segment of it.
        name = definition.name
        mtime = definition.source.file.stat().st_mtime
        templates = self.templates(OutputKind.MODULE_LOADER)

        return [
            GeneratedFile(
                path=output_dir / name / filename,
                content=self._render(template, view),
                printable_name=f'{name}/{filename}',
                source_mtime=mtime,
                template_paths=templates,
            )
            for filename, template in (
                ('ld-ev.hpp', Template.LOADER_HPP),
                ('ld-ev.cpp', Template.LOADER_CPP),
            )
        ]

    def emit_module_files(
        self,
        model: Model,
        module: str,
        module_dir: Path,
        *,
        license_header: str,
        read_blocks,
    ) -> list[GeneratedFile]:
        """The sources ``module create`` and ``module update`` manage.

        ``read_blocks`` is handed in rather than looked up here: which regions
        a file protects is a property of the template, and so belongs to this
        backend, but whether an existing file should be read back is the
        command's decision.
        """
        definition = model.modules[module]
        mtime = definition.source.file.stat().st_mtime
        templates = self.templates(OutputKind.MODULE_FILES)

        def rendered(template: Template, view, path: Path, abbr: str, category: FileCategory,
                     policy: UpdatePolicy = UpdatePolicy.REGENERATE,
                     with_license: bool = True) -> GeneratedFile:
            return GeneratedFile(
                path=path,
                content=self._render(template, view),
                printable_name=path.relative_to(module_dir).as_posix(),
                source_mtime=mtime,
                template_paths=templates,
                abbr=abbr,
                category=category,
                update_policy=policy,
                license_header=license_header if with_license else None,
            )

        files: list[GeneratedFile] = []

        # -- core -----------------------------------------------------------
        cmakelists = module_dir / 'CMakeLists.txt'
        header = module_dir / f'{definition.name}.hpp'

        files.append(rendered(
            Template.CMAKELISTS,
            ModuleView(module=definition,
                       blocks=read_blocks(blocks.CMAKELISTS, cmakelists)),
            cmakelists, 'cmakelists', FileCategory.CORE, with_license=False,
        ))
        files.append(rendered(
            Template.MODULE_HPP,
            ModuleView(module=definition, license_header=license_header,
                       blocks=read_blocks(blocks.MODULE_HEADER, header)),
            header, 'module.hpp', FileCategory.CORE,
        ))
        files.append(rendered(
            Template.MODULE_CPP,
            ModuleView(module=definition, license_header=license_header),
            module_dir / f'{definition.name}.cpp', 'module.cpp', FileCategory.CORE,
            UpdatePolicy.CREATE_IF_MISSING,
        ))

        # -- one implementation per provided interface ----------------------
        for provided in definition.provides:
            interface = model.interfaces[provided.interface]
            names = ProvidedView(provided)
            implementation_header = module_dir / names.class_header
            view = ImplementationView(
                provided=provided, interface=interface, module=definition,
                model=model, license_header=license_header,
                blocks=read_blocks(blocks.IMPLEMENTATION_HEADER, implementation_header),
            )
            files.append(rendered(
                Template.IMPLEMENTATION_HPP, view, implementation_header,
                f'{provided.id}.hpp', FileCategory.INTERFACES,
            ))
            files.append(rendered(
                Template.IMPLEMENTATION_CPP, view,
                module_dir / names.cpp_file,
                f'{provided.id}.cpp', FileCategory.INTERFACES,
                UpdatePolicy.CREATE_IF_MISSING,
            ))

        # -- docs -----------------------------------------------------------
        files.append(rendered(
            Template.DOCS_INDEX,
            ModuleView(module=definition),
            module_dir / 'docs' / 'index.rst', 'index.rst', FileCategory.DOCS,
            UpdatePolicy.LEAVE_ALONE, with_license=False,
        ))

        return files


def _find_clang_format(config_dir: Path) -> str:
    """The clang-format executable, once ``config_dir`` is known to configure it."""
    executable = shutil.which('clang-format')
    if executable is None:
        raise EvCliError(
            'could not find clang-format; pass --disable-clang-format to skip '
            'formatting generated sources'
        )
    if not config_dir.is_dir():
        raise EvCliError(f'clang-format directory {config_dir} does not exist')
    if not (config_dir / '.clang-format').exists():
        raise EvCliError(f'clang-format directory {config_dir} contains no .clang-format file')
    return executable


def _clang_format(executable: str, config_dir: Path, source: str) -> str:
    completed = subprocess.run(
        [executable, '--style=file'],
        input=source,
        capture_output=True,
        cwd=config_dir,
        encoding='utf-8',
    )
    if completed.returncode != 0:
        raise EvCliError(f'clang-format failed:\n{completed.stderr}')
    return completed.stdout
