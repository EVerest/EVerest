#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright Pionix GmbH and Contributors to EVerest

"""Integration tests for the manager's configuration management API.

Covers what docs/source/tutorials/management_apis.rst describes for the
configuration API: slot listing/metadata, loading module configurations from
YAML, slot duplication/deletion, marking the next boot slot, configuration
parameter updates (Applied / WillApplyOnRestart / Rejected) and the
active_slot / config_updates monitor topics. Driven via raw MQTT.

Uses config-example.yaml (as the tutorial does): its `example` module has the
mutable module parameter `log_interval` (updates to smaller values are applied
live) and the implementation parameter `example/enum_test2` whose live updates
are always rejected by the module (the API persists them for the next restart).
"""

import pytest

from everest.testing.core_utils.everest_core import EverestCore
from everest.testing.core_utils.fixtures import *

from assertions import assert_status_subsequence
from configuration_api_client import ConfigurationApiClient, configuration_command_topic
from example_config import example_active_modules_yaml
from mqtt_rpc import perform_rpc

# The configuration API topics are not test-instance specific (no per-test MQTT
# prefix), so these tests must not run concurrently with each other.
pytestmark = pytest.mark.xdist_group(name="management_api")


@pytest.mark.everest_core_config("config-example.yaml")
@pytest.mark.everest_manager_args("--configuration-api=ro")
def test_ro_reads_and_write_rejections(everest_core: EverestCore,
                                       configuration_client: ConfigurationApiClient):
    everest_core.start()
    running = configuration_client.wait_for_active_slot_status("Running")
    assert running["active_slot_id"] == 0
    statuses = [n["status"] for n in configuration_client.active_slot_notices]
    assert_status_subsequence(statuses, ["Stopped", "Starting", "Running"])

    slots = configuration_client.list_all_slots()["slots"]
    assert [s["slot_id"] for s in slots] == [0]
    assert slots[0]["config_file_path"]
    assert slots[0]["last_updated"]

    active_slot = configuration_client.get_active_slot()
    assert active_slot["active_slot_id"] == 0
    assert active_slot["next_boot_slot_id"] == 0

    configuration = configuration_client.get_configuration(0)
    assert configuration["status"] == "Success"
    module_ids = {m["module_id"] for m in configuration["module_configurations"]}
    assert {"example", "example_user", "store"} <= module_ids

    # write commands must all be rejected in read-only mode
    mark_result = configuration_client.mark_active_slot(0)
    assert mark_result["result"] == "AccessDenied"
    delete_result = configuration_client.delete_slot(0)
    assert delete_result["result"] == "AccessDenied"
    duplicate_result = configuration_client.duplicate_slot(0)
    assert duplicate_result["success"] is False
    load_result = configuration_client.load_from_yaml("active_modules: {}")
    assert load_result["success"] is False
    assert load_result["error_message"] == "Not Allowed"
    set_description_result = configuration_client.set_description(0, "nope")
    assert set_description_result["success"] is False
    update_result = configuration_client.set_config_parameters(
        0, [("example", "log_interval", None, "3")])
    assert update_result["results"] == ["Rejected"]


@pytest.mark.everest_core_config("config-example.yaml")
@pytest.mark.everest_manager_args("--configuration-api=rw")
def test_rw_slot_management(everest_core: EverestCore,
                            configuration_client: ConfigurationApiClient):
    everest_core.start()
    configuration_client.wait_for_active_slot_status("Running")

    load_result = configuration_client.load_from_yaml(
        example_active_modules_yaml(everest_core), description="Example setup")
    assert load_result["success"] is True
    assert load_result["slot_id"] == 1

    slots = configuration_client.list_all_slots()["slots"]
    assert [s["slot_id"] for s in slots] == [0, 1]
    assert next(s for s in slots if s["slot_id"] == 1)["description"] == "Example setup"

    set_description_result = configuration_client.set_description(1, "renamed")
    assert set_description_result["success"] is True
    slots = configuration_client.list_all_slots()["slots"]
    assert next(s for s in slots if s["slot_id"] == 1)["description"] == "renamed"

    duplicate_result = configuration_client.duplicate_slot(1, "duplicate of 1")
    assert duplicate_result["success"] is True
    assert duplicate_result["slot_id"] == 2
    slots = configuration_client.list_all_slots()["slots"]
    assert [s["slot_id"] for s in slots] == [0, 1, 2]
    assert next(s for s in slots if s["slot_id"] == 2)["description"] == "duplicate of 1"

    delete_result = configuration_client.delete_slot(2)
    assert delete_result["result"] == "Success"
    delete_result = configuration_client.delete_slot(0)
    assert delete_result["result"] == "CannotDeleteActiveSlot"
    delete_result = configuration_client.delete_slot(99)
    assert delete_result["result"] == "DoesNotExist"

    mark_result = configuration_client.mark_active_slot(1)
    assert mark_result["result"] == "Success"
    active_slot = configuration_client.get_active_slot()
    assert active_slot["active_slot_id"] == 0
    assert active_slot["next_boot_slot_id"] == 1


