#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright Pionix GmbH and Contributors to EVerest

import threading
import time
from collections import defaultdict

import pytest

from everest.testing.core_utils.everest_core import EverestCore, ManagerStatusFifo
from everest.testing.core_utils.fixtures import *


def topic_count_matching(topic_counts, topic_predicate):
    return sum(count for topic, count in topic_counts.items() if topic_predicate(topic))


def wait_for_matching_topic_count(topic_counts, topic_predicate, expected_count: int, timeout_s: float = 60.0):
    deadline = time.monotonic() + timeout_s
    while time.monotonic() < deadline:
        current_count = topic_count_matching(topic_counts, topic_predicate)
        if current_count >= expected_count:
            return
        time.sleep(0.2)
    raise TimeoutError(
        f"Timeout waiting for matching topics to reach count {expected_count}. "
        f"Current count is {topic_count_matching(topic_counts, topic_predicate)}."
    )


def clear_status_fifo_pending(everest_core: EverestCore) -> None:
    listener = everest_core.status_listener
    if hasattr(listener, "discard_pending"):
        listener.discard_pending()
        return
    if hasattr(listener, "_pending_lines"):
        listener._pending_lines.clear()
    if hasattr(listener, "_read_buffer"):
        listener._read_buffer = b""


@pytest.mark.everest_core_config("config-sil-immortal_manager.yaml")
def test_manager_stop_during_startup(everest_core: EverestCore):
    start_exception = []
    shutdown_errors = []

    def start_core():
        try:
            everest_core.start()
        except Exception as exc:
            start_exception.append(exc)

    def wait_for_sigint():
        try:
            everest_core.wait_for_manager_status(ManagerStatusFifo.SIGINT_RECEIVED, timeout_s=60.0)
        except Exception as exc:
            shutdown_errors.append(exc)

    starter_thread = threading.Thread(target=start_core)
    sigint_thread = threading.Thread(target=wait_for_sigint)
    starter_thread.start()

    everest_core.wait_for_manager_status(ManagerStatusFifo.MANAGER_STARTING_MODULES, timeout_s=60.0)
    sigint_thread.start()
    everest_core.stop()
    sigint_thread.join(timeout=60.0)
    starter_thread.join(timeout=60.0)

    assert not shutdown_errors, shutdown_errors
    assert not starter_thread.is_alive(), "Startup thread did not exit after stop()."


@pytest.mark.everest_core_config("config-sil-immortal_manager.yaml")
@pytest.mark.everest_manager_args("--graceful-shutdown")
def test_manager_graceful_shutdown_from_running(everest_core: EverestCore):
    shutdown_errors = []

    def wait_for_shutdown():
        try:
            everest_core.wait_for_manager_status(ManagerStatusFifo.SIGINT_RECEIVED, timeout_s=60.0)
            everest_core.wait_for_manager_status(ManagerStatusFifo.ALL_MODULES_STOPPED_CLEAN, timeout_s=60.0)
        except Exception as exc:
            shutdown_errors.append(exc)

    everest_core.start()

    shutdown_thread = threading.Thread(target=wait_for_shutdown)
    shutdown_thread.start()
    everest_core.stop()
    shutdown_thread.join(timeout=60.0)

    assert not shutdown_errors, shutdown_errors


@pytest.mark.everest_core_config("config-sil-immortal_manager.yaml")
@pytest.mark.everest_manager_args("--into-idle")
def test_manager_into_idle_skips_module_startup(everest_core: EverestCore):
    start_exception = []

    def start_core():
        try:
            everest_core.start()
        except Exception as exc:
            start_exception.append(exc)

    starter_thread = threading.Thread(target=start_core)
    starter_thread.start()

    everest_core.wait_for_manager_status(ManagerStatusFifo.MANAGER_IDLE, timeout_s=60.0)
    everest_core.assert_no_manager_status(ManagerStatusFifo.ALL_MODULES_STARTED, timeout_s=5.0)
    everest_core.stop()
    starter_thread.join(timeout=60.0)

    assert not starter_thread.is_alive(), "Startup thread did not exit after stop()."
    assert start_exception, "Expected start() to fail because no modules are started with --into-idle."
    assert all(isinstance(exc, TimeoutError) for exc in start_exception), (
        f"Unexpected startup exception(s): {start_exception}"
    )


