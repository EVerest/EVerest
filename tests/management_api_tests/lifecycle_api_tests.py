#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright Pionix GmbH and Contributors to EVerest

"""Integration tests for the manager's lifecycle management API.

Covers what docs/source/tutorials/management_apis.rst describes for the
lifecycle API: read-only monitoring, read-write stop/start/restart, the LWT
("EVerest Running") and the --into-idle workflow. The API is driven via raw
MQTT; side effects are verified via the manager status fifo.
"""

import threading

import pytest

from everest.testing.core_utils.everest_core import EverestCore, ManagerStatusFifo
from everest.testing.core_utils.fixtures import *

from lifecycle_api_client import LifecycleApiClient, read_retained_status

# The lifecycle API topics are not test-instance specific (no per-test MQTT
# prefix), so these tests must not run concurrently with each other.
pytestmark = pytest.mark.xdist_group(name="management_api")


def assert_status_subsequence(statuses: list, expected: list) -> None:
    idx = 0
    for status in statuses:
        if idx < len(expected) and status == expected[idx]:
            idx += 1
    assert idx == len(expected), f"Expected subsequence {expected} in {statuses}"


@pytest.mark.everest_core_config("config-sil-manager-lifecycle.yaml")
@pytest.mark.everest_manager_args("--lifecycle-api=ro")
def test_ro_status_stream_and_rejections(everest_core: EverestCore, lifecycle_client: LifecycleApiClient):
    everest_core.start()
    lifecycle_client.wait_for_module_status("Running")

    updates = lifecycle_client.status_updates
    assert_status_subsequence(lifecycle_client.module_statuses(), ["NotRunning", "Starting", "Running"])
    for update in updates:
        assert update["everest_running"] is True
        assert update["lifecycle_api_ro"] is True
        assert update["configuration_api_available"] == "N_A"
        assert update["tstamp"]

    version = lifecycle_client.rpc("get_everest_version")
    assert version["name"]
    assert version["version"]
    assert version["git_version"]

    assert lifecycle_client.rpc("stop_modules") == {"status": "Rejected"}
    assert lifecycle_client.rpc("start_modules") == {"status": "Rejected"}

    # the rejected commands must not have any effect on the modules
    everest_core.assert_no_manager_status(
        [ManagerStatusFifo.MANAGER_IDLE, ManagerStatusFifo.MANAGER_RESTART_REQUESTED],
        timeout_s=3.0,
    )
    assert "Stopping" not in lifecycle_client.module_statuses()


@pytest.mark.everest_core_config("config-sil-manager-lifecycle.yaml")
@pytest.mark.everest_manager_args("--lifecycle-api=ro", "--configuration-api=rw")
def test_ro_reports_configuration_api_availability(everest_core: EverestCore,
                                                   lifecycle_client: LifecycleApiClient):
    everest_core.start()
    update = lifecycle_client.wait_for_module_status("Running")

    assert update["configuration_api_available"] == "RW"
    assert update["lifecycle_api_ro"] is True


@pytest.mark.everest_core_config("config-sil-manager-lifecycle.yaml")
@pytest.mark.everest_manager_args("--lifecycle-api=rw")
def test_rw_stop_start(everest_core: EverestCore, lifecycle_client: LifecycleApiClient):
    everest_core.start()
    update = lifecycle_client.wait_for_module_status("Running")
    assert update["lifecycle_api_ro"] is False
    # drop the startup fifo lines so the waits below only match fresh events
    everest_core.discard_manager_status_pending()

    mark = lifecycle_client.mark()
    assert lifecycle_client.rpc("stop_modules") == {"status": "Stopping"}
    lifecycle_client.wait_for_module_status("Stopping", after_index=mark)
    lifecycle_client.wait_for_module_status("NotRunning", after_index=mark)
    everest_core.wait_for_manager_status(ManagerStatusFifo.MANAGER_IDLE)

    assert lifecycle_client.rpc("stop_modules") == {"status": "NoModulesToStop"}

    mark = lifecycle_client.mark()
    assert lifecycle_client.rpc("start_modules") == {"status": "Starting"}
    lifecycle_client.wait_for_module_status("Starting", after_index=mark)
    lifecycle_client.wait_for_module_status("Running", after_index=mark)
    everest_core.wait_for_manager_status(ManagerStatusFifo.ALL_MODULES_STARTED)
    everest_core.wait_for_manager_status(ManagerStatusFifo.MANAGER_RUNNING)

    assert all(update["everest_running"] is True for update in lifecycle_client.status_updates)