@pytest.mark.everest_core_config("config-example.yaml")
@pytest.mark.everest_manager_args("--configuration-api=rw")
def test_rw_parameter_update_inactive_slot(everest_core: EverestCore,
                                           configuration_client: ConfigurationApiClient):
    everest_core.start()
    configuration_client.wait_for_active_slot_status("Running")

    assert configuration_client.duplicate_slot(0, "editable copy")["slot_id"] == 1

    update_result = configuration_client.set_config_parameters(
        1, [("example", "log_interval", None, "3")])
    assert update_result["results"] == ["WillApplyOnRestart"]

    get_result = configuration_client.get_config_parameters(1, [("example", "log_interval", None)])
    assert get_result["status"] == "Success"
    (value_result,) = get_result["parameter_values"]
    assert value_result["status"] == "OK"
    assert str(value_result["parameter"]["value"]) == "3"


@pytest.mark.everest_core_config("config-example.yaml")
@pytest.mark.everest_manager_args("--configuration-api=rw")
def test_rw_parameter_update_active_slot_applied_and_rejected(
        everest_core: EverestCore, configuration_client: ConfigurationApiClient):
    everest_core.start()
    configuration_client.wait_for_active_slot_status("Running")

    mark = configuration_client.mark_config_update_stream()
    # log_interval 10 -> 3 is applied live by the Example module; enum_test2 rejects every
    # live update, but the change is persisted anyway and reported as WillApplyOnRestart
    # (see ConfigServiceCore::apply_active_slot_updates)
    result = configuration_client.set_config_parameters(
        0,
        [("example", "log_interval", None, "3"),
         ("example", "enum_test2", "example", "1")])
    assert result["results"] == ["Applied", "WillApplyOnRestart"]

    def is_log_interval_applied(notice):
        return notice["slot_id"] == 0 and any(
            r["update"]["cfg_param_id"]["parameter_name"] == "log_interval"
            and r["result"] == "Applied"
            for r in notice["update_results"])

    notice = configuration_client.wait_for_config_update(is_log_interval_applied,
                                                         after_index=mark)
    assert notice["origin"]["external"] is True


@pytest.mark.everest_core_config("config-example.yaml")
@pytest.mark.everest_manager_args("--configuration-api=rw")
def test_marked_slot_becomes_active_after_restart(everest_core: EverestCore,
                                                  configuration_client: ConfigurationApiClient):
    everest_core.start()
    configuration_client.wait_for_active_slot_status("Running")

    load_result = configuration_client.load_from_yaml(
        example_active_modules_yaml(everest_core), description="restart target")
    assert load_result["success"] is True
    assert load_result["slot_id"] == 1
    mark_result = configuration_client.mark_active_slot(1)
    assert mark_result["result"] == "Success"

    # restart the manager on the same database (EverestCore never passes --reset-from-yaml,
    # so the marked slot must win over the YAML config)
    everest_core.stop()
    mark = configuration_client.mark_active_slot_stream()
    everest_core.start()

    running = configuration_client.wait_for_active_slot_status("Running", after_index=mark)
    assert running["active_slot_id"] == 1
    active_slot = configuration_client.get_active_slot()
    assert active_slot["active_slot_id"] == 1
    assert active_slot["next_boot_slot_id"] == 1


@pytest.mark.everest_core_config("config-example.yaml")
@pytest.mark.everest_manager_args("--configuration-api=rw")
def test_force_read_configuration_from_db(everest_core: EverestCore,
                                          configuration_client: ConfigurationApiClient):
    everest_core.start()
    configuration_client.wait_for_active_slot_status("Running")

    # check the initial configuration (runtime view of the active slot)
    get_result = configuration_client.get_config_parameters(
        0, [("example", "enum_test2", "example")], force_read_from_db=False)
    assert get_result["status"] == "Success"
    (value_result,) = get_result["parameter_values"]
    assert value_result["status"] == "OK"
    assert str(value_result["parameter"]["value"]) == "2"

    # change a parameter that does not get applied immediately: enum_test2 rejects
    # live updates, so the new value is only persisted to the database
    update_result = configuration_client.set_config_parameters(
        0, [("example", "enum_test2", "example", "3")])
    assert update_result["results"] == ["WillApplyOnRestart"]

    # check the runtime configuration to be unchanged
    get_result = configuration_client.get_config_parameters(
        0, [("example", "enum_test2", "example")], force_read_from_db=False)
    assert get_result["status"] == "Success"
    (value_result,) = get_result["parameter_values"]
    assert value_result["status"] == "OK"
    assert str(value_result["parameter"]["value"]) == "2"

    # force reading from the db to see the new value
    get_result = configuration_client.get_config_parameters(
        0, [("example", "enum_test2", "example")], force_read_from_db=True)
    assert get_result["status"] == "Success"
    (value_result,) = get_result["parameter_values"]
    assert value_result["status"] == "OK"
    assert str(value_result["parameter"]["value"]) == "3"


@pytest.mark.everest_core_config("config-example.yaml")
@pytest.mark.everest_manager_args("--configuration-api=rw")
def test_malformed_payload_gets_error_reply(everest_core: EverestCore,
                                            configuration_client: ConfigurationApiClient,
                                            connected_mqtt_client):
    everest_core.start()
    configuration_client.wait_for_active_slot_status("Running")

    # mark_active_slot expects an integer slot_id; a string cannot be deserialized. The handler
    # must still answer with the command's failure shape so the client sees an error within the
    # normal RPC timeout instead of hanging until it times out.
    reply = perform_rpc(connected_mqtt_client,
                        configuration_command_topic("mark_active_slot"),
                        {"slot_id": "not-an-integer"})
    assert reply["result"] == "Failed"
