# SPDX-License-Identifier: Apache-2.0
# Copyright Pionix GmbH and Contributors to EVerest
"""Tests for the EVerest API client built from the AsyncAPI specs of this tree.

No MQTT broker is needed: the paho client is replaced by a fake that records
subscriptions and publications and lets the tests inject messages.
"""

import io
import json
import re
import readline
import sys
import threading
import time
from pathlib import Path
from types import SimpleNamespace
from unittest import mock

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from everest_api import commands, spy  # noqa: E402
from everest_api.model import api_name_from_file  # noqa: E402
from everest_api.mqtt_client import ApiClient  # noqa: E402
from everest_api.shell import EVerestAPICmd  # noqa: E402
from everest_api.specs import SPEC_GLOB, find_spec_dir, load_models  # noqa: E402

SPEC_DIR = find_spec_dir()
PLACEHOLDER = "everest_api/1/{interface_type}/{module_id}/e2m/{operation_name}/{uuid}"


class FakeMqtt:

    def __init__(self):
        self.subscribed = []
        self.published = []
        self.on_message = None
        self.connected = True

    def subscribe(self, topic):
        self.subscribed.append(topic)

    def publish(self, topic, data):
        self.published.append((topic, data))

    def deliver(self, topic, payload, raw=None):
        data = raw if raw is not None else json.dumps(payload).encode()
        self.on_message(None, None, SimpleNamespace(topic=topic, payload=data))

    def disconnect(self):
        self.connected = False

    def loop_stop(self):
        pass


@pytest.fixture(scope="module")
def models():
    return {model.api: model for model in load_models(SPEC_DIR, warn=pytest.fail)}


@pytest.fixture
def app(models, monkeypatch, tmp_path):
    monkeypatch.setenv("HOME", str(tmp_path))
    fakes = []

    def connect(warn):
        fakes.append(FakeMqtt())
        return fakes[-1]

    monkeypatch.setattr(commands, "connect_mqtt", connect)
    monkeypatch.setattr(spy, "connect_mqtt", connect)
    shell = EVerestAPICmd(list(models.values()))
    shell.colors = False
    shell.fakes = fakes
    yield shell
    shell.stop_requested = True


def run(app, line):
    return app.onecmd_plus_hooks(line)


def output(app, *lines):
    with mock.patch.object(app, "stdout", io.StringIO()) as stdout:
        for line in lines:
            run(app, line)
    return stdout.getvalue()


def client(app, client_id):
    return next(entry["instance"] for entry in app.api_clients if entry["id"] == client_id)


def complete(app, line):
    text = line.split(" ")[-1]
    with mock.patch.object(readline, "get_line_buffer", return_value=line), \
            mock.patch.object(readline, "get_begidx", return_value=len(line) - len(text)), \
            mock.patch.object(readline, "get_endidx", return_value=len(line)):
        app.complete(text, 0)
    # a single match gets a trailing space appended by cmd2
    return [match.rstrip(" ") for match in app.completion_matches]


# ---- specs and model ----

def test_every_spec_is_loaded(models):
    files = sorted(api_name_from_file(path.stem) for path in SPEC_DIR.glob(SPEC_GLOB))
    assert files and sorted(models) == files


@pytest.mark.parametrize("content", [
    "a: [",                                                       # invalid YAML
    "servers: [1]\n",                                             # servers not a mapping
    "servers: {default: {pathname: x}}\noperations: {op: }\n",    # null operation
    "servers: {default: {pathname: x}}\noperations: [1]\n",       # operations not a mapping
])
def test_a_broken_spec_is_skipped(tmp_path, content):
    (tmp_path / "broken_API.yaml").write_text(content)
    (tmp_path / "powermeter_API.yaml").write_text((SPEC_DIR / "powermeter_API.yaml").read_text())
    warnings = []
    loaded = load_models(tmp_path, warn=warnings.append)
    assert [model.api for model in loaded] == ["powermeter"]
    assert len(warnings) == 1 and "broken_API.yaml" in warnings[0]


def test_topics(models):
    fake = FakeMqtt()
    pm = ApiClient(fake, models["powermeter"], "pm")
    pm.start()
    assert "everest_api/1/powermeter/pm/e2m/start_transaction" in fake.subscribed
    assert pm.send.send_powermeter_values._topic == "everest_api/1/powermeter/pm/m2e/powermeter_values"
    assert pm.send.send_reply_start_transaction._topic is None

    fake = FakeMqtt()
    ApiClient(fake, models["lifecycle"], "lifecycle").start()
    assert all(topic.startswith("everest_api/1/lifecycle/") and "//" not in topic for topic in fake.subscribed)
    assert "everest_api/1/lifecycle/e2m/get_everest_version/+" in fake.subscribed

    entrypoint = models["entrypoint"]
    op = next(op for op in entrypoint.send_ops if op.params)
    assert op.params == ["api_type"]
    fake = FakeMqtt()
    ep = ApiClient(fake, entrypoint, "entrypoint")
    ep.start()
    assert ep.send_topic(op, {"api_type": "powermeter"}).endswith("/powermeter")
    assert any(topic.endswith("/+") for topic in fake.subscribed)


