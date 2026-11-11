# SPDX-License-Identifier: Apache-2.0
# Copyright Pionix GmbH and Contributors to EVerest
"""Every registered backend has to be a whole backend.

A single-backend abstraction is indistinguishable from no abstraction, and the
way that shows up is a protocol method only one implementation bothers with.
That happened: ``emit_module_files`` was added to the C++ backend and left out
of both the protocol and ``ir-dump``, so ``module create`` silently hardcoded
``cpp`` and selecting any other target would have failed at the attribute
lookup.  These tests are the guard against a repeat.
"""

from __future__ import annotations

import argparse

import pytest

from ev_cli.backends.base import Backend, GeneratedFile, OutputKind, UpdatePolicy, registry

#: What a backend must provide for every command to work against it.
REQUIRED = (
    'options',
    'templates',
    'emit_type_unit',
    'emit_interface',
    'emit_module_loader',
    'emit_module_files',
)


def test_more_than_one_backend_is_registered():
    """Otherwise none of the below proves anything about the abstraction."""
    assert len(registry.names()) >= 2


@pytest.mark.parametrize('name', registry.names())
class TestEveryBackend:

    def test_satisfies_the_protocol(self, name):
        assert isinstance(registry.create(name), Backend)

    @pytest.mark.parametrize('method', REQUIRED)
    def test_implements(self, name, method):
        backend = registry.create(name)
        assert callable(getattr(backend, method, None)), (
            f'backend "{name}" does not implement {method}()'
        )

    def test_names_itself(self, name):
        assert registry.create(name).name == name

    def test_templates_answers_every_output_kind(self, name):
        backend = registry.create(name)
        for kind in OutputKind:
            assert isinstance(backend.templates(kind), tuple)


class TestBackendOptions:
    """Each target contributes its own command line options."""

    @pytest.mark.parametrize('name', registry.names())
    def test_options_is_a_usable_parent_parser(self, name):
        parser = registry.backend_class(name).options()
        assert isinstance(parser, argparse.ArgumentParser)
        # a parent that adds its own -h would collide with every child's
        assert not parser.add_help

    def test_no_two_backends_claim_the_same_option(self):
        """All of them are attached to every emitting parser at once."""
        seen: dict[str, str] = {}
        for name in registry.names():
            for action in registry.backend_class(name).options()._actions:
                for flag in action.option_strings:
                    assert flag not in seen, (
                        f'{flag} claimed by both "{seen[flag]}" and "{name}"')
                    seen[flag] = name

    def test_the_cpp_backend_owns_the_clang_format_options(self):
        flags = {
            flag
            for action in registry.backend_class('cpp').options()._actions
            for flag in action.option_strings
        }
        assert '--disable-clang-format' in flags
        assert '--clang-format-file' in flags

    def test_a_backend_reads_its_own_options(self, fixture_model, tmp_path):
        """With --disable-clang-format nothing looks for clang-format or its directory."""
        args = argparse.Namespace(
            disable_clang_format=True, clang_format_file=str(tmp_path / 'does-not-exist'))
        files = registry.create('cpp', args).emit_type_unit(fixture_model, 'units', tmp_path)
        assert [file.path.name for file in files] == ['units.hpp']


def test_unknown_backend_is_reported_with_the_alternatives():
    with pytest.raises(KeyError, match='unknown backend'):
        registry.create('nonesuch')


class TestUpdatePolicy:
    """The backend decides which of its outputs a human owns."""

    def test_generated_files_may_be_regenerated_by_default(self):
        assert GeneratedFile(
            path=None, content='', printable_name='x', source_mtime=0.0,
        ).update_policy is UpdatePolicy.REGENERATE

    def test_the_cpp_backend_protects_its_sources(self, fixture_model, tmp_path):
        from ev_cli import blocks
        from ev_cli.backends.cpp.backend import CppBackend

        files = CppBackend(argparse.Namespace(disable_clang_format=True)).emit_module_files(
            fixture_model, 'Sample', tmp_path,
            license_header='// SPDX-License-Identifier: Apache-2.0',
            read_blocks=lambda block_set, path: blocks.placeholders(block_set),
        )
        policy = {f.abbr: f.update_policy for f in files}

        # Regenerated: the build file and the headers.
        assert policy['cmakelists'] is UpdatePolicy.REGENERATE
        assert policy['module.hpp'] is UpdatePolicy.REGENERATE
        assert policy['main.hpp'] is UpdatePolicy.REGENERATE
        # Never replaced: the sources a human writes behaviour into.
        assert policy['module.cpp'] is UpdatePolicy.CREATE_IF_MISSING
        assert policy['main.cpp'] is UpdatePolicy.CREATE_IF_MISSING
        # Never touched at all, not even recreated when deleted.
        assert policy['index.rst'] is UpdatePolicy.LEAVE_ALONE