@pytest.mark.everest_core_config("config-sil-immortal_manager.yaml")
@pytest.mark.everest_manager_args("--recover-module-crashes", "--graceful-shutdown")
def test_manager_restarts_modules_after_unexpected_exit(
    everest_core: EverestCore, connected_mqtt_client
):
    topic_counts = defaultdict(int)
    mqtt_everest_prefix = f"everest_{everest_core.everest_uuid}"
    mqtt_external_prefix = everest_core.mqtt_external_prefix
    ready_topic_suffix = "/modules/exit_simulator/ready"
    ready_topic_filter = f"{mqtt_everest_prefix}/modules/+/ready"
    exit_cmd_topic = f"{mqtt_external_prefix}everest_api/exit_simulator/cmd/exit"
    ready_topic_predicate = lambda topic: topic.startswith(f"{mqtt_everest_prefix}/") and topic.endswith(ready_topic_suffix)

    def on_message(_client, _userdata, msg):
        topic_counts[msg.topic] += 1

    connected_mqtt_client.on_message = on_message
    connected_mqtt_client.subscribe(ready_topic_filter)
    time.sleep(0.2)

    everest_core.start()

    wait_for_matching_topic_count(topic_counts, ready_topic_predicate, 1, timeout_s=60.0)
    baseline_ready = topic_count_matching(topic_counts, ready_topic_predicate)

    connected_mqtt_client.publish(exit_cmd_topic, "1")

    everest_core.wait_for_manager_status(ManagerStatusFifo.ALL_MODULES_STARTED, timeout_s=60.0)

    wait_for_matching_topic_count(
        topic_counts,
        ready_topic_predicate,
        baseline_ready + 1,
        timeout_s=60.0,
    )


@pytest.mark.everest_core_config("config-sil-immortal_manager.yaml")
@pytest.mark.everest_manager_args("--recover-module-crashes", "--graceful-shutdown")
def test_manager_restarts_modules_after_unexpected_exit_max_3_times(
    everest_core: EverestCore, connected_mqtt_client
):
    mqtt_external_prefix = everest_core.mqtt_external_prefix
    exit_cmd_topic = f"{mqtt_external_prefix}everest_api/exit_simulator/cmd/exit"

    everest_core.start()

    expected_status_sets = [
        [
            ManagerStatusFifo.MANAGER_CRASH_SHUTDOWN_IN_PROGRESS,
            ManagerStatusFifo.crash_recovery_attempt(1, 3),
            ManagerStatusFifo.ALL_MODULES_STARTED,
        ],
        [
            ManagerStatusFifo.crash_recovery_attempt(2, 3),
            ManagerStatusFifo.ALL_MODULES_STARTED,
        ],
        [
            ManagerStatusFifo.crash_recovery_attempt(3, 3),
            ManagerStatusFifo.ALL_MODULES_STARTED,
        ],
        [
            ManagerStatusFifo.CRASH_RECOVERY_EXHAUSTED,
        ],
    ]

    for status_set in expected_status_sets:
        connected_mqtt_client.publish(exit_cmd_topic, "1")
        for status in status_set:
            everest_core.wait_for_manager_status(status, timeout_s=60.0)

    everest_core.assert_no_manager_status(
        ManagerStatusFifo.crash_recovery_attempt(1, 3),
        timeout_s=10.0,
    )
    everest_core.assert_no_manager_status(ManagerStatusFifo.ALL_MODULES_STARTED, timeout_s=10.0)


@pytest.mark.everest_core_config("config-sil-immortal_manager.yaml")
def test_manager_does_not_transition_back_to_running_when_stopped_during_startup(
    everest_core: EverestCore
):
    start_exception = []

    def start_core():
        try:
            everest_core.start()
        except Exception as exc:
            start_exception.append(exc)

    starter_thread = threading.Thread(target=start_core)
    starter_thread.start()

    everest_core.wait_for_manager_status(ManagerStatusFifo.MANAGER_STARTING_MODULES, timeout_s=60.0)
    everest_core.stop()
    everest_core.wait_for_manager_status(ManagerStatusFifo.MANAGER_SHUTDOWN_REQUESTED, timeout_s=60.0)
    starter_thread.join(timeout=60.0)

    assert not starter_thread.is_alive(), "Startup thread did not exit after stop()."
    if start_exception:
        assert all(isinstance(exc, TimeoutError) for exc in start_exception), (
            f"Unexpected startup exception(s): {start_exception}"
        )

    # Ignore lifecycle events that may have been emitted before/during the first
    # shutdown request (modules can become ready in the same window as stop()).
    # What must not happen is a restart back to Running after shutdown was requested.
    clear_status_fifo_pending(everest_core)
    everest_core.assert_no_manager_status(ManagerStatusFifo.ALL_MODULES_STARTED, timeout_s=5.0)
    everest_core.assert_no_manager_status(ManagerStatusFifo.MANAGER_RUNNING, timeout_s=5.0)