def test_nothing_is_subscribed_before_start(models):
    fake = FakeMqtt()
    api_client = ApiClient(fake, models["powermeter"], "pm")
    assert fake.subscribed == []
    api_client.start()
    assert fake.subscribed


def test_routing_is_exact(models):
    fake = FakeMqtt()
    api_client = ApiClient(fake, models["isolation_monitor"], "imd")
    api_client.start()
    got = []
    for op_id, receiver in vars(api_client.receive).items():
        receiver.register_callback(lambda topic, data, o=op_id: got.append(o))
    fake.deliver("everest_api/1/isolation_monitor/imd/e2m/start_self_test", {})
    assert got == ["receive_start_self_test"]


# ---- client_add / client_remove ----

def test_client_add_creates_the_commands_of_every_api(app, models):
    assert sorted(complete(app, "client_add ")) == sorted(list(models) + ["spy"])
    for api in models:
        run(app, f"client_add {api} id_{api}")
    names = set(app.get_all_commands())
    for api, model in models.items():
        for op in model.send_ops + model.recv_ops:
            assert f"id_{api}.{op.op_id}" in names


def test_commands_belong_to_one_client(app):
    run(app, "client_add powermeter meter1")
    run(app, "client_add powermeter meter2")
    assert complete(app, "meter1.send_power") == ["meter1.send_powermeter_values"]
    run(app, 'meter2.send_powermeter_values {"a":1}')
    assert app.fakes[1].published == [("everest_api/1/powermeter/meter2/m2e/powermeter_values", '{"a": 1}')]
    assert app.fakes[0].published == []
    run(app, "client_remove meter1")
    assert "meter1.send_powermeter_values" not in app.get_all_commands()
    assert "meter2.send_powermeter_values" in app.get_all_commands()
    run(app, "client_add powermeter meter1")
    assert "meter1.send_powermeter_values" in app.get_all_commands()


@pytest.mark.parametrize("bad_id", ["'p|m'", "pm#", "pm+", "a/b", "'p m'"])
def test_unusable_client_ids_are_rejected_before_connecting(app, capsys, bad_id):
    run(app, f"client_add powermeter {bad_id}")
    assert "cannot be used as a client id" in capsys.readouterr().err
    assert app.fakes == []


def test_duplicate_client_ids_are_rejected(app, capsys):
    run(app, "client_add powermeter pm")
    run(app, "client_add slac pm")
    run(app, "client_add spy pm")
    assert capsys.readouterr().err.count("exists already") == 2
    assert len(app.fakes) == 1


def test_failed_client_add_leaves_nothing_behind(app, monkeypatch):
    monkeypatch.setattr(ApiClient, "start", mock.Mock(side_effect=ValueError("subscribe failed")))
    run(app, "client_add powermeter pm")
    assert not app.fakes[0].connected
    assert app.mqtt_clients == [] and app.api_clients == []
    assert not any(name.startswith("pm.") for name in app.get_all_commands())


def test_fixed_path_apis_default_to_the_api_name(app):
    run(app, "client_add lifecycle")
    assert "lifecycle.send_request_get_everest_version" in app.get_all_commands()


def test_client_remove(app):
    run(app, "client_add powermeter pm")
    run(app, "client_remove pm")
    assert app.api_clients == []
    assert not app.fakes[-1].connected
    assert not any(name.startswith("pm.") for name in app.get_all_commands())


def test_client_remove_unknown_id(app, capsys):
    run(app, "client_remove nope")
    assert "No client with id 'nope'" in capsys.readouterr().err


# ---- send ----

def test_send_with_generated_reply_to_and_receive_the_reply(app):
    run(app, "client_add lifecycle")
    fake = app.fakes[-1]
    run(app, "lifecycle.send_request_get_everest_version "
        + json.dumps({"headers": {"replyTo": PLACEHOLDER}}, separators=(",", ":")))
    topic, data = fake.published[-1]
    assert topic == "everest_api/1/lifecycle/m2e/get_everest_version"
    reply_to = json.loads(data)["headers"]["replyTo"]
    assert reply_to.startswith("everest_api/1/lifecycle/e2m/get_everest_version/")

    fake.deliver(reply_to, {"version": "1"})
    receiver = client(app, "lifecycle").receive.receive_reply_get_everest_version
    assert receiver.wait_for_message(timeout=0) == (reply_to, b'{"version": "1"}')


def test_quoted_payload(app):
    run(app, "client_add powermeter pm")
    run(app, """pm.send_powermeter_values '{"a":1}'""")
    assert app.fakes[-1].published[-1][1] == '{"a": 1}'


def test_invalid_payload(app, capsys):
    run(app, "client_add powermeter pm")
    run(app, "pm.send_powermeter_values {nope")
    assert "invalid JSON payload" in capsys.readouterr().err
    assert app.fakes[-1].published == []


