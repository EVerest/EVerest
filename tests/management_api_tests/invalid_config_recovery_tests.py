#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright Pionix GmbH and Contributors to EVerest

"""Recovery from an invalid boot configuration through the management APIs.

The manager boots on a configuration that fails validation. The boot slot 0 is
then seeded as an empty placeholder: its config_file_path is the YAML file and
its description records the validation error. A corrected module configuration
is then loaded into that very slot via the configuration API. The load must take
effect on the active configuration immediately (runtime reads and writes of
slot 0 see the loaded modules) and the modules must start from it afterwards.

Both idle flags are covered: --into-idle enters Idle directly (modules
NotRunning), --idle-on-failure settles with FailedToStart.
"""

from pathlib import Path

import pytest

from everest.testing.core_utils.everest_core import EverestCore, ManagerStatusFifo
from everest.testing.core_utils.fixtures import *

from background_manager_start import background_manager_start
from everest_management_api_cli.configuration_api import ConfigurationApiClient
from example_config import example_active_modules_yaml
from everest_management_api_cli.lifecycle_api import LifecycleApiClient

# The management API topics are not test-instance specific (no per-test MQTT
# prefix), so these tests must not run concurrently with each other.
pytestmark = pytest.mark.xdist_group(name="management_api")

EXAMPLE_MODULE_IDS = {"example", "example_user", "store"}
LOG_INTERVAL = ("example", "log_interval", None)


def _recover_by_loading_into_active_slot(everest_core: EverestCore,
                                          lifecycle_client: LifecycleApiClient,
                                          configuration_client: ConfigurationApiClient,
                                          idle_module_status: str):
    # the manager must bootstrap everything from scratch: no database may exist yet
    assert not everest_core.db_path.exists()

    # the modules are started later from within the idle phase, so start() has to run on a
    # background thread until start_modules is requested below
    with background_manager_start(everest_core) as (starter_thread, start_exception):
        everest_core.wait_for_manager_status(ManagerStatusFifo.MANAGER_IDLE, timeout_s=20.0)
        lifecycle_client.wait_for_module_status(idle_module_status)

        # the invalid YAML was seeded as an empty placeholder boot slot that records the failure
        slots = configuration_client.list_all_slots()["slots"]
        assert [s["slot_id"] for s in slots] == [0]
        assert slots[0]["description"]
        assert Path(slots[0]["config_file_path"]).resolve() == everest_core.everest_config_path.resolve()
        configuration = configuration_client.get_configuration(0)
        assert configuration["status"] == "Success"
        assert configuration["module_configurations"] == []
        active_slot = configuration_client.get_active_slot()
        assert active_slot["active_slot_id"] == 0
        assert active_slot["next_boot_slot_id"] == 0

        # push the corrected configuration into the active placeholder slot
        slot_mark = configuration_client.mark_active_slot_stream()
        load_result = configuration_client.load_from_yaml(
            example_active_modules_yaml(everest_core), description="Corrected", slot_id=0)
        assert load_result["success"] is True
        assert load_result["slot_id"] == 0
        slots = configuration_client.list_all_slots()["slots"]
        assert [s["slot_id"] for s in slots] == [0]
        assert slots[0]["description"] == "Corrected"

        # the load is effective without a restart: the runtime view of slot 0 holds the modules
        configuration = configuration_client.get_configuration(0)
        assert configuration["status"] == "Success"
        assert {m["module_id"] for m in configuration["module_configurations"]} == EXAMPLE_MODULE_IDS

        # writes resolve against the loaded modules; nothing runs, so they are persisted for the start
        result = configuration_client.set_config_parameters(0, [LOG_INTERVAL + ("3",)])
        assert result["results"] == ["WillApplyOnRestart"]

        # persist-first: the runtime value stays at its default until the modules start ...
        get_result = configuration_client.get_config_parameters(0, [LOG_INTERVAL])
        assert get_result["status"] == "Success"
        (value_result,) = get_result["parameter_values"]
        assert value_result["status"] == "OK"
        assert str(value_result["parameter"]["value"]) == "10"
        # ... while the database already holds the change
        get_result = configuration_client.get_config_parameters(0, [LOG_INTERVAL], force_read_from_db=True)
        (value_result,) = get_result["parameter_values"]
        assert value_result["status"] == "OK"
        assert str(value_result["parameter"]["value"]) == "3"

        # the reload of the active slot was announced as slot information
        notice = configuration_client.wait_for_active_slot(
            lambda n: n["active_slot_id"] == 0, timeout_s=5.0, after_index=slot_mark)
        assert notice["next_boot_slot_id"] == 0

        mark = lifecycle_client.mark()
        reply = lifecycle_client.rpc("start_modules")
        assert reply["status"] == "Starting"

        starter_thread.join(timeout=60.0)
        assert not starter_thread.is_alive(), "Startup thread did not finish after start_modules."
        assert not start_exception, f"Unexpected startup exception(s): {start_exception}"

    lifecycle_client.wait_for_module_status("Running", after_index=mark)
    everest_core.wait_for_manager_status(ManagerStatusFifo.MANAGER_RUNNING)

    running = configuration_client.wait_for_active_slot_status("Running")
    assert running["active_slot_id"] == 0

    # the modules came up from slot 0 with the parameter change made before the start
    get_result = configuration_client.get_config_parameters(0, [LOG_INTERVAL])
    (value_result,) = get_result["parameter_values"]
    assert value_result["status"] == "OK"
    assert str(value_result["parameter"]["value"]) == "3"


@pytest.mark.everest_core_config("config-test-invalid-module-config.yaml")
@pytest.mark.everest_manager_args("--configuration-api=rw", "--lifecycle-api=rw", "--into-idle")
def test_load_into_active_slot_recovers_invalid_config_with_into_idle(
        everest_core: EverestCore,
        lifecycle_client: LifecycleApiClient,
        configuration_client: ConfigurationApiClient):
    """--into-idle enters Idle directly, so the modules report NotRunning."""
    _recover_by_loading_into_active_slot(everest_core, lifecycle_client, configuration_client, "NotRunning")


@pytest.mark.everest_core_config("config-test-invalid-module-config.yaml")
@pytest.mark.everest_manager_args("--configuration-api=rw", "--lifecycle-api=rw", "--idle-on-failure")
def test_load_into_active_slot_recovers_invalid_config_with_idle_on_failure(
        everest_core: EverestCore,
        lifecycle_client: LifecycleApiClient,
        configuration_client: ConfigurationApiClient):
    """--idle-on-failure settles with FailedToStart, a resting status that still accepts the load."""
    _recover_by_loading_into_active_slot(everest_core, lifecycle_client, configuration_client, "FailedToStart")
