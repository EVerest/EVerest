# SPDX-License-Identifier: Apache-2.0
# Copyright Pionix GmbH and Contributors to EVerest
"""Tests for the generated API clients: reply topics and message routing."""

import importlib
import importlib.util
import json
import sys
from pathlib import Path
from types import SimpleNamespace

import pytest
import yaml

from everest_api_client_gen.generator import generate
from everest_api_client_gen.model import build_model
from everest_api_client_gen.spec_loader import load_spec

SPECS_DIR = Path(__file__).resolve().parents[4] / "docs" / "source" / "reference" / "EVerest_API"


def test_reply_subscription_matches_generated_reply_to():
    """api_client.py sends replyTo as the request topic with m2e turned into
    e2m; the reply receiver must subscribe there."""
    model = build_model(load_spec(SPECS_DIR / "error_history_consumer_API.yaml"),
                        "ErrorHistoryConsumerAPI", "error_history_consumer_api", "eh", "default")
    reply = next(o for o in model.recv_ops if o.op_id == "receive_reply_get_errors")
    assert model.subscribe_topic(reply) == "e2m/get_errors/+"
    assert model.receive_topic(reply) == "e2m/get_errors"


class _FakeClient:
    def __init__(self):
        self.on_message = None

    def subscribe(self, topic):
        pass


def _load_client_module(tmp_path, spec_name, snake):
    pytest.importorskip("paho.mqtt.client")
    generate(SPECS_DIR / spec_name, tmp_path, "X", snake, "x")
    path = tmp_path / "src" / snake / "client.py"
    spec = importlib.util.spec_from_file_location(f"{snake}_client", path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


@pytest.mark.parametrize("spec_name, snake, topics, expected_key", [
    ("isolation_monitor_API.yaml", "isolation_monitor_api",
     ["p/e2m/start", "p/e2m/start_self_test"], "p/e2m/start_self_test"),
    ("error_history_consumer_API.yaml", "error_history_consumer_api",
     ["p/e2m/error_raised", "p/e2m/error_raised/get/+"], "p/e2m/error_raised/get"),
])
def test_messages_reach_only_their_own_topic(tmp_path, spec_name, snake, topics, expected_key):
    module = _load_client_module(tmp_path, spec_name, snake)
    for order in (topics, list(reversed(topics))):
        helper = module._MQTTHelper(_FakeClient(), order)
        message_topic = expected_key + ("/uuid-1" if expected_key.endswith("/get") else "")
        helper._on_message(None, None, SimpleNamespace(topic=message_topic, payload=b"1"))
        assert helper.read_and_clear_buffer(expected_key) == [b"1"]
        for key in helper._buffer:
            assert helper._buffer[key] == [], f"{message_topic} also delivered to {key}"


def test_example_payload_with_quotes_generates_valid_code(tmp_path):
    """Examples are offered as tab completion; one with a quote or a
    backslash must still produce a valid Python literal."""
    pytest.importorskip("paho.mqtt.client")
    doc = load_spec(SPECS_DIR / "slac_API.yaml")
    messages = doc["components"]["messages"]
    op_id = next(o for o, op in doc["operations"].items()
                 if op["action"] == "send" and len(messages[o].get("examples") or []) == 1)
    payload = {"note": "it's a \\ \"quoted\" value"}
    messages[op_id]["examples"][0]["payload"] = payload
    spec = tmp_path / "slac_API.yaml"
    spec.write_text(yaml.safe_dump(doc, sort_keys=False), encoding="utf-8")
    generate(spec, tmp_path / "out", "X", "x_api", "x")
    sys.path.insert(0, str(tmp_path / "out" / "src"))
    try:
        module = importlib.import_module("x_api.XClientCommandSet")
    finally:
        sys.path.remove(str(tmp_path / "out" / "src"))
    completions = getattr(module.XClientCommandSet, f"complete_{op_id}_payload")(None, "", "", 0, 0)
    assert [json.loads(c) for c in completions] == [payload]