def test_reply_to_cache_completion_and_last(app):
    run(app, "client_add powermeter pm")
    fake = app.fakes[-1]
    reply_to = "everest_api/1/powermeter/pm/e2m/stop_transaction/1234"
    fake.deliver("everest_api/1/powermeter/pm/e2m/stop_transaction", {"headers": {"replyTo": reply_to}})
    assert app.reply_to_cache == [reply_to]

    assert complete(app, "pm.send_reply_stop_transaction ") == [reply_to]

    run(app, 'pm.send_reply_stop_transaction @last {"status":"OK"}')
    assert fake.published[-1] == (reply_to, '{"status": "OK"}')


def test_own_requests_seen_by_a_spy_are_not_cached(app):
    run(app, "client_add powermeter pm")
    run(app, "client_add spy")
    pm, spy_fake = app.fakes
    spy_fake.deliver("everest_api/1/powermeter/pm/m2e/communication_check", {"headers": {"replyTo": "own/1"}})
    assert app.reply_to_cache == []
    pm.deliver("everest_api/1/powermeter/pm/e2m/stop_transaction", {"headers": {"replyTo": "everest/1"}})
    assert app.reply_to_cache == ["everest/1"]


def test_an_unresolved_last_is_not_sent(app, capsys):
    run(app, "client_add powermeter pm")
    run(app, 'pm.send_reply_stop_transaction @last {"status":"OK"}')
    run(app, 'pm.send_reply_stop_transaction @3 {"status":"OK"}')
    assert app.fakes[-1].published == []
    err = capsys.readouterr().err
    assert "@last: no such replyTo" in err and "@3: no such replyTo" in err
    assert app.exit_code == 1


def test_an_unresolved_last_stops_the_script(app, tmp_path):
    run(app, "client_add powermeter pm")
    script = tmp_path / "s.script"
    script.write_text('pm.send_reply_stop_transaction @last {"status":"OK"}\necho NOT-REACHED\n')
    out = output(app, f"run_script {script}", "echo AFTER")
    assert "NOT-REACHED" not in out and "AFTER" in out
    assert app.fakes[-1].published == []
    assert app.exit_code == 1


def test_last_is_only_substituted_as_a_whole_word(app):
    run(app, "client_add powermeter pm")
    fake = app.fakes[-1]
    fake.deliver("everest_api/1/powermeter/pm/e2m/stop_transaction", {"headers": {"replyTo": "r/1"}})
    run(app, 'pm.send_powermeter_values {"meter_id":"a@last.com"}')
    assert fake.published[-1][1] == '{"meter_id": "a@last.com"}'


def test_request_payload_completion_inserts_the_placeholder(app):
    run(app, "client_add lifecycle")
    assert complete(app, "lifecycle.send_request_get_everest_version ") == [
        json.dumps({"headers": {"replyTo": PLACEHOLDER}}, separators=(",", ":"))]
    run(app, "set replyToPlaceholder MY_PLACEHOLDER")
    assert complete(app, "lifecycle.send_request_get_everest_version ") == ['{"headers":{"replyTo":"MY_PLACEHOLDER"}}']


def test_every_request_knows_its_reply(models):
    for model in models.values():
        replies = {op.op_id for op in model.recv_ops if op.is_reply}
        assert {op.reply_op_id for op in model.send_ops if op.reply_op_id} == replies


def test_help_explains_reply_to(app):
    run(app, "client_add lifecycle")
    run(app, "client_add powermeter pm")
    out = output(app, "help lifecycle.send_request_get_everest_version", "help pm.send_reply_stop_transaction",
                 "help replyto")
    assert "lifecycle.receive_reply_get_everest_version" in out
    assert "@last" in out and "replyToPlaceholder" in out


def test_payload_completion(app, models):
    run(app, "client_add evse_board_support bsp")
    op = next(op for op in models["evse_board_support"].send_ops if op.op_id == "send_event")
    assert op.payload_json
    assert complete(app, "bsp.send_event ") == [op.payload_json]


# ---- receive ----

def test_receive_returns_queued_messages_in_order(app):
    run(app, "client_add powermeter pm")
    fake = app.fakes[-1]
    topic = "everest_api/1/powermeter/pm/e2m/start_transaction"
    fake.deliver(topic, {"n": 1})
    fake.deliver(topic, {"n": 2})
    receiver = client(app, "pm").receive.receive_request_start_transaction
    assert app.exit_code == 0
    run(app, "pm.receive_request_start_transaction --timeout 1")
    assert receiver.wait_for_message(timeout=0) == (topic, b'{"n": 2}')
    assert receiver.wait_for_message(timeout=0) is None


def test_receive_next_ignores_queued_messages(app):
    run(app, "client_add powermeter pm")
    fake = app.fakes[-1]
    topic = "everest_api/1/powermeter/pm/e2m/start_transaction"
    fake.deliver(topic, {"n": 1})
    threading.Timer(0.1, fake.deliver, (topic, {"n": 2})).start()
    run(app, "pm.receive_request_start_transaction --next --timeout 5")
    assert app.exit_code == 0
    receiver = client(app, "pm").receive.receive_request_start_transaction
    assert receiver.wait_for_message(timeout=0) is None


