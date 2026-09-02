# SPDX-License-Identifier: Apache-2.0
# Copyright Pionix GmbH and Contributors to EVerest

"""Broker-free tests: argument parsing, request payloads, and shell robustness."""

import json
import types

import pytest
import yaml

from everest_management_api_cli import commands, configuration_api, lifecycle_api
from everest_management_api_cli.commands import (
    CommandError,
    build_parser,
    format_result,
    parse_raw_payload,
    prepare_yaml_for_slot,
    run_command,
)
from everest_management_api_cli.shell import ManagementApiShell

LIFECYCLE = "everest_api/1/lifecycle/m2e/"
CONFIGURATION = "everest_api/1/configuration/m2e/"
REPLY = {"ok": True}


class FakeSession:
    """Looks like commands.Session, but the clients never touch an MQTT connection."""

    def __init__(self, timeout_s: float = 3.0):
        self.host = "test-broker"
        self.port = 1883
        self.timeout_s = timeout_s
        self.mqtt = object()
        self.on_notice = None
        self.lifecycle = lifecycle_api.LifecycleApiClient(None, self._dispatch)
        self.configuration = configuration_api.ConfigurationApiClient(None, self._dispatch)

    def _dispatch(self, topic, payload):
        if self.on_notice is not None:
            self.on_notice(topic, payload)


@pytest.fixture
def rpc_calls(monkeypatch):
    """Replace perform_rpc everywhere; records (topic, payload, timeout_s) and returns REPLY."""
    calls = []

    def fake_perform_rpc(_client, topic, payload, timeout_s=10.0):
        calls.append((topic, payload, timeout_s))
        return dict(REPLY)

    for module in (commands, configuration_api, lifecycle_api):
        monkeypatch.setattr(module, "perform_rpc", fake_perform_rpc)
    return calls


def run(argv, session=None):
    parser, _ = build_parser(prog="test", include_connection_options=False)
    return run_command(session or FakeSession(), parser.parse_args(argv))


def status_message(payload: dict):
    return types.SimpleNamespace(payload=json.dumps(payload).encode())


@pytest.mark.parametrize("argv, topic, payload", [
    (["version"], LIFECYCLE + "get_everest_version", {}),
    (["start"], LIFECYCLE + "start_modules", {}),
    (["stop"], LIFECYCLE + "stop_modules", {}),
    (["slots"], CONFIGURATION + "list_all_slots", {}),
    (["active"], CONFIGURATION + "get_active_slot", {}),
    (["mark", "3"], CONFIGURATION + "mark_active_slot", {"slot_id": 3}),
    (["delete", "4"], CONFIGURATION + "delete_slot", {"slot_id": 4}),
    (["dup", "1"], CONFIGURATION + "duplicate_slot", {"slot_id": 1}),
    (["dup", "1", "-d", "copy of one"], CONFIGURATION + "duplicate_slot",
     {"slot_id": 1, "new_description": "copy of one"}),
    (["desc", "2", "hello", "world"], CONFIGURATION + "set_description",
     {"slot_id": 2, "description": "hello world"}),
    (["config", "1"], CONFIGURATION + "get_configuration", {"slot_id": 1, "force_read_from_db": False}),
    (["config", "1", "--db"], CONFIGURATION + "get_configuration", {"slot_id": 1, "force_read_from_db": True}),
    (["getp", "1", "evse_manager", "max_current"], CONFIGURATION + "get_config_parameters",
     {"slot_id": 1, "parameters": [{"module_id": "evse_manager", "parameter_name": "max_current"}],
      "force_read_from_db": False}),
    (["getp", "1", "m", "p", "--impl", "main", "--db"], CONFIGURATION + "get_config_parameters",
     {"slot_id": 1, "parameters": [{"module_id": "m", "parameter_name": "p", "implementation_id": "main"}],
      "force_read_from_db": True}),
    (["setp", "1", "m", "p", "42"], CONFIGURATION + "set_config_parameters",
     {"slot_id": 1, "parameter_updates": [{"cfg_param_id": {"module_id": "m", "parameter_name": "p"},
                                           "value": "42"}]}),
    (["setp", "1", "m", "p", "42", "--impl", "main"], CONFIGURATION + "set_config_parameters",
     {"slot_id": 1, "parameter_updates": [{"cfg_param_id": {"module_id": "m", "parameter_name": "p",
                                                            "implementation_id": "main"},
                                           "value": "42"}]}),
    (["raw", "lifecycle", "get_everest_version"], LIFECYCLE + "get_everest_version", {}),
    (["raw", "configuration", "mark_active_slot", '{"slot_id": 999}'], CONFIGURATION + "mark_active_slot",
     {"slot_id": 999}),
])
def test_command_sends_expected_request(rpc_calls, argv, topic, payload):
    assert run(argv) == REPLY
    assert rpc_calls == [(topic, payload, 3.0)]


