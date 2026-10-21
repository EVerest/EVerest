# SPDX-License-Identifier: Apache-2.0
# Copyright Pionix GmbH and Contributors to EVerest
"""Tests for the EVerest API client built from the AsyncAPI specs of this tree.

No MQTT broker is needed: the paho client is replaced by a fake that records
subscriptions and publications and lets the tests inject messages.
"""

import io
import json
import readline
import sys
import threading
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


# ---- second review ----

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