def test_receive_timeout(app, capsys):
    run(app, "client_add powermeter pm")
    run(app, "pm.receive_heartbeat --timeout 0.1 --optional")
    assert app.exit_code == 0
    run(app, "pm.receive_heartbeat --timeout 0.1")
    assert "timed out" in capsys.readouterr().err
    assert app.exit_code == 1


def test_communication_check(app):
    run(app, "client_add powermeter pm")
    run(app, "pm.send_communication_check true")
    assert app.fakes[-1].published[-1] == ("everest_api/1/powermeter/pm/m2e/communication_check", "true")


def test_a_bad_message_does_not_stop_the_client(app, capsys):
    run(app, "client_add powermeter pm")
    run(app, "client_add spy")
    pm, spy_fake = app.fakes
    topic = "everest_api/1/powermeter/pm/e2m/start_transaction"
    pm.deliver(topic, None, raw=b"\xff\xfe")
    spy_fake.deliver(topic, None, raw=b"\xff\xfe")
    with mock.patch.object(app, "log_event", side_effect=[RuntimeError("boom"), None]):
        pm.deliver(topic, {"n": 1})  # the callback fails: reported, not raised
    receiver = client(app, "pm").receive.receive_request_start_transaction
    assert len([receiver.wait_for_message(timeout=0) for _ in range(2)]) == 2


# ---- shell ----

def test_spy_and_wait_for(app):
    run(app, "client_add spy")
    fake = app.fakes[-1]
    assert fake.subscribed == ["everest_api/#"]
    topic = "everest_api/1/evse_manager_consumer/em/e2m/session_event"
    timer = threading.Timer(0.1, fake.deliver, (topic, {"event": "Authorized"}))
    timer.start()
    assert not run(app, "wait_for session_event Authorized --timeout 5")
    timer.join()
    assert app.exit_code == 0


def test_wait_for_timeout_fails_but_does_not_quit(app, capsys):
    assert not run(app, "wait_for session_event Never --timeout 0.1 --optional")
    assert app.exit_code == 0
    assert not run(app, "wait_for session_event Never --timeout 0.1")
    assert "timed out" in capsys.readouterr().err
    assert app.exit_code == 1


def test_a_timeout_stops_the_script_only(app, tmp_path):
    script = tmp_path / "s.script"
    script.write_text("wait_for nothing --timeout 0.1\necho NOT-REACHED\n")
    out = output(app, f"run_script {script}", "echo AFTER")
    assert "NOT-REACHED" not in out and "AFTER" in out
    assert app.exit_code == 1


def test_spy_with_topic_and_remove(app):
    run(app, "client_add spy")
    run(app, "client_add spy everest_api/1/powermeter")
    default_spy, topic_spy = app.fakes
    assert topic_spy.subscribed == ["everest_api/1/powermeter/#"]
    assert complete(app, "client_remove ") == ["everest_api/1/powermeter", "spy"]
    run(app, "client_remove spy")
    assert not default_spy.connected and topic_spy.connected
    run(app, "client_remove everest_api/1/powermeter")
    assert not topic_spy.connected and not app.mqtt_clients


def test_spy_rejects_wildcard_topics(app, capsys):
    run(app, "client_add spy everest_api/#")
    assert "cannot be spied on" in capsys.readouterr().err
    assert app.fakes == [] and app.api_clients == []


@pytest.mark.parametrize("show,hide,topic,shown", [
    ("powermeter", "", "everest_api/1/slac/s/e2m/x", False),
    ("powermeter", "", "everest_api/1/powermeter/pm/e2m/x", True),
    ("powermeter", "bsp", "everest_api/1/slac/s/e2m/x", False),
    ("powermeter", "start", "everest_api/1/powermeter/pm/e2m/start_transaction", False),
    ("", "bsp", "everest_api/1/bsp/b/e2m/x", False),
    ("", "bsp", "everest_api/1/slac/s/e2m/x", True),
])
def test_show_and_hide_topic(app, show, hide, topic, shown):
    app.show_topic, app.hide_topic = show, hide
    with mock.patch.object(app, "stdout", io.StringIO()) as stdout:
        app.log_event(topic, "{}")
    assert bool(stdout.getvalue()) == shown


def test_quiet_is_settable(app):
    with mock.patch.object(app, "stdout", io.StringIO()) as stdout:
        app.log_event("everest_api/1/powermeter/pm/e2m/heartbeat", "1")
        assert stdout.getvalue() == ""
        run(app, "set quiet false")
        app.log_event("everest_api/1/powermeter/pm/e2m/heartbeat", "1")
    assert "heartbeat" in stdout.getvalue()


def test_errors_are_shown_while_muted(app):
    app.mute = True
    with mock.patch.object(app, "stdout", io.StringIO()) as stdout:
        app.log_event("ERROR:", "boom")
    assert "boom" in stdout.getvalue()


