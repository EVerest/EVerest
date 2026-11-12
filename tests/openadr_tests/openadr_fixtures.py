# SPDX-License-Identifier: Apache-2.0
# Copyright Pionix GmbH and Contributors to EVerest

import pytest
from everest.testing.core_utils._configuration.everest_configuration_strategies.everest_configuration_strategy import (
    EverestConfigAdjustmentStrategy,
)

from mock_vtn import MockVtn
from openadr_test_utils import VtnStrategy


@pytest.fixture
def mock_vtn():
    vtn = MockVtn()
    vtn.start()
    yield vtn
    vtn.stop()


@pytest.fixture
def everest_config_strategies(request, mock_vtn):
    """Override base fixture to point the module to the mock VTN."""
    strategies = []
    marker = request.node.get_closest_marker("everest_config_adaptions")
    if marker:
        for v in marker.args:
            assert isinstance(v, EverestConfigAdjustmentStrategy)
            strategies.append(v)
    strategies.append(VtnStrategy(mock_vtn.url))
    return strategies
