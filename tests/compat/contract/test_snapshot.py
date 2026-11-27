# SPDX-License-Identifier: Apache-2.0
# Copyright Pionix GmbH and Contributors to EVerest

from pathlib import Path

from contract import snapshot

REPO_ROOT = Path(__file__).resolve().parents[3]

MANAGER_SOURCE = """
constexpr std::array<std::string_view, 2> EXPERIMENTAL_OPTIONS{
    "into-idle", "lifecycle-api"};

    desc.add_options()("help,h", "produce help message");
    desc.add_options()("config", po::value<std::string>(),
                       "Full path to a config file");
    desc.add_options()("into-idle",
                       "Experimental: boot into Idle");
    desc.add_options()(
        "lifecycle-api", po::value<std::string>()->implicit_value("ro"), "Experimental");
"""

CONFIG_SCHEMA = """
$defs:
  config_map:
    type: object
type: object
properties:
  active_modules:
    patternProperties:
      ^[a-z]+$:
        properties:
          config_module:
            $ref: '#/$defs/config_map'
"""


def test_manager_options_are_read_from_the_declarations(tmp_path):
    assert snapshot.collect_manager(tmp_path) is None
    path = tmp_path / snapshot.MANAGER_SOURCE
    path.parent.mkdir(parents=True)
    path.write_text(MANAGER_SOURCE)
    assert snapshot.collect_manager(tmp_path) == {
        "options": {
            "help": {"alias": "h", "takes_value": False},
            "config": {"alias": None, "takes_value": True},
            "into-idle": {"alias": None, "takes_value": False},
            "lifecycle-api": {"alias": None, "takes_value": True},
        },
        "experimental_options": ["into-idle", "lifecycle-api"],
    }


def test_framework_schemas_have_their_references_inlined(tmp_path):
    assert snapshot.collect_framework(tmp_path) == {}
    path = tmp_path / snapshot.FRAMEWORK_SCHEMAS["config"]
    path.parent.mkdir(parents=True)
    path.write_text(CONFIG_SCHEMA)
    config = snapshot.collect_framework(tmp_path)["config"]
    assert "$defs" not in config
    module = config["properties"]["active_modules"]["patternProperties"]["^[a-z]+$"]
    assert module["properties"]["config_module"] == {"type": "object"}


def test_every_path_the_snapshot_reads_exists_in_this_tree():
    """A contract file that moves must be followed here, or its part of the contract goes unread."""
    paths = [
        snapshot.ASYNCAPI_DIR, snapshot.MODULES_DIR, snapshot.OCPP_CONFIG_DIR, snapshot.OCPP16_KEY_MAPPINGS,
        snapshot.RPC_API_TYPES, snapshot.MANAGER_SOURCE, snapshot.PROJECT_FILE, *snapshot.FRAMEWORK_SCHEMAS.values(),
        *snapshot.registry.REGISTRY_FILES.values(),
    ]
    assert [path for path in paths if not (REPO_ROOT / path).exists()] == []


def test_every_part_of_the_contract_is_found_in_this_tree():
    tree = snapshot.snapshot_tree(REPO_ROOT)
    assert tree["asyncapi"] and tree["modules"] and tree["rpc_api"]["types"] and tree["version"]
    assert tree["ocpp"]["v16_profile_schemas"] and tree["ocpp"]["component_config"]
    assert tree["ocpp"]["v16_custom_key_mappings"]
    assert set(tree["framework"]) == set(snapshot.FRAMEWORK_SCHEMAS)
    assert tree["manager"]["options"] and tree["manager"]["experimental_options"]
    stores = {name.rsplit("/", 1)[0] for name in tree["migrations"]}
    assert stores >= set(snapshot.MIGRATION_FAMILIES.values())


def test_documentation_of_a_config_option_is_not_part_of_the_contract():
    option = {"type": "string", "default": "a", "description": "text", "deprecated": {"deprecated_in": "2026.10.0"}}
    assert snapshot._config_set({"option": option}) == {"option": {"type": "string", "default": "a", "required": False}}