@pytest.mark.everest_core_config("config-sil-immortal_manager.yaml")
@pytest.mark.everest_manager_args("--recover-module-crashes", "--graceful-shutdown")
def test_manager_recovers_after_crash_with_blocked_module_timeout(
    everest_core: EverestCore, connected_mqtt_client
):
    topic_counts = defaultdict(int)
    mqtt_everest_prefix = f"everest_{everest_core.everest_uuid}"
    mqtt_external_prefix = everest_core.mqtt_external_prefix
    block_ready_topic_suffix = "/modules/block_simulator/ready"
    block_ready_topic_filter = f"{mqtt_everest_prefix}/modules/+/ready"
    block_cmd_topic = f"{mqtt_external_prefix}everest_api/block_simulator/cmd/block"
    exit_cmd_topic = f"{mqtt_external_prefix}everest_api/exit_simulator/cmd/exit"
    block_ready_topic_predicate = lambda topic: topic.startswith(f"{mqtt_everest_prefix}/") and topic.endswith(
        block_ready_topic_suffix
    )

    def on_message(_client, _userdata, msg):
        topic_counts[msg.topic] += 1

    connected_mqtt_client.on_message = on_message
    connected_mqtt_client.subscribe(block_ready_topic_filter)
    time.sleep(0.2)

    everest_core.start()

    wait_for_matching_topic_count(topic_counts, block_ready_topic_predicate, 1, timeout_s=60.0)
    baseline_ready = topic_count_matching(topic_counts, block_ready_topic_predicate)
    connected_mqtt_client.publish(block_cmd_topic, "")
    time.sleep(0.5)

    connected_mqtt_client.publish(exit_cmd_topic, "1")

    everest_core.wait_for_manager_status(ManagerStatusFifo.FORCE_SHUTDOWN_TIMEOUT, timeout_s=60.0)
    everest_core.wait_for_manager_status(ManagerStatusFifo.ALL_MODULES_STARTED, timeout_s=60.0)

    wait_for_matching_topic_count(
        topic_counts,
        block_ready_topic_predicate,
        baseline_ready + 1,
        timeout_s=60.0,
    )


@pytest.mark.everest_core_config("config-sil-immortal_manager.yaml")
def test_manager_default_stop_terminates_modules_immediately(everest_core: EverestCore):
    """Without --graceful-shutdown, stop() must force-terminate modules right away:
    no MQTT shutdown drain, no FORCE_SHUTDOWN_TIMEOUT event, no clean-stop event."""
    everest_core.start()
    everest_core.stop()

    everest_core.wait_for_manager_status(ManagerStatusFifo.SIGINT_RECEIVED, timeout_s=5.0)
    everest_core.wait_for_manager_status(ManagerStatusFifo.MANAGER_FORCE_TERMINATING, timeout_s=5.0)
    everest_core.wait_for_manager_status(ManagerStatusFifo.MANAGER_EXITING, timeout_s=5.0)
    # Immediate termination is expected behavior, not a graceful-shutdown timeout.
    everest_core.assert_no_manager_status(ManagerStatusFifo.FORCE_SHUTDOWN_TIMEOUT, timeout_s=1.0)
    everest_core.assert_no_manager_status(ManagerStatusFifo.ALL_MODULES_STOPPED_CLEAN, timeout_s=1.0)

    assert everest_core.process.returncode == 0


@pytest.mark.everest_core_config("config-sil-immortal_manager.yaml")
def test_manager_default_crash_terminates_remaining_modules_and_exits(
    everest_core: EverestCore, connected_mqtt_client
):
    """Without --recover-module-crashes and --graceful-shutdown, an unexpected module exit must
    lead to immediate termination of the remaining modules and a manager exit with failure."""
    mqtt_external_prefix = everest_core.mqtt_external_prefix
    exit_cmd_topic = f"{mqtt_external_prefix}everest_api/exit_simulator/cmd/exit"

    everest_core.start()

    connected_mqtt_client.publish(exit_cmd_topic, "1")

    everest_core.wait_for_manager_status(ManagerStatusFifo.MANAGER_CRASH_SHUTDOWN_IN_PROGRESS, timeout_s=60.0)
    everest_core.wait_for_manager_status(ManagerStatusFifo.MANAGER_FORCE_TERMINATING, timeout_s=60.0)
    everest_core.wait_for_manager_status(ManagerStatusFifo.MANAGER_EXITING, timeout_s=60.0)
    everest_core.assert_no_manager_status(ManagerStatusFifo.FORCE_SHUTDOWN_TIMEOUT, timeout_s=1.0)
    everest_core.assert_no_manager_status(
        ManagerStatusFifo.crash_recovery_attempt(1, 3),
        timeout_s=1.0,
    )

    assert everest_core.process.wait(timeout=60.0) != 0