def test_load_reduces_full_config_to_active_modules(rpc_calls, tmp_path):
    config = tmp_path / "config.yaml"
    config.write_text("settings:\n  prefix: /x\nactive_modules:\n  m:\n    module: Mod\n")
    run(["load", str(config), "-d", "from file", "--slot", "7"])
    topic, payload, _ = rpc_calls[0]
    assert topic == CONFIGURATION + "load_from_yaml"
    assert yaml.safe_load(payload["raw_yaml"]) == {"active_modules": {"m": {"module": "Mod"}}}
    assert payload["description"] == "from file"
    assert payload["slot_id"] == 7


def test_load_as_is_sends_file_unchanged(rpc_calls, tmp_path):
    config = tmp_path / "config.yaml"
    config.write_text("not: [valid")
    run(["load", str(config), "--as-is"])
    assert rpc_calls[0][1] == {"raw_yaml": "not: [valid"}


def test_load_missing_file_is_reported(rpc_calls, tmp_path):
    with pytest.raises(CommandError, match="cannot read"):
        run(["load", str(tmp_path / "missing.yaml")])
    assert rpc_calls == []


def test_prepare_yaml_passes_through_without_active_modules():
    assert prepare_yaml_for_slot("foo: 1\n") == "foo: 1\n"


def test_prepare_yaml_rejects_invalid_yaml():
    with pytest.raises(CommandError, match="not valid YAML"):
        prepare_yaml_for_slot("not: [valid")


def test_raw_payload_defaults_to_empty_object_and_rejects_bad_json():
    assert parse_raw_payload([]) == {}
    assert parse_raw_payload(['{"a":', "1}"]) == {"a": 1}
    with pytest.raises(CommandError, match="not valid JSON"):
        parse_raw_payload(["{not json"])


def test_slot_id_must_be_an_integer(capsys):
    parser, _ = build_parser(prog="test", include_connection_options=False)
    with pytest.raises(SystemExit):
        parser.parse_args(["mark", "abc"])
    assert "slot id must be an integer" in capsys.readouterr().err


def test_status_without_manager_is_reported():
    with pytest.raises(CommandError, match="no lifecycle status received"):
        run(["status", "--wait", "0.1"])


def test_status_returns_latest_or_all_updates():
    session = FakeSession()
    session.lifecycle._on_status(None, None, status_message({"everest_running": False}))
    session.lifecycle._on_status(None, None, status_message({"everest_running": True}))
    assert run(["status"], session) == {"everest_running": True}
    assert run(["status", "--all"], session) == [{"everest_running": False}, {"everest_running": True}]


def test_format_result():
    assert format_result("plain text") == "plain text"
    assert format_result({"a": [1, 2]}, compact=True) == '{"a":[1,2]}'
    assert json.loads(format_result({"a": [1, 2]})) == {"a": [1, 2]}


# --- shell ---------------------------------------------------------------------------------


def test_shell_dispatches_line_to_command(rpc_calls, capsys):
    ManagementApiShell(FakeSession()).onecmd("mark 5")
    assert rpc_calls[0][1] == {"slot_id": 5}
    assert json.loads(capsys.readouterr().out) == REPLY


def test_shell_quotes_json_payload_as_one_argument(rpc_calls):
    ManagementApiShell(FakeSession()).onecmd("raw configuration mark_active_slot '{\"slot_id\": 999}'")
    assert rpc_calls[0] == (CONFIGURATION + "mark_active_slot", {"slot_id": 999}, 3.0)


def test_shell_survives_bad_input(rpc_calls, capsys):
    shell = ManagementApiShell(FakeSession())
    shell.onecmd("mark notanumber")
    shell.onecmd("no-such-command")
    shell.onecmd("raw configuration x '{unbalanced")
    shell.onecmd("mark")
    shell.onecmd("slots --help")
    assert rpc_calls == []
    out = capsys.readouterr()
    assert "slot id must be an integer" in out.err
    assert "invalid choice" in out.out
    assert "No closing quotation" in out.out
    assert "list_all_slots" in out.out