@pytest.mark.everest_core_config("config-sil-manager-lifecycle.yaml")
@pytest.mark.everest_manager_args("--lifecycle-api=rw")
def test_rw_restart_while_running(everest_core: EverestCore, lifecycle_client: LifecycleApiClient):
    everest_core.start()
    lifecycle_client.wait_for_module_status("Running")
    # drop the startup fifo lines so the waits below only match fresh events
    everest_core.discard_manager_status_pending()

    mark = lifecycle_client.mark()
    assert lifecycle_client.rpc("start_modules") == {"status": "Restarting"}

    # an API-triggered restart is a graceful shutdown (with restart cause), not a
    # crash-recovery restart, so the fifo shows MANAGER_SHUTDOWN_REQUESTED
    everest_core.wait_for_manager_status(ManagerStatusFifo.MANAGER_SHUTDOWN_REQUESTED)
    lifecycle_client.wait_for_module_status("RestartTriggered", after_index=mark)
    lifecycle_client.wait_for_module_status("Running", after_index=mark)
    everest_core.wait_for_manager_status(ManagerStatusFifo.ALL_MODULES_STARTED)
    everest_core.wait_for_manager_status(ManagerStatusFifo.MANAGER_RUNNING)


@pytest.mark.everest_core_config("config-sil-manager-lifecycle.yaml")
@pytest.mark.everest_manager_args("--lifecycle-api=ro")
def test_lwt_on_manager_death(everest_core: EverestCore, lifecycle_client: LifecycleApiClient):
    everest_core.start()
    lifecycle_client.wait_for_module_status("Running")

    mark = lifecycle_client.mark()
    # SIGKILL: the broker must publish the LWT on the unclean disconnect
    everest_core.process.kill()
    everest_core.process.wait(timeout=60.0)

    lwt = lifecycle_client.wait_for_status(
        lambda u: u["everest_running"] is False, after_index=mark)
    assert "module_status" not in lwt
    assert "tstamp" not in lwt

    # the LWT is retained: a client connecting only now must see it immediately
    retained = read_retained_status()
    assert retained is not None, "Expected a retained status message after manager death"
    assert retained["everest_running"] is False


@pytest.mark.everest_core_config("config-sil-manager-lifecycle.yaml")
@pytest.mark.everest_manager_args("--lifecycle-api=rw", "--into-idle")
def test_into_idle_workflow(everest_core: EverestCore, lifecycle_client: LifecycleApiClient):
    start_exception = []

    # start() blocks until ALL_MODULES_STARTED, which with --into-idle only
    # happens once start_modules is requested below
    def start_core():
        try:
            everest_core.start()
        except Exception as exc:
            start_exception.append(exc)

    starter_thread = threading.Thread(target=start_core)
    starter_thread.start()

    everest_core.wait_for_manager_status(ManagerStatusFifo.MANAGER_IDLE, timeout_s=20.0)
    update = lifecycle_client.wait_for_module_status("NotRunning")
    assert update["everest_running"] is True

    assert lifecycle_client.rpc("stop_modules") == {"status": "NoModulesToStop"}

    mark = lifecycle_client.mark()
    assert lifecycle_client.rpc("start_modules") == {"status": "Starting"}

    starter_thread.join(timeout=60.0)
    assert not starter_thread.is_alive(), "Startup thread did not finish after start_modules."
    assert not start_exception, f"Unexpected startup exception(s): {start_exception}"

    lifecycle_client.wait_for_module_status("Running", after_index=mark)
    everest_core.wait_for_manager_status(ManagerStatusFifo.MANAGER_RUNNING)
