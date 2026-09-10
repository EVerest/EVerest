#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright Pionix GmbH and Contributors to EVerest

"""Shared helper for building an active_modules YAML slot payload from config-example.yaml.

Used by both configuration_api_tests.py and complete_workflow_tests.py to feed the
configuration API's load_from_yaml with the tutorial's example module set.
"""

from pathlib import Path

import yaml

from everest.testing.core_utils.everest_core import EverestCore

CONFIG_NAME = "config-example.yaml"

# This file lives at <repo>/tests/management_api_tests/example_config.py.
_REPO_ROOT = Path(__file__).resolve().parents[2]


def _resolve_example_config_path(everest_core: EverestCore) -> Path:
    """Locate config-example.yaml, mirroring the everest-testing core_config fixture's fallback.

    See applications/utils/everest-testing/src/everest/testing/core_utils/fixtures.py
    (`core_config`): prefer the config as installed under the everest-prefix, but on a
    local dev setup where that prefix is not (or not freshly) installed, fall back to
    the repository's own config/ directory.
    """
    installed_path = everest_core.etc_path / CONFIG_NAME
    if installed_path.exists():
        return installed_path

    repo_config_path = _REPO_ROOT / "config" / CONFIG_NAME
    if repo_config_path.exists():
        return repo_config_path

    raise FileNotFoundError(
        f"'{CONFIG_NAME}' not found at '{installed_path}' or '{repo_config_path}'."
    )


def example_active_modules_yaml(everest_core: EverestCore) -> str:
    """The active_modules subtree of config-example.yaml, as YAML text.

    Mirrors the CLI's load_yaml behavior, which extracts the module configuration
    from a config file (manager settings are not part of a slot).
    """
    config = yaml.safe_load(_resolve_example_config_path(everest_core).read_text())
    return yaml.dump({"active_modules": config["active_modules"]})
