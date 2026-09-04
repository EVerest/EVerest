#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright Pionix GmbH and Contributors to EVerest

"""Integration tests for the manager's lifecycle management API.

Covers what docs/source/tutorials/management_apis.rst describes for the
lifecycle API: read-only monitoring, read-write stop/start/restart, the LWT
("EVerest Running") and the --into-idle workflow. The API is driven via raw
MQTT; side effects are verified via the manager status fifo.
"""

import json
import os
import threading
import time
import uuid

import pytest

from everest.testing.core_utils.everest_core import EverestCore, ManagerStatusFifo
from everest.testing.core_utils.fixtures import *

from assertions import assert_status_subsequence
from background_manager_start import background_manager_start
from lifecycle_api_client import (LIFECYCLE_STATUS_TOPIC, LifecycleApiClient, make_mqtt_client,
                                  read_retained_status)

# The lifecycle API topics are not test-instance specific (no per-test MQTT
# prefix), so these tests must not run concurrently with each other.
pytestmark = pytest.mark.xdist_group(name="management_api")


class _StatusTopicRecorder:
    """Records every raw payload delivered on the lifecycle status topic.

    Unlike LifecycleApiClient, which drops zero-length payloads, this keeps the
    empty retained-clear ones too, so a test can catch them.
    """

    def __init__(self):
        host = os.environ.get("MQTT_SERVER_ADDRESS", "127.0.0.1")
        port = int(os.environ.get("MQTT_SERVER_PORT", "1883"))
        self._lock = threading.Lock()
        self.payloads = []  # list[bytes], in delivery order
        self._client = make_mqtt_client(f"management_api_tests_recorder_{uuid.uuid4().hex}")
        self._client.on_message = self._on_message
        self._client.connect(host, port)
        self._client.subscribe(LIFECYCLE_STATUS_TOPIC, qos=2)
        self._client.loop_start()

    def _on_message(self, _client, _userdata, msg):
        with self._lock:
            self.payloads.append(msg.payload)

    def empty_payloads(self) -> list:
        with self._lock:
            return [p for p in self.payloads if not p]

    def wait_for_payload(self, predicate, timeout_s: float = 60.0) -> None:
        deadline = time.monotonic() + timeout_s
        while time.monotonic() < deadline:
            with self._lock:
                snapshot = list(self.payloads)
            for payload in snapshot:
                if payload and predicate(json.loads(payload)):
                    return
            time.sleep(0.05)
        raise TimeoutError("Timeout waiting for a matching status payload")

    def close(self) -> None:
        self._client.loop_stop()
        self._client.disconnect()


@pytest.mark.everest_core_config("config-sil-manager-lifecycle.yaml")
@pytest.mark.everest_manager_args("--lifecycle-api=rw")
def test_status_topic_survives_startup_retained_clear(everest_core: EverestCore,
                                                      lifecycle_client: LifecycleApiClient):
    """The manager must not wipe the management-API status topic.

    At all-modules-ready the manager clears the retained topics it published.
    The lifecycle status is retained too, so it got wiped along with the module
    topics: live monitors were handed an empty (undeserializable) payload and
    the retained status was gone. Neither may happen while the manager lives.
    """
    # subscribe before the manager starts, so a startup retained-clear is seen;
    # the lifecycle_client fixture already removed any stale retained message
    recorder = _StatusTopicRecorder()
    try:
        time.sleep(0.2)  # let the recorder subscription settle at the broker

        everest_core.start()
        lifecycle_client.wait_for_module_status("Running")

        # the broker keeps the per-topic order per subscriber, so once "Running"
        # arrives here, any startup retained-clear has been delivered as well
        recorder.wait_for_payload(lambda u: u.get("module_status") == "Running")
        time.sleep(0.5)  # settle any in-flight delivery

        empties = recorder.empty_payloads()
        assert not empties, (
            "manager published an empty (retained-clear) payload on the lifecycle "
            f"status topic; full delivery sequence was: {recorder.payloads}"
        )
    finally:
        recorder.close()


