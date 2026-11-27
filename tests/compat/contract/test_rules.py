# SPDX-License-Identifier: Apache-2.0
# Copyright Pionix GmbH and Contributors to EVerest

import copy

import pytest

from contract import rules
from contract.rules import ADD, BREAKING, EDIT, NON_BREAKING, REMOVE, UNCLASSIFIED, Direction


def classes(findings, pointer_suffix=None):
    return {
        (f.pointer, f.kind, f.classification)
        for f in findings
        if pointer_suffix is None or f.pointer.endswith(pointer_suffix)
    }


def single(findings):
    assert len(findings) == 1, [str(f) for f in findings]
    return findings[0]


# ---------------------------------------------------------------------------------------------------
# Message payload schemas


def payload(**overrides):
    schema = {
        "type": "object",
        "required": ["id"],
        "properties": {
            "id": {"type": "integer"},
            "soc": {"type": "number", "minimum": 0, "maximum": 100},
            "state": {"type": "string", "enum": ["A", "B"]},
        },
    }
    schema.update(overrides)
    return schema


def test_identical_schema_has_no_findings():
    assert rules.diff_schema(payload(), payload(), "p", Direction.TO_CLIENT) == []


def test_removed_field_is_breaking_in_both_directions():
    new = payload()
    del new["properties"]["soc"]
    for direction in Direction:
        finding = single(rules.diff_schema(payload(), new, "p", direction))
        assert (finding.pointer, finding.kind, finding.classification) == ("p/properties/soc", REMOVE, BREAKING)


def test_added_optional_field_is_non_breaking():
    new = payload()
    new["properties"]["extra"] = {"type": "string"}
    finding = single(rules.diff_schema(payload(), new, "p", Direction.FROM_CLIENT))
    assert (finding.kind, finding.classification) == (ADD, NON_BREAKING)


def test_new_required_field_depends_on_direction():
    new = payload()
    new["properties"]["extra"] = {"type": "string"}
    new["required"] = ["id", "extra"]
    assert single(rules.diff_schema(payload(), new, "p", Direction.FROM_CLIENT)).classification == BREAKING
    assert single(rules.diff_schema(payload(), new, "p", Direction.UNKNOWN)).classification == BREAKING
    assert single(rules.diff_schema(payload(), new, "p", Direction.TO_CLIENT)).classification == NON_BREAKING


def test_existing_field_becoming_required_is_breaking_for_client_sent_messages():
    new = payload(required=["id", "soc"])
    finding = single(rules.diff_schema(payload(), new, "p", Direction.FROM_CLIENT))
    assert (finding.pointer, finding.classification) == ("p/required/soc", BREAKING)
    assert single(rules.diff_schema(payload(), new, "p", Direction.TO_CLIENT)).classification == NON_BREAKING


def test_required_field_becoming_optional_is_non_breaking():
    new = payload(required=[])
    finding = single(rules.diff_schema(payload(), new, "p", Direction.TO_CLIENT))
    assert (finding.pointer, finding.kind, finding.classification) == ("p/required/id", REMOVE, NON_BREAKING)


def test_type_change_is_breaking():
    new = payload()
    new["properties"]["id"]["type"] = "string"
    finding = single(rules.diff_schema(payload(), new, "p", Direction.TO_CLIENT))
    assert (finding.pointer, finding.classification) == ("p/properties/id/type", BREAKING)


def test_enum_values():
    new = payload()
    new["properties"]["state"]["enum"] = ["A", "C"]
    found = classes(rules.diff_schema(payload(), new, "p", Direction.TO_CLIENT))
    assert ("p/properties/state/enum", REMOVE, BREAKING) in found
    assert ("p/properties/state/enum", ADD, NON_BREAKING) in found


def test_constraint_added_where_none_existed_breaks_whoever_provides_the_value():
    for key, value in (("enum", [1, 2]), ("minimum", 0), ("pattern", "^[0-9]+$")):
        new = payload()
        new["properties"]["id"][key] = value
        from_client = single(rules.diff_schema(payload(), new, "p", Direction.FROM_CLIENT))
        assert (from_client.kind, from_client.classification) == (ADD, BREAKING), key
        assert single(rules.diff_schema(payload(), new, "p", Direction.UNKNOWN)).classification == BREAKING, key
        assert single(rules.diff_schema(payload(), new, "p", Direction.TO_CLIENT)).classification == NON_BREAKING, key


