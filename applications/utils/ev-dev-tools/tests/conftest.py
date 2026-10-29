# SPDX-License-Identifier: Apache-2.0
# Copyright Pionix GmbH and Contributors to EVerest
"""Shared fixtures.

Two source trees are used.  ``fixture_tree`` is the small one checked in beside
these tests: it exercises every payload shape the generator supports and is
what the golden-output tests compare against, so the suite means something
without an everest-core checkout.  ``everest_tree`` is the real thing, used by
the tests that assert something about the definitions as they actually are.
"""

from __future__ import annotations

import os
from pathlib import Path

import pytest

TESTS_DIR = Path(__file__).resolve().parent
FIXTURE_TREE = TESTS_DIR / 'fixtures' / 'tree'
GOLDEN_DIR = TESTS_DIR / 'fixtures' / 'golden'

#: Set when a checkout is named rather than inferred, which changes a missing
#: one from "nothing to test against here" into a misconfiguration.
NAMED_EVEREST_DIR = os.environ.get('EVEREST_CORE')

#: tests/ -> ev-dev-tools/ -> utils/ -> applications/ -> repository root
EVEREST_DIR = Path(NAMED_EVEREST_DIR or TESTS_DIR.parents[3])
SCHEMAS_DIR = EVEREST_DIR / 'lib' / 'everest' / 'framework' / 'schemas'

if NAMED_EVEREST_DIR and not SCHEMAS_DIR.is_dir():
    # Skipping here would let a run that was pointed at a checkout report success
    # having tested nothing against it.
    raise pytest.UsageError(
        f'EVEREST_CORE={NAMED_EVEREST_DIR} has no framework schemas at {SCHEMAS_DIR}'
    )

needs_everest = pytest.mark.skipif(
    not SCHEMAS_DIR.is_dir(),
    reason=f'no everest-core checkout at {EVEREST_DIR} (set EVEREST_CORE)',
)


def make_loader(tree_dir: Path, sink):
    from ev_cli.schema.loader import DefinitionLoader, SourceTree
    from ev_cli.schema.validate import Validators

    return DefinitionLoader(SourceTree([tree_dir]), Validators.load(SCHEMAS_DIR), sink)


@pytest.fixture
def sink():
    from ev_cli.schema.diagnostics import CollectingSink
    return CollectingSink()


@pytest.fixture
def fixture_loader(sink):
    """A loader over the small checked-in tree."""
    if not SCHEMAS_DIR.is_dir():
        pytest.skip(f'no framework schemas at {SCHEMAS_DIR}')
    return make_loader(FIXTURE_TREE, sink)


@pytest.fixture
def fixture_model(fixture_loader, sink):
    """The whole fixture tree as a model: types, interfaces and the module."""
    from ev_cli.ir import build

    types = build.build_type_units(fixture_loader, sink)
    interfaces = build.build_interfaces(
        fixture_loader, sink, fixture_loader.tree.interface_names())
    modules = build.build_modules(fixture_loader, sink, FIXTURE_TREE, ['Sample'])

    from ev_cli.ir.model import Model
    return Model(types=types, interfaces=interfaces, modules=modules)


@pytest.fixture
def everest_loader(sink):
    """A loader over the real everest-core definitions."""
    if not SCHEMAS_DIR.is_dir():
        pytest.skip(f'no everest-core checkout at {EVEREST_DIR}')
    return make_loader(EVEREST_DIR, sink)