@pytest.mark.everest_core_config("config-sil-manager-lifecycle.yaml")
@pytest.mark.everest_manager_args("--lifecycle-api=rw")
def test_retained_status_readable_after_module_stop(everest_core: EverestCore,
                                                    lifecycle_client: LifecycleApiClient):
    """After stop_modules the manager is still alive; its status must stay readable.

    everest_running == false means the manager process itself is gone - it publishes that on
    shutdown, and the broker's LWT covers an unclean death - so a client connecting after
    stop_modules -> Idle has to find a retained status of everest_running == true with
    module_status == "NotRunning".
    """
    everest_core.start()
    lifecycle_client.wait_for_module_status("Running")
    everest_core.discard_manager_status_pending()

    mark = lifecycle_client.mark()
    reply = lifecycle_client.rpc("stop_modules")
    assert reply["status"] == "Stopping"
    stopped = lifecycle_client.wait_for_module_status("NotRunning", after_index=mark)
    everest_core.wait_for_manager_status(ManagerStatusFifo.MANAGER_IDLE)

    assert stopped["everest_running"] is True
    assert stopped["module_status"] == "NotRunning"

    # let any retained-clear vs. status-publish ordering settle at the broker
    time.sleep(0.5)

    # a client connecting only now must still see the manager's retained status
    retained = read_retained_status()
    assert retained is not None, (
        "no retained lifecycle status after stop_modules: the manager wiped the "
        "management-API status topic via clear_retained_topics()"
    )
    assert retained["everest_running"] is True
    assert retained["module_status"] == "NotRunning"


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

    reply = lifecycle_client.rpc("stop_modules")
    assert reply["status"] == "Rejected"
    reply = lifecycle_client.rpc("start_modules")
    assert reply["status"] == "Rejected"

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
    reply = lifecycle_client.rpc("stop_modules")
    assert reply["status"] == "Stopping"
    lifecycle_client.wait_for_module_status("Stopping", after_index=mark)
    lifecycle_client.wait_for_module_status("NotRunning", after_index=mark)
    everest_core.wait_for_manager_status(ManagerStatusFifo.MANAGER_IDLE)

    reply = lifecycle_client.rpc("stop_modules")
    assert reply["status"] == "NoModulesToStop"

    mark = lifecycle_client.mark()
    reply = lifecycle_client.rpc("start_modules")
    assert reply["status"] == "Starting"
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
    reply = lifecycle_client.rpc("start_modules")
    assert reply["status"] == "Restarting"

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
    """SIGKILL: the manager cannot publish its own final status, so the broker's LWT must.

    See test_status_reports_not_running_after_clean_shutdown for the other half of the
    contract; the payloads are identical, which is why both tests are needed.
    """
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
@pytest.mark.everest_manager_args("--lifecycle-api=ro")
def test_status_reports_not_running_after_clean_shutdown(everest_core: EverestCore,
                                                         lifecycle_client: LifecycleApiClient):
    """A cleanly exiting manager must leave the same not-running status the LWT would.

    everest_running == false is not reserved for a crash: the manager publishes it itself,
    retained, immediately before disconnecting, so a client connecting after a graceful stop
    does not read a stale everest_running == true. The payload is byte-identical to the LWT,
    so a clean exit and an unclean death are indistinguishable to a client - by design, and
    the reason this test asserts the contract rather than which mechanism delivered it.
    """
    everest_core.start()
    lifecycle_client.wait_for_module_status("Running")

    mark = lifecycle_client.mark()
    # SIGINT, and stop() blocks on process.wait(), so the manager is reaped before we assert
    everest_core.stop()
    assert everest_core.process.returncode == 0, "manager did not exit cleanly"

    final = lifecycle_client.wait_for_status(
        lambda u: u["everest_running"] is False, after_index=mark)
    assert "module_status" not in final
    assert "tstamp" not in final

    # retained: a client connecting only now must see it immediately
    retained = read_retained_status()
    assert retained is not None, "Expected a retained status after a clean manager shutdown"
    assert retained["everest_running"] is False
    assert "module_status" not in retained


@pytest.mark.everest_core_config("config-sil-manager-lifecycle.yaml")
@pytest.mark.everest_manager_args("--lifecycle-api=rw", "--into-idle")
def test_into_idle_workflow(everest_core: EverestCore, lifecycle_client: LifecycleApiClient):
    # --into-idle keeps start() blocked until start_modules is requested below, so it
    # has to run on a background thread across the idle-phase work
    with background_manager_start(everest_core) as (starter_thread, start_exception):
        everest_core.wait_for_manager_status(ManagerStatusFifo.MANAGER_IDLE, timeout_s=20.0)
        update = lifecycle_client.wait_for_module_status("NotRunning")
        assert update["everest_running"] is True

        reply = lifecycle_client.rpc("stop_modules")
        assert reply["status"] == "NoModulesToStop"

        mark = lifecycle_client.mark()
        reply = lifecycle_client.rpc("start_modules")
        assert reply["status"] == "Starting"

        starter_thread.join(timeout=60.0)
        assert not starter_thread.is_alive(), "Startup thread did not finish after start_modules."
        assert not start_exception, f"Unexpected startup exception(s): {start_exception}"

    lifecycle_client.wait_for_module_status("Running", after_index=mark)
    everest_core.wait_for_manager_status(ManagerStatusFifo.MANAGER_RUNNING)


@pytest.mark.everest_core_config("config-empty.yaml")
@pytest.mark.everest_manager_args("--lifecycle-api=rw", "--into-idle")
def test_start_modules_from_idle_with_empty_config_stays_idle(everest_core: EverestCore,
                                                              lifecycle_client: LifecycleApiClient):
    """Regression: start_modules while Idle on an empty-but-valid config (zero modules) used to
    reach handle_start_modules(), whose empty-list invariant (std::logic_error) took the whole
    manager down after the client had been told "Starting". The manager must instead stay Idle
    and report FailedToStart, so a corrected configuration can be pushed and retried."""
    # The manager is expected to stay Idle throughout, so start() can wait for that
    # directly instead of using the background-start helper.
    everest_core.start(expected_status=ManagerStatusFifo.MANAGER_IDLE)
    lifecycle_client.wait_for_module_status("NotRunning")

    mark = lifecycle_client.mark()
    reply = lifecycle_client.rpc("start_modules")
    assert reply["status"] == "Starting"
    update = lifecycle_client.wait_for_module_status("FailedToStart", after_index=mark)
    assert update["everest_running"] is True

    # A second attempt must publish FailedToStart AGAIN (repeat delivery, no value dedupe),
    # and the manager must still be alive and serving requests.
    mark = lifecycle_client.mark()
    reply = lifecycle_client.rpc("start_modules")
    assert reply["status"] == "Starting"
    update = lifecycle_client.wait_for_module_status("FailedToStart", after_index=mark)
    assert update["everest_running"] is True

    everest_core.assert_no_manager_status(ManagerStatusFifo.MANAGER_EXITING, timeout_s=2.0)
    everest_core.stop()