def test_enum_constraint_removed_is_non_breaking():
    new = payload()
    del new["properties"]["state"]["enum"]
    for direction in Direction:
        assert single(rules.diff_schema(payload(), new, "p", direction)).classification == NON_BREAKING


def test_payload_default_matters_for_what_the_client_sends():
    old = payload()
    old["properties"]["soc"]["default"] = 50
    for new_default in (80, None):
        new = payload()
        if new_default is not None:
            new["properties"]["soc"]["default"] = new_default
        assert single(rules.diff_schema(old, new, "p", Direction.FROM_CLIENT)).classification == BREAKING
        assert single(rules.diff_schema(old, new, "p", Direction.TO_CLIENT)).classification == NON_BREAKING


def test_range_widening_and_narrowing():
    wider = payload()
    wider["properties"]["soc"]["minimum"] = -10
    assert single(rules.diff_schema(payload(), wider, "p", Direction.FROM_CLIENT)).classification == NON_BREAKING
    narrower = payload()
    narrower["properties"]["soc"]["maximum"] = 90
    assert single(rules.diff_schema(payload(), narrower, "p", Direction.FROM_CLIENT)).classification == BREAKING


def test_missing_schema_equals_the_empty_schema():
    assert rules.diff_schema(None, {}, "p", Direction.FROM_CLIENT) == []
    constrained = rules.diff_schema(None, {"type": "object", "required": ["id"]}, "p", Direction.FROM_CLIENT)
    assert constrained and all(finding.classification == BREAKING for finding in constrained)
    assert single(rules.diff_schema({}, True, "p", Direction.FROM_CLIENT)).classification == UNCLASSIFIED


def test_description_changes_are_ignored():
    new = payload(description="new text")
    old = payload(description="old text")
    assert rules.diff_schema(old, new, "p", Direction.TO_CLIENT) == []


def test_unknown_keyword_change_is_unclassified():
    new = payload(**{"x-custom": 1})
    assert single(rules.diff_schema(payload(), new, "p", Direction.TO_CLIENT)).classification == UNCLASSIFIED


def test_nested_items_and_oneof_are_recursed():
    old = {"type": "array", "items": {"oneOf": [payload(), {"type": "null"}]}}
    new = copy.deepcopy(old)
    del new["items"]["oneOf"][0]["properties"]["soc"]
    finding = single(rules.diff_schema(old, new, "p", Direction.TO_CLIENT))
    assert finding.pointer == "p/items/oneOf/0/properties/soc"


def test_additional_properties_closed_is_breaking_for_client_sent_messages():
    new = payload(additionalProperties=False)
    assert single(rules.diff_schema(payload(), new, "p", Direction.FROM_CLIENT)).classification == BREAKING
    assert single(rules.diff_schema(payload(), new, "p", Direction.TO_CLIENT)).classification == NON_BREAKING


# ---------------------------------------------------------------------------------------------------
# AsyncAPI


def spec():
    return {
        "version": "1.0.0",
        "servers": {"default": {"host": "localhost:1883", "pathname": "everest_api/1/x/{module_id}", "protocol": "mqtt"}},
        "channels": {
            "receive_status": {"address": "e2m/status", "messages": {"receive_status": {"payload": payload()}}},
            "send_request_get": {"address": "m2e/get", "messages": {"send_request_get": {"payload": payload()}}},
            "receive_reply_get": {"address": None, "messages": {"receive_reply_get": {"payload": payload()}}},
        },
        "operations": {
            "receive_status": {"action": "receive", "channel": "receive_status", "reply_channel": None},
            "send_request_get": {"action": "send", "channel": "send_request_get", "reply_channel": "receive_reply_get"},
        },
    }


def test_channel_directions_follow_operation_actions():
    directions = rules._channel_directions(spec())
    assert directions == {
        "receive_status": Direction.TO_CLIENT,
        "send_request_get": Direction.FROM_CLIENT,
        "receive_reply_get": Direction.TO_CLIENT,
    }