def test_apis_lists_operations(app, models):
    out = output(app, "apis powermeter")
    assert all(op.op_id in out for op in models["powermeter"].send_ops + models["powermeter"].recv_ops)
    assert complete(app, "apis power") == sorted(["power_supply_dc", "powermeter"])


def test_apis_with_an_api_without_operations(tmp_path, monkeypatch):
    monkeypatch.setenv("HOME", str(tmp_path))
    (tmp_path / "empty_API.yaml").write_text("servers: {default: {pathname: 'everest_api/1/empty/{module_id}'}}\n"
                                             "operations: {}\n")
    shell = EVerestAPICmd(load_models(tmp_path, warn=pytest.fail))
    assert output(shell, "apis empty") == ""
    shell.stop_requested = True


def test_messages_from_mqtt_threads_are_printed_while_a_command_runs(app):
    with mock.patch.object(app, "stdout", io.StringIO()) as stdout:
        with app.terminal_lock:  # held by cmd2 while a command runs
            app.output_queue.put("In: some/topic, {}")
            app.output_queue.join()
    assert "In: some/topic, {}" in stdout.getvalue()


def test_receive_prints_and_sets_last_to_the_received_request(app):
    run(app, "client_add powermeter pm")
    fake = app.fakes[-1]
    topic = "everest_api/1/powermeter/pm/e2m/stop_transaction"
    fake.deliver(topic, {"headers": {"replyTo": "reply/1"}})
    fake.deliver(topic, {"headers": {"replyTo": "reply/2"}})
    assert app.reply_to_cache[0] == "reply/2"
    assert "received: " + topic in output(app, "pm.receive_request_stop_transaction --timeout 1")
    run(app, 'pm.send_reply_stop_transaction @last {"status":"OK"}')
    assert fake.published[-1] == ("reply/1", '{"status": "OK"}')


def test_unquoted_timestamps_in_examples(tmp_path):
    spec = (SPEC_DIR / "powermeter_API.yaml").read_text()
    quoted = '"2019-08-24T14:15:22Z"'
    assert quoted in spec or "'2019-08-24T14:15:22Z'" in spec
    spec = spec.replace(quoted, "2019-08-24T14:15:22Z").replace("'2019-08-24T14:15:22Z'", "2019-08-24T14:15:22Z")
    (tmp_path / "powermeter_API.yaml").write_text(spec)
    assert [model.api for model in load_models(tmp_path, warn=pytest.fail)] == ["powermeter"]


def test_a_timeout_in_a_nested_script_stops_the_outer_script(app, tmp_path):
    inner = tmp_path / "inner.script"
    inner.write_text("wait_for nothing --timeout 0.1\necho INNER-NOT-REACHED\n")
    outer = tmp_path / "outer.script"
    outer.write_text(f"run_script {inner}\necho OUTER-NOT-REACHED\n")
    out = output(app, f"run_script {outer}", "echo AFTER")
    assert "NOT-REACHED" not in out and "AFTER" in out
    assert app.exit_code == 1
    assert "AFTER" in output(app, f"run_script {tmp_path / 'missing.script'}", "echo AFTER")


def test_quotes_are_stripped_from_reply_to_and_topic_parameters(app):
    run(app, "client_add powermeter pm")
    run(app, "client_add entrypoint")
    run(app, 'pm.send_reply_stop_transaction "reply/9" {}')
    assert app.fakes[0].published[-1] == ("reply/9", "{}")
    run(app, 'entrypoint.send_request_query_module "powermeter" {}')
    assert app.fakes[1].published[-1][0] == "everest_api/query-modules/powermeter"


def test_wait_for_sees_what_arrived_during_the_previous_command(app):
    run(app, "client_add spy")
    fake = app.fakes[-1]
    fake.deliver("everest_api/x/old_event", {})
    run(app, "echo previous command")
    fake.deliver("everest_api/x/session_event", {"event": "SessionFinished"})
    assert not run(app, "wait_for session_event SessionFinished --timeout 0.1")
    assert app.exit_code == 0
    run(app, "wait_for old_event --timeout 0.1 --optional")
    assert app.exit_code == 0
    assert not run(app, "wait_for old_event --timeout 0.1")
    assert app.exit_code == 1


def test_the_placeholder_is_only_replaced_in_requests(app):
    run(app, "client_add lifecycle")
    run(app, "client_add powermeter pm")
    run(app, "set replyToPlaceholder ''")
    run(app, "pm.send_communication_check true")
    assert app.fakes[1].published[-1][1] == "true"
    run(app, "lifecycle.send_request_get_everest_version {}")
    assert app.fakes[0].published[-1][1] == "{}"
    run(app, "set replyToPlaceholder XX")
    run(app, 'pm.send_powermeter_values {"meter_id":"XX"}')
    assert app.fakes[1].published[-1][1] == '{"meter_id": "XX"}'


