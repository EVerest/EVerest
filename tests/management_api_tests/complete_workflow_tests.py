#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright Pionix GmbH and Contributors to EVerest

"""Integration test for the "Complete Workflow Example" of
docs/source/tutorials/management_apis.rst.

Boots the manager on a fresh (non-existing) configuration database with an
empty module configuration into idle, then drives both management APIs
together: load a module configuration from YAML, mark it for the next boot,
modify configuration parameters and start the modules from it. Afterwards a
second slot is loaded and activated via lifecycle stop/start without
restarting the manager.
"""

import pytest

from everest.testing.core_utils.everest_core import EverestCore, ManagerStatusFifo
from everest.testing.core_utils.fixtures import *

from background_manager_start import background_manager_start
from configuration_api_client import ConfigurationApiClient
from example_config import example_active_modules_yaml
from lifecycle_api_client import LifecycleApiClient

# The management API topics are not test-instance specific (no per-test MQTT
# prefix), so these tests must not run concurrently with each other.
pytestmark = pytest.mark.xdist_group(name="management_api")


@pytest.mark.everest_core_config("config-empty.yaml")
@pytest.mark.everest_manager_args("--configuration-api=rw", "--lifecycle-api=rw", "--into-idle")
def test_complete_workflow_from_idle(everest_core: EverestCore,
                                     lifecycle_client: LifecycleApiClient,
                                     configuration_client: ConfigurationApiClient):
    # the manager must bootstrap everything from scratch: no database may exist yet
    assert not everest_core.db_path.exists()

    # --into-idle keeps start() blocked until start_modules is requested below, so it
    # has to run on a background thread across the idle-phase work
    with background_manager_start(everest_core) as (starter_thread, start_exception):
        everest_core.wait_for_manager_status(ManagerStatusFifo.MANAGER_IDLE, timeout_s=20.0)
        lifecycle_client.wait_for_module_status("NotRunning")

        # the fresh database was seeded with the empty module configuration as slot 0
        slots = configuration_client.list_all_slots()["slots"]
        assert [s["slot_id"] for s in slots] == [0]
        active_slot = configuration_client.get_active_slot()
        assert active_slot["active_slot_id"] == 0
        assert active_slot["next_boot_slot_id"] == 0

        # load the example module configuration into a new slot ...
        load_result = configuration_client.load_from_yaml(
            example_active_modules_yaml(everest_core), description="Example")
        assert load_result["success"] is True
        assert load_result["slot_id"] == 1
        # ... which, unlike the YAML-seeded slot 0, has no config file associated
        slots = configuration_client.list_all_slots()["slots"]
        assert not next(s for s in slots if s["slot_id"] == 1).get("config_file_path")

        # mark the new slot as the one to boot from
        mark_result = configuration_client.mark_active_slot(1)
        assert mark_result["result"] == "Success"
        active_slot = configuration_client.get_active_slot()
        assert active_slot["active_slot_id"] == 0
        assert active_slot["next_boot_slot_id"] == 1

        # modify parameters in the marked slot; no modules are running, so every
        # change is persisted for the upcoming start
        result = configuration_client.set_config_parameters(
            1,
            [("example", "log_interval", None, "3"),
             ("example", "enum_test2", "example", "1")])
        assert result["results"] == ["WillApplyOnRestart", "WillApplyOnRestart"]

        # finally start the modules from the marked slot
        mark = lifecycle_client.mark()
        reply = lifecycle_client.rpc("start_modules")
        assert reply["status"] == "Starting"

        starter_thread.join(timeout=60.0)
        assert not starter_thread.is_alive(), "Startup thread did not finish after start_modules."
        assert not start_exception, f"Unexpected startup exception(s): {start_exception}"

    lifecycle_client.wait_for_module_status("Running", after_index=mark)
    everest_core.wait_for_manager_status(ManagerStatusFifo.MANAGER_RUNNING)

    running = configuration_client.wait_for_active_slot_status("Running")
    assert running["active_slot_id"] == 1
    active_slot = configuration_client.get_active_slot()
    assert active_slot["active_slot_id"] == 1
    assert active_slot["next_boot_slot_id"] == 1

    # the modules came up with the parameter changes made before the start
    get_result = configuration_client.get_config_parameters(
        1, [("example", "log_interval", None), ("example", "enum_test2", "example")])
    assert get_result["status"] == "Success"
    values = [str(v["parameter"]["value"]) for v in get_result["parameter_values"]]
    assert values == ["3", "1"]

    # --- second part of the tutorial workflow: switch to a different slot via
    # lifecycle stop/start, without restarting the manager ---

    load_result = configuration_client.load_from_yaml(
        example_active_modules_yaml(everest_core), description="second slot")
    assert load_result["success"] is True
    assert load_result["slot_id"] == 2
    mark_result = configuration_client.mark_active_slot(2)
    assert mark_result["result"] == "Success"
    active_slot = configuration_client.get_active_slot()
    assert active_slot["active_slot_id"] == 1
    assert active_slot["next_boot_slot_id"] == 2

    everest_core.discard_manager_status_pending()

    mark = lifecycle_client.mark()
    reply = lifecycle_client.rpc("stop_modules")
    assert reply["status"] == "Stopping"
    lifecycle_client.wait_for_module_status("NotRunning", after_index=mark)
    everest_core.wait_for_manager_status(ManagerStatusFifo.MANAGER_IDLE)

    mark = lifecycle_client.mark()
    reply = lifecycle_client.rpc("start_modules")
    assert reply["status"] == "Starting"
    lifecycle_client.wait_for_module_status("Running", after_index=mark)
    everest_core.wait_for_manager_status(ManagerStatusFifo.ALL_MODULES_STARTED)
    everest_core.wait_for_manager_status(ManagerStatusFifo.MANAGER_RUNNING)

    active_slot = configuration_client.get_active_slot()
    assert active_slot["active_slot_id"] == 2
    assert active_slot["next_boot_slot_id"] == 2

    # slot 2 was loaded from the unmodified YAML, so the changes made to slot 1
    # must not be visible anymore
    get_result = configuration_client.get_config_parameters(
        2, [("example", "log_interval", None), ("example", "enum_test2", "example")])
    assert get_result["status"] == "Success"
    values = [str(v["parameter"]["value"]) for v in get_result["parameter_values"]]
    assert values == ["10", "2"]