def test_removed_channel_and_operation_are_breaking():
    new = spec()
    del new["channels"]["receive_status"]
    del new["operations"]["receive_status"]
    found = classes(rules.diff_asyncapi({"x": spec()}, {"x": new}))
    assert ("asyncapi/x/channels/receive_status", REMOVE, BREAKING) in found
    assert ("asyncapi/x/operations/receive_status", REMOVE, BREAKING) in found


def test_added_channel_is_non_breaking():
    new = spec()
    new["channels"]["receive_extra"] = {"address": "e2m/extra", "messages": {}}
    assert single(rules.diff_asyncapi({"x": spec()}, {"x": new})).classification == NON_BREAKING


def test_address_change_is_breaking():
    new = spec()
    new["channels"]["receive_status"]["address"] = "e2m/statuses"
    finding = single(rules.diff_asyncapi({"x": spec()}, {"x": new}))
    assert (finding.pointer, finding.classification) == ("asyncapi/x/channels/receive_status/address", BREAKING)


def test_required_field_added_to_request_is_breaking_but_to_reply_is_not():
    new = spec()
    for channel in ("send_request_get", "receive_reply_get"):
        schema = new["channels"][channel]["messages"][channel]["payload"]
        schema["properties"]["extra"] = {"type": "string"}
        schema["required"] = ["id", "extra"]
    found = {f.pointer: f.classification for f in rules.diff_asyncapi({"x": spec()}, {"x": new})}
    assert found["asyncapi/x/channels/send_request_get/messages/send_request_get/payload/required/extra"] == BREAKING
    assert found["asyncapi/x/channels/receive_reply_get/messages/receive_reply_get/payload/required/extra"] == NON_BREAKING


def test_operation_action_and_reply_changes():
    new = spec()
    new["operations"]["send_request_get"]["reply_channel"] = None
    new["operations"]["receive_status"]["action"] = "send"
    found = classes(rules.diff_asyncapi({"x": spec()}, {"x": new}))
    assert ("asyncapi/x/operations/send_request_get/reply", REMOVE, BREAKING) in found
    assert ("asyncapi/x/operations/receive_status/action", EDIT, BREAKING) in found


def test_server_pathname_change_is_breaking_and_version_bump_is_not():
    new = spec()
    new["servers"]["default"]["pathname"] = "everest_api/2/x/{module_id}"
    new["version"] = "2.0.0"
    found = classes(rules.diff_asyncapi({"x": spec()}, {"x": new}))
    assert ("asyncapi/x/servers/default/pathname", EDIT, BREAKING) in found
    assert ("asyncapi/x/version", EDIT, NON_BREAKING) in found


def test_spec_removed_and_added():
    found = classes(rules.diff_asyncapi({"old": spec()}, {"new": spec()}))
    assert found == {("asyncapi/old", REMOVE, BREAKING), ("asyncapi/new", ADD, NON_BREAKING)}


# ---------------------------------------------------------------------------------------------------
# Modules


def module():
    return {
        "path": "modules/EVSE/Mod",
        "config": {
            "timeout": {"type": "integer", "default": 5, "required": False, "minimum": 1},
            "mode": {"type": "string", "default": "a", "enum": ["a", "b"], "required": False},
        },
        "provides": {"main": {"interface": "evse_manager", "config": {}, "properties": {}}},
        "requires": {"bsp": {"interface": "evse_board_support", "min_connections": 1, "max_connections": 1}},
    }


def test_module_removed_and_added():
    found = classes(rules.diff_modules({"A": module()}, {"B": module()}))
    assert found == {("modules/A", REMOVE, BREAKING), ("modules/B", ADD, NON_BREAKING)}


def test_config_option_removed_is_breaking():
    new = module()
    del new["config"]["mode"]
    finding = single(rules.diff_modules({"M": module()}, {"M": new}))
    assert (finding.pointer, finding.kind, finding.classification) == ("modules/M/config/mode", REMOVE, BREAKING)