@pytest.mark.parametrize("text", ["---", "-h", "--- [1/2] phase ---", "=== x ==="])
def test_echo_prints_any_text(app, text):
    assert output(app, f"echo {text}") == text + "\n"


# ---- variables, on and every ----

def test_variables_in_commands(app, capsys):
    run(app, 'let meter {"id":"M1","limits":[16,32]}')
    run(app, "let label plain text")
    assert app.variables == {"meter": {"id": "M1", "limits": [16, 32]}, "label": "plain text"}
    assert output(app, "echo ${meter.id} ${meter.limits[1]} ${label} ${meter.limits}") == 'M1 32 plain text [16,32]\n'
    assert "meter" in output(app, "vars")
    run(app, "echo ${missing}")
    assert "unknown variable 'missing'" in capsys.readouterr().err
    assert app.exit_code == 1


def test_let_rejects_bad_names(app, capsys):
    run(app, "let msg 1")
    run(app, "let 1x 1")
    assert capsys.readouterr().err.count("cannot be used as a variable name") == 2
    assert app.variables == {}


def test_receive_save(app):
    run(app, "client_add powermeter pm")
    app.fakes[-1].deliver("everest_api/1/powermeter/pm/e2m/start_transaction",
                          {"headers": {"replyTo": "r/1"}, "payload": {"transaction_id": "T7"}})
    run(app, "pm.receive_request_start_transaction --timeout 1 --save start")
    run(app, 'pm.send_powermeter_values {"meter_id":"${start.payload.transaction_id}"}')
    assert app.fakes[-1].published[-1][1] == '{"meter_id": "T7"}'


def test_on_bridges_a_message_to_another_client(app):
    run(app, "client_add ev_board_support ev_bsp")
    run(app, "client_add evse_board_support bsp")
    run(app, 'on ev_bsp.receive_set_cp_state bsp.send_event {"event":"${msg}"}')
    app.fakes[0].deliver("everest_api/1/ev_board_support/ev_bsp/e2m/set_cp_state", "B")
    app.rules.wait_idle()
    assert app.fakes[1].published == [("everest_api/1/evse_board_support/bsp/m2e/event", '{"event": "B"}')]


def test_on_conditions_and_once(app):
    run(app, "client_add evse_board_support bsp")
    run(app, 'on bsp.receive_allow_power_on --if msg.allow_power_on=true bsp.send_event {"event":"PowerOn"}')
    run(app, 'on --if msg.allow_power_on=false bsp.receive_allow_power_on bsp.send_event {"event":"PowerOff"}')
    run(app, "on --once bsp.receive_allow_power_on let first ${msg.reason}")
    topic = "everest_api/1/evse_board_support/bsp/e2m/allow_power_on"
    for value, reason in ((True, "a"), (False, "b"), (True, "c")):
        app.fakes[0].deliver(topic, {"allow_power_on": value, "reason": reason})
    app.rules.wait_idle()
    assert [json.loads(data)["event"] for _, data in app.fakes[0].published] == ["PowerOn", "PowerOff", "PowerOn"]
    assert app.variables == {"first": "a"}
    assert len(app.rules.rules()) == 2


def test_rules_are_expanded_when_they_run(app):
    run(app, "on pm.receive_request_start_transaction pm.send_powermeter_values {\"meter_id\":\"${id}\"}")
    run(app, "client_add powermeter pm")  # a rule may refer to a client added later
    run(app, "let id first")
    topic = "everest_api/1/powermeter/pm/e2m/start_transaction"
    app.fakes[0].deliver(topic, {})
    app.rules.wait_idle()
    run(app, "let id second")
    app.fakes[0].deliver(topic, {})
    app.rules.wait_idle()
    assert [data for _, data in app.fakes[0].published] == ['{"meter_id": "first"}', '{"meter_id": "second"}']


def test_every_with_count(app):
    run(app, "client_add powermeter pm")
    run(app, "every 0.05 --count 3 pm.send_communication_check true")
    deadline = time.monotonic() + 5
    while len(app.fakes[0].published) < 3 and time.monotonic() < deadline:
        time.sleep(0.01)
    time.sleep(0.2)
    app.rules.wait_idle()
    assert len(app.fakes[0].published) == 3
    assert app.rules.rules() == []


def test_rule_remove(app, capsys):
    run(app, "every 10 echo a")
    run(app, "on pm.receive_heartbeat echo b")
    assert "1: every 10s" in output(app, "rules")
    run(app, "rule_remove 1")
    assert [rule.rule_id for rule in app.rules.rules()] == [2]
    run(app, "rule_remove 7")
    assert "No rule '7'" in capsys.readouterr().err
    run(app, "rule_remove all")
    assert app.rules.rules() == []


@pytest.mark.parametrize("line", ["on pm.receive_heartbeat pm.receive_heartbeat",
                                  "on pm.send_communication_check echo x",
                                  "every 1 wait_for x",
                                  "every 0 echo x",
                                  "on --if nonsense pm.receive_heartbeat echo x"])
