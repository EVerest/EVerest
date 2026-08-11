#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright Pionix GmbH and Contributors to EVerest

import time

import pytest

from everest.testing.core_utils.fixtures import *

from configuration_api_client import ConfigurationApiClient
from lifecycle_api_client import LifecycleApiClient


@pytest.fixture
def lifecycle_client(connected_mqtt_client) -> LifecycleApiClient:
    """Lifecycle API client subscribed to the status topic before the manager starts.

    The lifecycle API topics carry no per-test MQTT prefix, so a retained status
    message (e.g. the LWT of a previously killed manager) can leak between runs;
    it is cleared before subscribing and again on teardown.
    """
    client = LifecycleApiClient(connected_mqtt_client)
    client.clear_retained_status()
    client.subscribe_status()
    # give the broker a moment to process the subscription
    time.sleep(0.2)

    yield client

    client.clear_retained_status()


@pytest.fixture
def configuration_client(connected_mqtt_client) -> ConfigurationApiClient:
    """Configuration API client subscribed to the notice topics before the manager starts.

    The active_slot/config_updates notices are QoS0 and not retained, so the subscription
    must be in place before the manager publishes anything.
    """
    client = ConfigurationApiClient(connected_mqtt_client)
    client.subscribe_notices()
    # give the broker a moment to process the subscriptions
    time.sleep(0.2)

    yield client