def test_config_option_added_with_and_without_default():
    new = module()
    new["config"]["with_default"] = {"type": "boolean", "default": False, "required": False}
    new["config"]["mandatory"] = {"type": "string", "required": True}
    found = {f.pointer: f.classification for f in rules.diff_modules({"M": module()}, {"M": new})}
    assert found["modules/M/config/with_default"] == NON_BREAKING
    assert found["modules/M/config/mandatory"] == BREAKING


def test_default_change_removal_and_addition():
    changed = module()
    changed["config"]["timeout"]["default"] = 10
    assert single(rules.diff_modules({"M": module()}, {"M": changed})).classification == BREAKING

    removed = module()
    del removed["config"]["timeout"]["default"]
    removed["config"]["timeout"]["required"] = True
    assert single(rules.diff_modules({"M": module()}, {"M": removed})).classification == BREAKING

    assert single(rules.diff_modules({"M": removed}, {"M": module()})).classification == NON_BREAKING


def test_config_type_change_is_breaking():
    new = module()
    new["config"]["timeout"]["type"] = "number"
    finding = single(rules.diff_modules({"M": module()}, {"M": new}))
    assert (finding.pointer, finding.classification) == ("modules/M/config/timeout/type", BREAKING)


def test_config_enum_and_range_rules():
    new = module()
    new["config"]["mode"]["enum"] = ["a", "b", "c"]
    new["config"]["timeout"]["minimum"] = 2
    found = {f.pointer: f.classification for f in rules.diff_modules({"M": module()}, {"M": new})}
    assert found["modules/M/config/mode/enum"] == NON_BREAKING
    assert found["modules/M/config/timeout/minimum"] == BREAKING


def test_mutability_defaults_to_read_only():
    relaxed = module()
    relaxed["config"]["timeout"]["mutability"] = "ReadWrite"
    assert single(rules.diff_modules({"M": module()}, {"M": relaxed})).classification == NON_BREAKING
    assert single(rules.diff_modules({"M": relaxed}, {"M": module()})).classification == BREAKING


def test_implementation_removed_is_breaking_and_interface_change_is_unclassified():
    renamed = module()
    renamed["provides"] = {"meter": module()["provides"]["main"]}
    found = classes(rules.diff_modules({"M": module()}, {"M": renamed}))
    assert ("modules/M/provides/main", REMOVE, BREAKING) in found
    assert ("modules/M/provides/meter", ADD, NON_BREAKING) in found

    interface = module()
    interface["provides"]["main"]["interface"] = "evse_manager_v2"
    assert single(rules.diff_modules({"M": module()}, {"M": interface})).classification == BREAKING


def test_requirement_rules():
    new = module()
    new["requires"]["optional_extra"] = {"interface": "x", "min_connections": 0, "max_connections": 1}
    new["requires"]["mandatory_extra"] = {"interface": "x", "min_connections": 1, "max_connections": 1}
    new["requires"]["bsp"]["max_connections"] = 2
    found = {f.pointer: f.classification for f in rules.diff_modules({"M": module()}, {"M": new})}
    assert found["modules/M/requires/optional_extra"] == NON_BREAKING
    assert found["modules/M/requires/mandatory_extra"] == BREAKING
    assert found["modules/M/requires/bsp/max_connections"] == NON_BREAKING

    stricter = module()
    stricter["requires"]["bsp"]["min_connections"] = 2
    assert single(rules.diff_modules({"M": module()}, {"M": stricter})).classification == BREAKING

    dropped = module()
    del dropped["requires"]["bsp"]
    finding = single(rules.diff_modules({"M": module()}, {"M": dropped}))
    assert (finding.pointer, finding.classification) == ("modules/M/requires/bsp", BREAKING)


# ---------------------------------------------------------------------------------------------------
# OCPP


