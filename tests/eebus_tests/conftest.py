# SPDX-License-Identifier: Apache-2.0
# Copyright Pionix GmbH and Contributors to EVerest

"""Shared fixtures for EEBus reference control box tests."""

import os
from pathlib import Path

import pytest
from everest.testing.core_utils._configuration.everest_configuration_strategies.everest_configuration_strategy import (
    EverestConfigAdjustmentStrategy,
)

from eebus_test_utils import EebusPortStrategy, ReferenceControlBox, get_free_port


@pytest.fixture
def eebus_grpc_port():
    """Allocate a free port for the EEBUS gRPC control service."""
    return get_free_port()


@pytest.fixture
def eebus_service_port():
    """Allocate a free port for the EEBUS SHIP service."""
    return get_free_port()


@pytest.fixture
def everest_config_strategies(request, eebus_grpc_port, eebus_service_port):
    """Override base fixture to inject EebusPortStrategy with dynamic ports.

    This replaces the base everest_config_strategies from
    everest.testing.core_utils.fixtures, adding EEBUS-specific port
    rewriting. NetworkIsolationStrategy is not needed for EEBUS tests.
    """
    strategies = []
    marker = request.node.get_closest_marker("everest_config_adaptions")
    if marker:
        for v in marker.args:
            assert isinstance(v, EverestConfigAdjustmentStrategy), (
                "Arguments to 'everest_config_adaptions' must all be "
                "instances of EverestConfigAdjustmentStrategy"
            )
            strategies.append(v)
    strategies.append(EebusPortStrategy(eebus_grpc_port, eebus_service_port))
    return strategies


@pytest.fixture(scope="session")
def controlbox_binary():
    """Locate the controlbox binary or skip."""
    candidates = [
        Path(__file__).parent / "controlbox",
        Path.home() / "go" / "bin" / "controlbox",
    ]
    for p in candidates:
        if p.exists() and os.access(p, os.X_OK):
            return p
    pytest.skip("controlbox binary not found")


@pytest.fixture
def reference_control_box(request, controlbox_binary):
    """Create and yield a ReferenceControlBox, stopping it on teardown.

    The control box is NOT auto-started because the SKI may only become
    available after EVerest starts and the sidecar generates certs.
    Call box.start() in the test body after extracting the SKI.
    """
    port = get_free_port()
    # remote_ski will be set by the test before calling start()
    box = ReferenceControlBox(controlbox_binary, port, remote_ski="")
    yield box
    box.stop()