def test_invalid_rules_are_rejected(app, capsys, line):
    run(app, line)
    assert capsys.readouterr().err
    assert app.rules.rules() == []


def test_a_failing_rule_is_reported(app):
    run(app, "on pm.receive_heartbeat nobody.send_x {}")
    run(app, "client_add powermeter pm")
    with mock.patch.object(app, "log_event", wraps=app.log_event) as log_event:
        app.fakes[0].deliver("everest_api/1/powermeter/pm/e2m/heartbeat", 1)
        app.rules.wait_idle()
    assert any(call.args[0] == "ERROR:" and "not a send command" in call.args[1]
               for call in log_event.call_args_list)


# ---- expressions ----

@pytest.mark.parametrize("expression,expected", [
    ("1 + 2 * 3", "7"),
    ("min(32, 16) if 3 <= duty <= 7 else 0", "0"),
    ("round(energy / 3, 1)", "33.3"),
    ("m.a.b[1] * 10", "20"),
    ('m["a"]["b"][0]', "1"),
    ("true and not false", "true"),
    ("'x' if duty > 50 else 'y'", "x"),
])
def test_expressions(app, expression, expected):
    run(app, "let duty 53.3")
    run(app, "let energy 100")
    run(app, 'let m {"a":{"b":[1,2]}}')
    assert output(app, f"echo ${{= {expression}}}") == expected + "\n"


@pytest.mark.parametrize("expression", ['__import__("os")', "duty.real", "(lambda: 1)()", "1/0", "nope + 1", "1 +"])
def test_bad_expressions_are_errors(app, capsys, expression):
    run(app, "let duty 1")
    run(app, f"echo ${{= {expression}}}")
    assert capsys.readouterr().err
    assert app.exit_code == 1


def test_now_is_a_utc_timestamp(app):
    out = output(app, "echo ${= now()}").strip()
    assert re.fullmatch(r"\d{4}-\d\d-\d\dT\d\d:\d\d:\d\d\.\d{3}Z", out)


def test_rules_and_echo_keep_redirection_characters(app, tmp_path, monkeypatch):
    monkeypatch.chdir(tmp_path)
    assert output(app, "echo a | b > c") == "a | b > c\n"
    run(app, "let phases 3")
    run(app, "on pm.receive_heartbeat let x ${= 1 if phases > 1 else 0}")
    assert app.rules.rules()[0].command == "let x ${= 1 if phases > 1 else 0}"
    run(app, "client_add powermeter pm")
    app.fakes[0].deliver("everest_api/1/powermeter/pm/e2m/heartbeat", 1)
    app.rules.wait_idle()
    assert app.variables["x"] == 1
    assert list(tmp_path.iterdir()) == []


# ---- payloads are the rest of the line ----

@pytest.mark.parametrize("payload", ['{"a": 1, "b": [1, 2]}', '{"type":"T","sub_type":"","message":"a b c"}',
                                     '{"x": "a > b | c"}'])
def test_payload_as_typed(app, payload):
    run(app, "client_add powermeter pm")
    run(app, f"pm.send_powermeter_values {payload}")
    assert json.loads(app.fakes[0].published[-1][1]) == json.loads(payload)


def test_payload_after_reply_to_and_parameters(app):
    run(app, "client_add entrypoint")
    run(app, 'entrypoint.send_request_query_module powermeter {"headers": {"replyTo": "r/1"}}')
    assert app.fakes[0].published[-1] == ("everest_api/query-modules/powermeter", '{"headers": {"replyTo": "r/1"}}')
    run(app, "client_add powermeter pm")
    run(app, 'pm.send_reply_stop_transaction r/2 {"status": "OK", "error": ""}')
    assert app.fakes[1].published[-1] == ("r/2", '{"status": "OK", "error": ""}')


def test_payload_with_spaces_in_a_rule(app):
    run(app, "client_add over_voltage_monitor ovm")
    run(app, 'on ovm.receive_reset_over_voltage_error ovm.send_clear_error {"type": "T", "sub_type": "", "message": "a b"}')
    app.fakes[0].deliver("everest_api/1/over_voltage_monitor/ovm/e2m/reset_over_voltage_error", None)
    app.rules.wait_idle()
    assert json.loads(app.fakes[0].published[-1][1]) == {"type": "T", "sub_type": "", "message": "a b"}


def test_missing_payload(app, capsys):
    run(app, "client_add powermeter pm")
    run(app, "pm.send_powermeter_values")
    assert "payload is missing" in capsys.readouterr().err
    assert app.fakes[0].published == []


# ---- third review ----

def test_alias_keeps_the_payload(app):
    run(app, "client_add powermeter pm")
    run(app, "alias create sr pm.send_reply_stop_transaction r/2")
    run(app, 'sr {"status": "OK", "error": ""}')
    assert app.fakes[0].published[-1] == ("r/2", '{"status": "OK", "error": ""}')


def test_terminator_is_not_part_of_the_payload(app):
    run(app, "client_add powermeter pm")
    run(app, 'pm.send_powermeter_values {"a": "x;y"};')
    assert app.fakes[0].published[-1][1] == '{"a": "x;y"}'