def ocpp():
    return {
        "v16_profile_schemas": {
            "Core": {
                "HeartbeatInterval": {"type": "integer", "readOnly": False, "required": True},
                "Optional": {"type": "string", "default": "x", "required": False},
            }
        },
        "component_config": {
            "standardized/OCPPCommCtrlr": {
                "name": "OCPPCommCtrlr",
                "variables": {
                    "HeartbeatInterval": {
                        "characteristics": {"dataType": "integer", "unit": "s", "supportsMonitoring": True},
                        "attributes": {"Actual": {"mutability": "ReadWrite"}},
                        "default": "1800",
                        "required": True,
                    }
                },
            }
        },
        "v16_custom_key_mappings": {"mappings": [{"ocpp16_key": "A"}]},
    }


def test_profile_key_removed_and_type_changed():
    new = ocpp()
    del new["v16_profile_schemas"]["Core"]["Optional"]
    new["v16_profile_schemas"]["Core"]["HeartbeatInterval"]["type"] = "string"
    found = classes(rules.diff_ocpp(ocpp(), new))
    assert ("ocpp/v16/Core/Optional", REMOVE, BREAKING) in found
    assert ("ocpp/v16/Core/HeartbeatInterval/type", EDIT, BREAKING) in found


def test_component_variable_rules():
    new = ocpp()
    variable = new["component_config"]["standardized/OCPPCommCtrlr"]["variables"]["HeartbeatInterval"]
    variable["default"] = "900"
    variable["attributes"]["Actual"]["mutability"] = "ReadOnly"
    variable["characteristics"]["supportsMonitoring"] = False
    variable["characteristics"]["unit"] = "min"
    found = {f.pointer: f.classification for f in rules.diff_ocpp(ocpp(), new)}
    base = "ocpp/component_config/standardized/OCPPCommCtrlr/HeartbeatInterval"
    assert found[f"{base}/default"] == BREAKING
    assert found[f"{base}/attributes/Actual/mutability"] == BREAKING
    assert found[f"{base}/supportsMonitoring"] == BREAKING
    assert found[f"{base}/unit"] == BREAKING


def test_component_variable_relaxations_are_non_breaking():
    old = ocpp()
    variable = old["component_config"]["standardized/OCPPCommCtrlr"]["variables"]["HeartbeatInterval"]
    variable["attributes"]["Actual"]["mutability"] = "ReadOnly"
    variable["characteristics"]["supportsMonitoring"] = False
    assert {f.classification for f in rules.diff_ocpp(old, ocpp())} == {NON_BREAKING}


def test_component_removed_variable_added():
    new = ocpp()
    new["component_config"]["standardized/OCPPCommCtrlr"]["variables"]["Extra"] = {
        "characteristics": {}, "attributes": {}, "default": None, "required": False
    }
    new["component_config"]["custom/EVSE_1"] = {"name": "EVSE", "variables": {}}
    found = classes(rules.diff_ocpp(ocpp(), new))
    assert found == {
        ("ocpp/component_config/standardized/OCPPCommCtrlr/Extra", ADD, NON_BREAKING),
        ("ocpp/component_config/custom/EVSE_1", ADD, NON_BREAKING),
    }
    reverse = {f.pointer: (f.kind, f.classification) for f in rules.diff_ocpp(new, ocpp())}
    assert reverse["ocpp/component_config/custom/EVSE_1"] == (REMOVE, BREAKING)
    assert reverse["ocpp/component_config/standardized/OCPPCommCtrlr/Extra"] == (REMOVE, BREAKING)


def test_profile_key_that_becomes_required():
    new = ocpp()
    new["v16_profile_schemas"]["Core"]["Optional"]["required"] = True
    required = single(rules.diff_ocpp(ocpp(), new))
    assert (required.pointer, required.classification) == ("ocpp/v16/Core/Optional/required", BREAKING)
    assert single(rules.diff_ocpp(new, ocpp())).classification == NON_BREAKING


def test_values_list_is_compared_by_value():
    def variable(values):
        return {"characteristics": {"valuesList": values}}

    assert rules._diff_component_variable(variable("Auto,Manual"), variable("Manual, Auto"), "p") == []
    removed = single(rules._diff_component_variable(variable("AB,C"), variable("AB"), "p"))
    assert (removed.classification, removed.message) == (BREAKING, "valuesList value 'C' removed")
    assert single(rules._diff_component_variable(variable("AB,C"), variable("AB,C,D"), "p")).classification == NON_BREAKING
    assert classes(rules._diff_component_variable(variable("AB,C"), variable("AC,B"), "p")) >= {("p/valuesList", REMOVE, BREAKING)}


