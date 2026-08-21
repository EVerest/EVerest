#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright Pionix GmbH and Contributors to EVerest

import pytest

from everest.testing.core_utils.fixtures import *

from configuration_api_client import ConfigurationApiClient
from lifecycle_api_client import LifecycleApiClient, clear_retained_lifecycle_status


@pytest.fixture
def _lifecycle_status_teardown_guard():
    """Clears the retained lifecycle status topic strictly after the manager has fully exited.

    lifecycle_client (below) depends on connected_mqtt_client, which depends on everest_core;
    pytest tears fixtures down in the reverse of their setup order, so lifecycle_client's own
    teardown -- and any clear performed there -- runs BEFORE everest_core's teardown calls
    everest_core.stop(). A still-live manager can then re-publish retained status after that
    clear, and since the manager itself no longer wipes this topic on startup, that leak is
    never otherwise cleaned up.

    This fixture takes no fixture parameters of its own -- not even everest_core -- so it has
    no dependency edge forcing it to tear down before anything. Listed first in lifecycle_client's
    signature (below), pytest schedules its setup ahead of connected_mqtt_client/everest_core, and
    by the same reversal rule its teardown then runs last: after everest_core's teardown, which
    calls EverestCore.stop() -- SIGINT followed by a blocking process.wait() -- so the manager
    process is confirmed dead by the time the clear below runs. Verified with a standalone
    fixture-ordering probe against this project's pytest (9.1.1); also covers the SIGKILL case
    (test_lwt_on_manager_death), since that test already waits out the kill itself before this
    fixture's teardown runs.
    """
    yield
    clear_retained_lifecycle_status()


@pytest.fixture
def lifecycle_client(_lifecycle_status_teardown_guard, connected_mqtt_client) -> LifecycleApiClient:
    """Lifecycle API client subscribed to the status topic before the manager starts.

    The lifecycle API topics carry no per-test MQTT prefix, so a retained status
    message (e.g. the LWT of a previously killed manager) can leak between runs;
    it is cleared before subscribing here, and by _lifecycle_status_teardown_guard
    once the manager is confirmed dead (see that fixture's docstring for why it is
    listed first above).
    """
    client = LifecycleApiClient(connected_mqtt_client)
    client.clear_retained_status()
    client.subscribe_status()

    yield client


@pytest.fixture
def configuration_client(connected_mqtt_client) -> ConfigurationApiClient:
    """Configuration API client subscribed to the notice topics before the manager starts.

    The active_slot/config_updates notices are QoS0 and not retained, so the subscription
    must be in place before the manager publishes anything.
    """
    client = ConfigurationApiClient(connected_mqtt_client)
    client.subscribe_notices()

    yield client