@pytest.mark.parametrize("expression", ["int(1e400)", "not " * 3000 + "1", '"%99999d" % 1', '"a" * 500000000',
                                        'float("nan")'])
def test_every_evaluation_error_fails_the_script(app, tmp_path, expression):
    script = tmp_path / "s.script"
    script.write_text(f"echo ${{= {expression}}}\necho NOT-REACHED\n")
    assert "NOT-REACHED" not in output(app, f"run_script {script}")
    assert app.exit_code == 1


def test_substitution_is_one_pass_and_allows_braces(app):
    app.set_variable("a", "${b}")  # a value that looks like a reference is not expanded again
    run(app, "let b X")
    run(app, 'let s say "hi"')
    assert output(app, 'echo ${= a} ${= json({"k": "}"})} ${= json(s)} ${= 3 in (1, 3)} ${other-thing}') == \
        '${b} {"k":"}"} "say \\"hi\\"" true ${other-thing}\n'


def test_definition_errors_fail_the_script(app, tmp_path, capsys):
    for line in ("let 1x 2", "on nobody pm.send_x {}", "on pm.receive_y wait_for x", "every 0 echo x",
                 "rule_remove 9"):
        app.exit_code = 0
        script = tmp_path / "s.script"
        script.write_text(f"{line}\necho NOT-REACHED\n")
        assert "NOT-REACHED" not in output(app, f"run_script {script}"), line
        assert app.exit_code == 1, line


def test_send_errors_in_rules_are_reported(app):
    run(app, "client_add powermeter pm")
    run(app, 'on pm.receive_heartbeat pm.send_powermeter_values {"m": "${msg}"}')
    with mock.patch.object(app, "log_event", wraps=app.log_event) as log_event:
        app.fakes[0].deliver("everest_api/1/powermeter/pm/e2m/heartbeat", 'say "hi"')
        app.rules.wait_idle()
    assert any(call.args[0] == "ERROR:" and "invalid JSON payload" in call.args[1]
               for call in log_event.call_args_list)
    run(app, "rule_remove all")
    run(app, 'on pm.receive_heartbeat pm.send_powermeter_values {"m": ${= json(msg)}}')
    app.fakes[0].deliver("everest_api/1/powermeter/pm/e2m/heartbeat", 'say "hi"')
    app.rules.wait_idle()
    assert json.loads(app.fakes[0].published[-1][1]) == {"m": 'say "hi"'}


def test_save_checks_the_name(app, capsys):
    run(app, "client_add powermeter pm")
    app.fakes[0].deliver("everest_api/1/powermeter/pm/e2m/start_transaction", {})
    run(app, "pm.receive_request_start_transaction --timeout 1 --save msg")
    assert "cannot be used as a variable name" in capsys.readouterr().err
    assert "msg" not in app.variables


def test_a_queued_once_rule_can_still_be_removed(app):
    started, release = threading.Event(), threading.Event()
    run(app, "on --once pm.receive_heartbeat let x 1")
    real_run = app.rules._run

    def slow_run(rule, scope):
        if rule.rule_id == 99:
            started.set()
            release.wait(5)
        real_run(rule, scope)
    with mock.patch.object(app.rules, "_run", slow_run):
        blocker = app.rules.add_on("pm.receive_block", "let y 1")
        blocker.rule_id = 99
        app.rules.message_received("pm.receive_block", {})
        started.wait(5)
        app.rules.message_received("pm.receive_heartbeat", {"msg": 1})
        assert [rule.rule_id for rule in app.rules.rules() if rule.once]  # still listed while queued
        run(app, "rule_remove 1")
        release.set()
        app.rules.wait_idle()
    assert "x" not in app.variables


def test_several_commands_run_in_order_in_one_tick(app):
    run(app, "let n 0")
    run(app, "every 0.05 --count 3 let n ${= n + 1}; let twice ${= n * 2}")
    deadline = time.monotonic() + 5
    while app.rules.rules() and time.monotonic() < deadline:
        time.sleep(0.01)
    app.rules.wait_idle()
    assert app.variables["n"] == 3 and app.variables["twice"] == 6


def test_every_with_an_expression_condition_and_variables(app):
    run(app, "let --default period 0.05")
    run(app, "let --default period 9")
    run(app, "let times 2")
    run(app, "let on 0")
    run(app, "let n 0")
    run(app, "every ${period} --count ${times} --if ${= on == 1 and n < 10} let n ${= n + 1}")
    time.sleep(0.3)
    app.rules.wait_idle()
    assert app.variables["n"] == 0 and app.rules.rules()
    run(app, "let on 1")
    deadline = time.monotonic() + 5
    while app.rules.rules() and time.monotonic() < deadline:
        time.sleep(0.01)
    app.rules.wait_idle()
    assert app.variables["n"] == 2


def test_rules_stop_on_quit(app):
    run(app, "every 0.05 let x 1")
    app.postloop()
    assert app.rules.rules() == []