def test_custom_key_mappings():
    new = ocpp()
    new["v16_custom_key_mappings"]["mappings"].append({"ocpp16_key": "B"})
    assert single(rules.diff_ocpp(ocpp(), new)).classification == UNCLASSIFIED
    added = ocpp()
    added["v16_custom_key_mappings"] = None
    assert single(rules.diff_ocpp(added, ocpp())).classification == NON_BREAKING


# ---------------------------------------------------------------------------------------------------
# Migrations and RPC API


def test_migration_rules():
    old = {"framework/1_up-initial.sql": "a", "framework/2_up-x.sql": "b", "ocpp_v2_core/1_up-initial.sql": "c"}
    new = {"framework/1_up-initial.sql": "a", "framework/2_up-x.sql": "B", "framework/3_up-y.sql": "d"}
    found = classes(rules.diff_migrations(old, new))
    assert found == {
        ("migrations/framework/2_up-x.sql", EDIT, BREAKING),
        ("migrations/framework/3_up-y.sql", ADD, NON_BREAKING),
        ("migrations/ocpp_v2_core/1_up-initial.sql", REMOVE, BREAKING),
    }


def test_rpc_api_rules():
    old = {"version": "1.1.0", "types": {"A": payload(), "B": payload()}}
    new = {"version": "1.2.0", "types": {"A": payload(required=["id", "soc"]), "C": payload()}}
    found = classes(rules.diff_rpc_api(old, new))
    assert ("rpc_api/version", EDIT, NON_BREAKING) in found
    assert ("rpc_api/types/B", REMOVE, BREAKING) in found
    assert ("rpc_api/types/C", ADD, NON_BREAKING) in found
    assert ("rpc_api/types/A/required/soc", ADD, BREAKING) in found
    assert rules.diff_rpc_api(None, None) == []
    assert single(rules.diff_rpc_api(old, None)).classification == BREAKING


def test_diff_snapshots_covers_every_section():
    old = {"asyncapi": {"x": spec()}, "modules": {"M": module()}, "ocpp": ocpp(), "migrations": {"framework/1_up-initial.sql": "a"}, "rpc_api": None}
    new = copy.deepcopy(old)
    new["modules"]["M"]["config"]["timeout"]["default"] = 1
    new["migrations"]["framework/1_up-initial.sql"] = "b"
    found = classes(rules.diff_snapshots(old, new))
    assert found == {
        ("modules/M/config/timeout/default", EDIT, BREAKING),
        ("migrations/framework/1_up-initial.sql", EDIT, BREAKING),
    }
    assert rules.by_classification([])[BREAKING] == []


@pytest.mark.parametrize("old,new", [([], ["x"]), (["x"], ["x", "y"])])
def test_type_lists_are_order_insensitive(old, new):
    assert rules.diff_schema({"type": ["a", "b"]}, {"type": ["b", "a"]}, "p", Direction.UNKNOWN) == []
    assert single(rules.diff_schema({"type": old}, {"type": new}, "p", Direction.UNKNOWN)).classification == BREAKING


# ---------------------------------------------------------------------------------------------------
# Manager: configuration file, settings file and command line


def config_schema():
    return {
        "type": "object",
        "properties": {
            "settings": {"type": "object", "properties": {"run_as_user": {"type": "string"}}},
            "active_modules": {
                "type": "object",
                "patternProperties": {
                    "^[a-z]+$": {"properties": {"module": {"type": "string"}, "capabilities": {"type": "array"}}}
                },
            },
        },
    }