def test_shell_reports_rpc_timeout(monkeypatch, capsys):
    def timing_out(*_args, **_kwargs):
        raise TimeoutError("no reply")

    monkeypatch.setattr(configuration_api, "perform_rpc", timing_out)
    ManagementApiShell(FakeSession()).onecmd("slots")
    assert "error: TimeoutError: no reply" in capsys.readouterr().out


def test_shell_timeout_command(capsys):
    session = FakeSession()
    shell = ManagementApiShell(session)
    shell.onecmd("timeout 2.5")
    assert session.timeout_s == 2.5
    shell.onecmd("timeout")
    assert capsys.readouterr().out.count("reply timeout: 2.5s") == 2


def test_shell_prints_live_notices(capsys):
    session = FakeSession()
    ManagementApiShell(session)
    session.lifecycle._on_status(None, None, status_message({"everest_running": True}))
    assert '[status] {"everest_running": true}' in capsys.readouterr().out


def test_shell_help_lists_api_and_shell_commands(capsys):
    shell = ManagementApiShell(FakeSession())
    shell.onecmd("help")
    shell.onecmd("help setp")
    out = capsys.readouterr().out
    assert "usage: COMMAND [ARGS ...]" in out
    assert "clear-retained-status" in out
    assert "timeout [SECONDS]" in out
    assert "usage: setp" in out


def test_shell_exit_commands():
    shell = ManagementApiShell(FakeSession())
    assert shell.onecmd("quit") is True
    assert shell.onecmd("exit") is True
    assert shell.onecmd("EOF") is True


# --- parameter files -----------------------------------------------------------------------

PARAMETER_FILE = """\
active_modules:
  example:
    config_module:
      log_interval: 3
      enum_test: "two"
    config_implementation:
      example:
        current: 40.5
        enabled: true
"""

EXPECTED_IDS = [
    {"module_id": "example", "parameter_name": "log_interval"},
    {"module_id": "example", "parameter_name": "enum_test"},
    {"module_id": "example", "parameter_name": "current", "implementation_id": "example"},
    {"module_id": "example", "parameter_name": "enabled", "implementation_id": "example"},
]


def test_setp_from_file_sends_every_parameter_with_json_scalars(rpc_calls, tmp_path):
    from everest_management_api_cli.commands import parse_parameter_file
    assert parse_parameter_file(PARAMETER_FILE) == [
        ("example", "log_interval", None, "3"),
        ("example", "enum_test", None, "two"),
        ("example", "current", "example", "40.5"),
        ("example", "enabled", "example", "true"),
    ]
    path = tmp_path / "update.yaml"
    path.write_text(PARAMETER_FILE)
    run(["setp", "1", "--file", str(path)])
    topic, payload, _ = rpc_calls[0]
    assert topic == CONFIGURATION + "set_config_parameters"
    assert [u["cfg_param_id"] for u in payload["parameter_updates"]] == EXPECTED_IDS
    assert [u["value"] for u in payload["parameter_updates"]] == ["3", "two", "40.5", "true"]


def test_getp_from_file_queries_every_parameter(rpc_calls, tmp_path):
    path = tmp_path / "query.yaml"
    path.write_text(PARAMETER_FILE)
    run(["getp", "1", "-f", str(path), "--db"])
    topic, payload, _ = rpc_calls[0]
    assert topic == CONFIGURATION + "get_config_parameters"
    assert payload == {"slot_id": 1, "parameters": EXPECTED_IDS, "force_read_from_db": True}


@pytest.mark.parametrize("argv, message", [
    (["getp", "1"], "module_id and parameter are required"),
    (["setp", "1", "m", "p"], "module_id and parameter and value are required"),
    (["getp", "1", "m", "p", "--file", "x.yaml"], "not both"),
])
def test_parameter_selection_errors(rpc_calls, argv, message):
    with pytest.raises(CommandError, match=message):
        run(argv)
    assert rpc_calls == []


@pytest.mark.parametrize("text, message", [
    ("not: [valid", "not valid YAML"),
    ("foo: 1", "needs an 'active_modules' mapping"),
    ("active_modules: {}", "contains no parameters"),
    ("active_modules:\n  m:\n    config_module:\n      p: [1, 2]\n", "must be scalars"),
])
def test_parameter_file_errors(text, message):
    from everest_management_api_cli.commands import parse_parameter_file
    with pytest.raises(CommandError, match=message):
        parse_parameter_file(text)
