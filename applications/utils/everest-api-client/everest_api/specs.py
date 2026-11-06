# SPDX-License-Identifier: Apache-2.0
# Copyright Pionix GmbH and Contributors to EVerest
"""Locate the EVerest API AsyncAPI specs and load them into ApiModels."""

import os
import sys
from pathlib import Path

import yaml

from everest_api.model import api_name_from_file, build_model
from everest_api.spec_loader import SpecError, load_spec

SPEC_DIR_ENV = "EVEREST_API_SPEC_DIR"
SPEC_GLOB = "*_API.yaml"

# applications/utils/everest-api-client/everest_api -> repository root
_IN_TREE_SPEC_DIR = Path(__file__).resolve().parents[4] / "docs" / "source" / "reference" / "EVerest_API"


def find_spec_dir(spec_dir=None):
    """--spec-dir, then $EVEREST_API_SPEC_DIR, then the specs of this source tree."""
    return Path(spec_dir or os.getenv(SPEC_DIR_ENV) or _IN_TREE_SPEC_DIR)


def load_models(spec_dir, warn=lambda msg: print(msg, file=sys.stderr)):
    """Build one ApiModel per *_API.yaml in spec_dir, sorted by API name. A spec
    that cannot be processed is reported and skipped."""
    models = []
    for path in sorted(Path(spec_dir).glob(SPEC_GLOB)):
        try:
            models.append(build_model(load_spec(path), api_name_from_file(path.stem)))
        except (SpecError, OSError, ValueError, TypeError, yaml.YAMLError) as err:
            warn(f"Warning: skipping {path.name}: {err}")
    return sorted(models, key=lambda model: model.api)