def test_pattern_properties_are_recursed():
    new = config_schema()
    del new["properties"]["active_modules"]["patternProperties"]["^[a-z]+$"]["properties"]["capabilities"]
    finding = single(rules.diff_framework({"config": config_schema()}, {"config": new}))
    assert finding.pointer == "framework/config/properties/active_modules/patternProperties/^[a-z]+$/properties/capabilities"
    assert (finding.kind, finding.classification) == (REMOVE, BREAKING)

    renamed = config_schema()
    renamed["properties"]["active_modules"]["patternProperties"] = {"^[A-Z]+$": {}}
    found = classes(rules.diff_framework({"config": config_schema()}, {"config": renamed}))
    assert ("framework/config/properties/active_modules/patternProperties/^[a-z]+$", REMOVE, BREAKING) in found
    assert ("framework/config/properties/active_modules/patternProperties/^[A-Z]+$", ADD, NON_BREAKING) in found


def test_framework_schemas():
    removed_key = config_schema()
    del removed_key["properties"]["settings"]
    assert classes(rules.diff_framework({"config": config_schema()}, {"config": removed_key})) == {
        ("framework/config/properties/settings", REMOVE, BREAKING)
    }
    assert single(rules.diff_framework({"config": config_schema()}, {})).classification == BREAKING
    assert rules.diff_framework({}, {"settings": {"type": "object"}}) == []


def manager(**options):
    base = {"config": {"alias": None, "takes_value": True}, "help": {"alias": "h", "takes_value": False}}
    base.update(options)
    return {"options": {name: option for name, option in base.items() if option is not None}}


def test_manager_options():
    assert rules.diff_manager(manager(), manager()) == []
    assert rules.diff_manager(None, manager()) == []
    unreadable = single(rules.diff_manager(manager(), None))
    assert (unreadable.pointer, unreadable.classification) == ("manager/options", BREAKING)
    found = classes(rules.diff_manager(manager(conf={"alias": None, "takes_value": True}), manager(db={"alias": None, "takes_value": True})))
    assert found == {("manager/options/conf", REMOVE, BREAKING), ("manager/options/db", ADD, NON_BREAKING)}
    now_flag = single(rules.diff_manager(manager(), manager(config={"alias": None, "takes_value": False})))
    assert (now_flag.pointer, now_flag.classification) == ("manager/options/config/takes_value", BREAKING)
    no_alias = single(rules.diff_manager(manager(), manager(help={"alias": None, "takes_value": False})))
    assert (no_alias.pointer, no_alias.classification) == ("manager/options/help/alias", BREAKING)


# ---------------------------------------------------------------------------------------------------
# Unreleased components


def finding(pointer, kind=REMOVE, classification=BREAKING):
    return rules.Finding(pointer, kind, classification, "")


def test_change_to_something_added_since_the_release_does_not_concern_it():
    since_release = [finding("modules/New", ADD, NON_BREAKING), finding("modules/Old/config/timeout/default", EDIT)]
    findings = [
        finding("modules/New/config/option"),
        finding("modules/Old/config/added_on_main"),
        finding("modules/Old/config/timeout/default", EDIT),
    ]
    released, unreleased = rules.split_unreleased(findings, since_release)
    assert [f.pointer for f in released] == ["modules/Old/config/timeout/default"]
    assert [f.pointer for f in unreleased] == ["modules/New/config/option", "modules/Old/config/added_on_main"]


def test_compatible_change_is_counted_wherever_it_is():
    compatible = finding("modules/New/config/option", ADD, NON_BREAKING)
    released, unreleased = rules.split_unreleased([compatible], [finding("modules/New", ADD, NON_BREAKING)])
    assert released == [compatible] and not unreleased


def test_breaking_addition_to_a_released_component_concerns_the_release():
    added = finding("modules/Old/config/without_default", ADD, BREAKING)
    released, unreleased = rules.split_unreleased([added], [added])
    assert released == [added] and not unreleased


def test_finding_concerns_the_release_at_a_nested_or_enclosing_place():
    since_release = [finding("modules/Old/config/timeout", EDIT, UNCLASSIFIED), finding("modules/Gone")]
    findings = [finding("modules/Old/config/timeout/default", EDIT), finding("modules/Gone"), finding("modules/Old/config")]
    released, unreleased = rules.split_unreleased(findings, since_release)
    assert len(released) == 3 and not unreleased


def test_similar_names_are_different_places():
    released, unreleased = rules.split_unreleased([finding("modules/OCPP201")], [finding("modules/OCPP")])
    assert not released and len(unreleased) == 1
